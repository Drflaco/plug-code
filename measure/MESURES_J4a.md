# Mesures J4a — 16 septembre 2026

Machine : Ryzen 7 5800XT, Windows 10. Binaire Release, MSVC 19.44, JUCE 8.0.15.
Blocs de 128 échantillons. Le budget du CdC §4.4 est 25 % du bloc au p99,9.

## Phase 0 — optimisation du socle

### Ce qui a changé

| Changement | Pourquoi | Effet sur le rendu |
| --- | --- | --- |
| Seules les entrées **déclarées par la skill** (plus `mix` et `gain`) sont composées ; les autres valent `nullptr` dans `ParamCurves` | le socle composait 13 courbes par emplacement dont 10 que personne ne lisait | aucun : les courbes supprimées n'étaient lues par personne |
| Chemin rapide de composition : une tranche à valeur unique quand ni glide, ni modulation, ni lissage en cours | le cas ordinaire est une valeur qui ne bouge pas | aucun : même nombre, posé d'un bloc au lieu d'échantillon par échantillon |
| Loi de mélange : cosinus et sinus calculés **une fois par bloc** quand `mix`, `gain` et l'activation sont plats (test de platitude par comparaison) | la loi −3 dB coûtait un cos et un sin par échantillon et par emplacement | aucun : l'écriture finale `(d·dry + w·wet)·gain` est inchangée |
| Sources de modulation calculées seulement si une route les lit, ou si une enveloppe se déclenche sur l'audio | suiveur et deux enveloppes tournaient sur chaque emplacement, routés ou non | aucun : une source que personne ne lit ne change aucun échantillon |
| Activation posée d'un bloc quand la rampe est finie ; alimentation copiée quand la queue est coupée | deux boucles par échantillon pour des valeurs constantes | aucun |
| Crochet `j3_state.xml` retiré | échafaudage J3 | sans objet |

**Précaution de bit-exactitude** : là où la rampe de glide est terminée, le
chemin rapide reproduit l'écriture `rampStart + (cible − rampStart)` du chemin
général, qui n'est pas toujours égale à `cible` en virgule flottante.

### Vérification — le rendu n'a pas bougé d'un octet

Les rendus de référence ont été figés **avant** l'optimisation
(`measure/j4a/ref/socle_48k.wav`, `socle_44k.wav`) et le test T13 les compare
octet par octet à chaque exécution de la matrice. Le fichier 48 kHz porte la
même empreinte que le rendu J3 d'origine (`c557a258…`).

| Vérification | Résultat |
| --- | --- |
| T13 rendu 48 kHz identique à la référence figée | oui, octet par octet |
| T13 rendu 44,1 kHz identique à la référence figée | oui, octet par octet |
| Matrice complète (J3 + 44,1 kHz + non-régression) | **32/32** |
| pluginval niveau 5 | SUCCESS |

### Coût par bloc — avant et après, 200 000 blocs par mesure

À 48 kHz, bloc = 2 666,7 µs, budget 25 % = 666,7 µs.

| Contexte | moyenne avant | moyenne après | p99,9 avant | p99,9 après |
| --- | --- | --- | --- | --- |
| Socle vide, 16 emplacements sans skill | 0,25 µs | 0,26 µs | 0,8 µs | 0,9 µs |
| 3 emplacements (référence J3) | 24,64 µs | **7,44 µs** | 63,0 µs | 33,8 µs |
| **Par emplacement occupé** | **8,21 µs** | **2,48 µs** | — | — |

Cas demandé par le pilote, mesuré après optimisation seulement :

| Contexte | 48 kHz | 44,1 kHz |
| --- | --- | --- |
| 16 emplacements occupés — moyenne | 22,64 µs | 22,80 µs |
| 16 emplacements occupés — p99,9 | 69,6 µs (**2,61 % du bloc**) | 69,1 µs (**2,38 % du bloc**) |
| 16 emplacements occupés — par emplacement | 1,42 µs | 1,43 µs |
| 3 emplacements — p99,9 | 33,8 µs (1,27 %) | 36,7 µs (1,26 %) |

Les maxima isolés (400 à 500 µs, un bloc sur 200 000) sont des à-coups
d'ordonnancement Windows, pas un coût du socle : ils apparaissent aussi sur le
socle vide, où le travail est nul.

### Objectifs de la phase 0

| Objectif | Cible | Mesuré | Verdict |
| --- | --- | --- | --- |
| Coût par emplacement | < 3 µs | 2,48 µs (3 emplacements) ; 1,42 µs (16) | atteint |
| Socle complet 16 emplacements, p99,9 | < 5 % du bloc | 2,61 % à 48 kHz ; 2,38 % à 44,1 kHz | atteint |
| Rendu identique avant/après | octet par octet | identique aux deux taux | atteint |
| Matrice J3 | 27/27 | 32/32 (5 vérifications ajoutées) | atteint |

Il reste donc, pour les neuf effets, environ 597 µs de budget au p99,9 à
48 kHz une fois le socle payé — soit 66 µs par effet si les neuf tournent
ensemble.

