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

| Skill | Latence | 48 kHz moyenne | 48 kHz p99 | 48 kHz p99,9 | 44,1 kHz p99,9 | Contrat |
| --- | --- | --- | --- | --- | --- | --- |
| `core.filter` | 0 | 1,39 µs | 1,60 µs | 4,70 µs (0,18 %) | 14,80 µs (0,51 %) | passé |
| `core.gain` | 0 | 0,40 µs | 0,80 µs | 1,10 µs (0,04 %) | 1,10 µs (0,04 %) | passé |
| `core.gate` | 0 | 0,80 µs | 1,30 µs | 1,90 µs (0,07 %) | 1,80 µs (0,06 %) | passé |

Les maxima isolés (40 à 610 µs selon les passages, y compris sur les skills les
plus simples) suivent l'ordonnancement de la machine, pas le traitement : ils ne
se reproduisent pas d'une mesure à l'autre. Le chiffre utile pour le budget
§4.4 est la p99 ; la p99,9 est déjà bruitée à cette échelle.

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
