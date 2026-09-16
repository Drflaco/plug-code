# Distorsion — `core.drive`

Mise en forme d'onde à trois formes fondues, anti-repliement par **intégration
de la non-linéarité** plutôt que par suréchantillonnage. **Latence nulle.**

| | |
| --- | --- |
| Identité | `core.drive` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-6 dB** — une mise en forme rend un signal en phase avec le sec (§3.7) |
| Latence | **0 échantillon** (voir « Le choix de l'anti-repliement » ci-dessous) |
| Entrées occupées | `main`, `paramA`, `paramB`, `paramC` |
| Paramètre structurel | **aucun** — rien ici ne peut changer la latence entre deux passes |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Drive | 0 dB → +36 dB d'attaque, exponentielle (milieu = +18 dB) | libre | glissement |
| `paramA` | Forme | 0 = doux (tanh) · 0,5 = écrêtage franc · 1 = repli ; fondu continu entre les trois | libre | glissement |
| `paramB` | Asymétrie | 0,5 = symétrique ; 0 et 1 = ±1 de décalage avant la forme | libre | glissement |
| `paramC` | Lissage | passe-bas d'un pôle après la forme : 1 kHz → 20 kHz, exponentielle | libre | glissement |

**Pas de niveau de sortie ici.** L'entrée `gain` de l'emplacement appartient au
moteur (§3.2) : c'est elle qui rattrape le niveau quand on pousse le drive. La
skill ne double pas ce réglage.

## Le choix de l'anti-repliement — et sa conséquence

Le catalogue (§3.8) annonce la distorsion en « moyen (suréchantillonnage) »,
latence « nulle ou faible ». **Ce module ne suréchantillonne pas.** Il rend, pour
chaque échantillon, la **moyenne de la forme sur le segment qui joint
l'échantillon précédent au courant** — c'est-à-dire la différence des primitives
divisée par la différence des entrées (ADAA d'ordre 1). Les primitives des trois
formes sont exactes et écrites en clair dans le source.

Pourquoi ce choix plutôt qu'un suréchantillonneur :

- **Latence nulle, vraiment nulle et constante.** Aucun taux à exposer, donc
  aucun paramètre structurel, donc aucune latence qui pourrait changer entre
  deux passes (§3.3.1, §4.3).
- **Aucune allocation, aucun buffer.** L'état tient en quatre nombres par canal.
- **Un suréchantillonneur à phase linéaire aurait posé un problème de mesure** :
  sa réponse impulsionnelle oscille *avant* son pic, et l'épreuve de latence du
  vérificateur cherche le premier échantillon au-dessus de 1e-4. Elle lirait donc
  une latence plus courte que la latence déclarée, et refuserait une déclaration
  pourtant juste. Le point est signalé au pilote : il se posera pour de bon avec
  le pitch shift granulaire, seul effet latent du catalogue.

Le prix, dit ici parce qu'il s'entend :

- L'intégration d'ordre 1 vaut un **moyennage sur un échantillon** : retard de
  groupe d'un **demi-échantillon** et **-1,25 dB à 8 kHz** (à 48 kHz). La latence
  déclarée à l'hôte est un entier ; zéro est la seule valeur honnête, et le
  demi-échantillon reste sous la tolérance d'un échantillon du vérificateur.
- L'anti-repliement est **partiel**, pas total. C'est à cela que sert le
  `Lissage` : baissé, il retire ce qui reste de friture dans le haut.

**Le jour où le socle transmettra `master.quality` (§3.10)**, c'est l'**ordre de
l'intégration** qui s'y brancherait (ordre 1 en éco et normal, ordre 2 en haute),
jamais un taux de suréchantillonnage : la latence resterait nulle dans les trois
positions, et le réglage garderait sa nature structurelle sans jamais rien
décaler. Aucun code ne le prépare aujourd'hui — le socle ne transmet rien, et il
n'y a rien à inventer avant.

## Le reste du contrat

**Le fondu des formes est continu, donc la forme est libre.** La primitive d'un
mélange linéaire est le mélange linéaire des primitives : l'anti-repliement reste
exact au milieu du fondu. C'est ce qui permet de laisser `Forme` **libre** au
lieu d'en faire des paliers verrouillés comme le type du filtre — il n'y a pas de
marche à craindre entre deux pas.

**Les deux bouts du segment sont toujours évalués avec les mêmes réglages.** Quand
le drive, la forme ou l'asymétrie bougent, l'échantillon précédent est repassé par
la non-linéarité **courante** avant la division. La skill rend donc toujours la
moyenne d'une forme fixe, jamais un mélange de deux — sans quoi un glissement de
drive produirait une marche à chaque échantillon.

**L'asymétrie ne laisse pas de continu.** La sortie vaut `f(u) - f(biais)` : la
composante continue qu'engendre le décalage est retirée, donc le silence en entrée
rend le silence même à asymétrie maximale, et rien ne part en continu dans la
suite de la chaîne.

**Les trois formes sont bornées à 1.** Quel que soit le drive, la sortie reste
dans ±1 (mesuré, cas 7). Le repli, lui, ne plafonne pas : au-delà de 1 l'onde
revient, ce qui engendre beaucoup de partiels — c'est son intérêt et son danger.

## Cas numériques (`selfTest`, 48 kHz)

1. **Drive** — harmonique 3 d'un sinus à 1 kHz : -42,8 dB à 0 dB de drive,
   -9,7 dB à +36 dB (un carré vaut -9,5 dB).
2. **Forme (doux)** — continu 0,5 poussé de 6,02 dB : sortie 0,761594, soit
   `tanh(1)` à six décimales.
3. **Forme (écrêtage)** — continu 0,5 poussé de 12,04 dB : sortie 1,000000. Et un
   sinus poussé à fond devient un carré : fondamentale 1,27190 (4/π = 1,27324) et
   rapport h3/h1 = 0,33060 (1/3 attendu).
4. **Forme (repli)** — gain 4 : un continu à 0,25 atteint la crête (1,000000,
   `sin(π/2)`) et un continu à 0,5 est replié jusqu'au zéro (-0,000000, `sin(π)`).
5. **Asymétrie** — harmonique 2 relative : 0,000000051 à 0,5 (symétrie parfaite)
   et 0,34113 à 1. Silence en entrée avec asymétrie maximale : crête 0,000000000.
6. **Lissage** — sinus à 8 kHz : 17,4 dB d'écart entre lissage 1 (20 kHz) et
   lissage 0 (1 kHz).
7. **Bornage** — drive +36 dB sur un sinus pleine échelle, les trois formes :
   crête maximale 1,000000.