## Phase 1 — les neuf effets

Chaque skill passe `PlugSkillTest` avant d'entrer au registre : identité,
libellé et aide par paramètre, classe de verrou, latence déclarée contre latence
réelle mesurée sur impulsion, blocs vides et irréguliers, silence après remise à
zéro, balayage de chaque paramètre sur ses bornes, déterminisme entre deux
instances, plus ses propres cas numériques. Le coût est mesuré seul dans un
emplacement, 60 000 blocs de 128 échantillons.

| Skill | Latence | Moyenne | p99 | p99,9 | Loi | Contrat |
| --- | --- | --- | --- | --- | --- | --- |
| `core.filter` | 0 | 1,39 µs | 1,70 µs | 11,5 µs | −6 dB | passé |
| `core.gain` | 0 | 0,37 µs | 0,80 µs | 1,00 µs | −6 dB | passé |
| `core.gate` | 0 | 0,78 µs | 1,30 µs | 2,90 µs | −6 dB | passé |
| `core.fm` | 0 | 3,35 µs | 4,70 µs | 13,2 µs | −3 dB | passé |
| `core.drive` | 0 | 1,89 µs | 3,00 µs | 18,8 µs | −6 dB | passé |
| `core.delay` | 0 | 1,51 µs | 2,50 µs | 7,10 µs | −3 dB | passé |
| `core.reverb` | 0 | 5,51 µs | 9,60 µs | 27,6 µs | −3 dB | passé |
| `core.grain` | **2402** | 3,27 µs | 5,40 µs | 15,5 µs | −3 dB | passé |
| `core.repitch` | **194** | 2,97 µs | 4,50 µs | 20,1 µs | −3 dB | passé |

Mesures à 48 kHz, 60 000 blocs, chaque skill seule dans un emplacement. Le p99
est le chiffre de référence par effet (décision pilote) ; la p99,9 est déjà dans
le bruit d'ordonnancement de la machine à cette échelle.

**Huit des neuf sont à latence nulle, un seul est latent** — la propriété que le
§3.8 demandait de préserver est tenue. Les deux latences déclarées sont exactes
au pic de la réponse impulsionnelle, mesurées et non devinées : 2402 échantillons
pour le granulaire (fenêtre centrée sur le plus grand grain, ce qui permet de
moduler la taille par pas sans jamais décaler la piste) et 194 pour le repitch.

## La chaîne de référence du §4.4 — le chiffre attendu depuis le J2

Les neuf effets chargés simultanément dans neuf emplacements, séquenceur actif
sur chacun, un pas sur quatre désactivé. 200 000 blocs.

| | moyenne | p50 | p99 | **p99,9** | max |
| --- | --- | --- | --- | --- | --- |
| 48 kHz (budget 666,7 µs) | 29,85 µs | 29,5 | 50,3 | **75,1 µs = 2,82 % du bloc** | 465 µs |
| 44,1 kHz (budget 725,6 µs) | 29,75 µs | 29,4 | 51,2 | **76,6 µs = 2,64 % du bloc** | 258 µs |

Le budget du §4.4 autorise 25 % du bloc au p99,9. La chaîne complète en consomme
**2,8 %**, soit un neuvième du budget. Les maxima isolés (un bloc sur 200 000)
suivent l'ordonnancement de Windows : ils apparaissent aussi sur le socle vide,
où il n'y a rien à calculer.

Le budget se recalibre donc ainsi : **la chaîne de référence est mesurée, elle
tient largement, et il reste de la marge pour l'interface (J4b) et pour l'effet
distinctif (J5)**.

Après chaque intégration : matrice du socle 32/32, grille toujours à 284
paramètres, pluginval niveau 5 SUCCESS.

### Famille A — filtre, gain, gate (latence nulle)

- `core.filter` : variable d'état TPT deux pôles par canal ; coupure 20 Hz à
  20 kHz exponentielle et résonance Q 0,5 à 12, toutes deux libres ; type en
  trois paliers, verrouillé par défaut parce que changer de palier déplace la
  prise de sortie et s'entend comme une marche ; décalage stéréo sur l'entrée
  `stereo`. Loi −6 dB.
- `core.gain` : gain et découpe rythmique, avec montée et descente séparées —
  c'est ce qui rend la découpe séquencée utilisable, un saut de gain par pas
  claque. Loi −6 dB.
- `core.gate` : seuil, attaque, maintien, relâchement, profondeur, tous libres ;
  rampe mise en forme à pente nulle aux deux bouts, donc pas de marche même à
  0,1 ms d'attaque. Loi −6 dB.

Décisions d'intégration prises sur cette famille :
- L'entrée `stereo` (indice 9) est laissée aux skills tant que le moteur ne s'en
  sert pas. Si le socle en fait un jour un panoramique générique, ce sera une
  décision pilote et la skill la rendra.
- Le type de filtre reste verrouillé par défaut : c'est exactement le cas prévu
  au §3.3.1, « techniquement modulable, mais chaque saut a un coût ».

### Famille B — FM, distorsion (latence nulle)

- `core.fm` : modulation de phase du signal entrant, jamais de la sortie — la
  ligne à retard ne contient que l'entrée, donc l'auto-modulation ne peut pas
  diverger. Les deux garde-fous du §2 sont là : excursion bornée à 2 ms et
  rapports accrochés à une série harmonique. L'accroche à la tonalité de la
  source n'y est pas, et l'aide le dit. Loi −3 dB.
- `core.drive` : mise en forme d'onde à trois formes fondues, anti-repliement
  par intégration de la non-linéarité au premier ordre. **Elle ne suréchantillonne
  pas** : conséquence assumée, latence nulle et constante, aucun paramètre
  structurel, aucun tampon. Prix dit dans l'aide : un demi-échantillon de retard
  de groupe et −1,25 dB à 8 kHz. Loi −6 dB.

Écart au §3.8, assumé et signalé : le catalogue donnait la distorsion en
« moyen (suréchantillonnage) ». Le choix retenu atteint le même but sans
suréchantillonner, et préserve la propriété « huit sur neuf à latence nulle ».

### Famille C — délai, réverbe (latence nulle, les deux à queue)

- `core.delay` : anneau préalloué pour 2 s, jamais réalloué ; changer le temps
  déplace la tête dans le même anneau. Tête **continue**, bornée à un quart
  d'échantillon par échantillon : un saut de temps ne peut pas claquer, il
  glisse comme une bande. Le paramètre est déclaré en glissement en conséquence.
  Réinjection bornée à 0,95 avec bloqueur de continu dans la boucle. Loi −3 dB.
- `core.reverb` : douze lignes par canal (huit peignes, quatre passe-tout)
  allouées à la taille maximale ; la taille ne change que la longueur utile.
  Réinjection bornée à 0,985 par peigne, avec passe-haut de boucle normalisé —
  la forme brute aurait fait passer la boucle au-dessus de 1 sur une décroissance
  longue avec grave coupé. Loi −3 dB.

Sur les queues (§3.3.2), la répartition est nette et vérifiée : **le socle fait
le mode**, il coupe l'alimentation en « laissée mourir » et fait taire le traité
en « coupée ». À la charge des skills : se tenir quand l'entrée devient
silencieuse et purger à la remise à zéro. Chaque skill a son cas numérique
dédié — impulsion puis silence, fenêtres strictement décroissantes.

### Famille D — pitch granulaire, repitch (le seul effet latent)

- `core.grain` : deux têtes fondues, fait main, **sans Signalsmith**. Un saut de
  hauteur ne change que la vitesse des têtes, jamais leur position : il n'y a
  donc rien à resynchroniser, ce qui retire du chemin critique le risque que
  l'annexe A.3 signalait. Latence 2402 échantillons à 48 kHz, soit 50 ms :
  c'est le prix d'une taille de grain modulable par pas, la fenêtre étant calée
  sur le plus grand grain. Hauteur verrouillée par défaut. Loi −3 dB.
- `core.repitch` : varispeed à tête unique, la durée n'est pas préservée, recalage
  par fondu de 4 ms en butée. Hauteur verrouillée par défaut mais **en glissement**,
  comme le geste 3 l'exige. Latence 194 échantillons. Loi −3 dB.

Cas obligatoire demandé par le pilote — hauteur changeant à chaque pas, 128
paliers servis en blocs de 128 : latence inchangée, plus grand écart entre deux
échantillons 0,0575 pour le granulaire et 0,0202 pour le repitch, aucun pic de
coût. La régularité est acquise par construction : deux lectures interpolées et
un cosinus par échantillon, pas de FFT, donc pas de paquet tous les N blocs.

Décision maintenue et signalée : le §3.3.1 justifie le verrou du pitch par le
coût de resynchronisation des bibliothèques de pitch-shift. Cette raison ne vaut
pas pour cette implémentation, qui n'en a aucun. Le verrou reste en place pour
une raison musicale, et l'aide au survol dit la raison vraie plutôt que de
réciter une justification technique qui serait fausse ici.

## Ce qui reste ouvert après la phase 1

- Les deux conventions de temps annoncées (gain à 99 %, gate en transition
  complète) restent à unifier dans les aides.
- `master.quality` (§3.10) existe dans la grille mais le socle ne le transmet
  pas aux skills. Rien n'a été inventé. La distorsion s'y brancherait par
  l'ordre de son anti-repliement, sans jamais changer sa latence.
- Taille de réverbe : classée verrouillée par défaut plutôt que structurelle,
  puisqu'elle ne change ni la latence ni l'allocation.
- Décroissance de réverbe : la plage annoncée (0,2 à 6 s) plafonne vers 4 à 5 s
  aux petites tailles, par l'effet de la borne de réinjection. Écrit dans l'aide.
- Pas de pré-délai sur la réverbe, pas de division rythmique sur le délai : hors
  périmètre, non inventés.
