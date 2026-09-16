# Réverbe — `core.reverb`

Réverbe à **huit peignes amortis en parallèle** puis **quatre passe-tout en
série**, par canal, **latence nulle**. L'autre moitié du geste 1 : gate et
réverbe séquencés.

| | |
| --- | --- |
| Identité | `core.reverb` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-3 dB** — le traité est décorrélé du sec (§3.7) |
| Latence | **0 échantillon** (aucune pré-écoute, aucun bloc à accumuler) |
| Entrées occupées | `main`, `paramA`, `paramB`, `paramC`, `paramD`, `stereo` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Décroissance | 0,2 s → 6 s (RT60), exponentielle | libre | glissement |
| `paramA` | Amortissement | 0 = transparent · 0,5 ≈ 11 kHz · 1 ≈ 800 Hz (passe-bas de boucle, à 48 kHz) | libre | glissement |
| `paramB` | Taille | 35 % → 100 % (première réflexion 10 ms → 30 ms) | **verrouillée par défaut** | saut |
| `paramC` | Diffusion | 0 → 0,7 (coefficient des quatre passe-tout) | libre | glissement |
| `paramD` | Grave | 20 Hz → 500 Hz (passe-haut de boucle) | libre | glissement |
| `stereo` | Largeur | 0 = queue mono · 1 = chaque canal garde sa queue | libre | glissement |

**Pourquoi la taille est verrouillée par défaut.** La changer déplace d'un coup
les têtes des huit peignes : la queue en cours se replie sur une autre longueur
et s'entend comme un décrochage, pas comme une pièce qui grandit. Elle ne touche
ni à la latence (toujours nulle) ni à l'allocation (les lignes sont déjà à leur
taille maximale), donc elle n'est **pas structurelle** au sens du §3.3.1 — juste
coûteuse à moduler. Déverrouille-la si le décrochage est justement l'effet
cherché ; rien ne l'interdit.

## Décroissance, et la borne qui la plafonne

La réinjection de chaque peigne est calculée depuis le RT60 demandé :
`g = 10^(-3·L / (RT60 · sr))`. Chaque peigne perd donc 60 dB en RT60, quelle que
soit sa longueur — c'est ce qui donne une décroissance unique, et mesurable.

Cette réinjection est bornée à **0,985, strictement sous 1** : la boucle d'un
peigne est une récursion pure, à 1 elle ne s'éteint jamais. Aux **très petites
tailles**, où les lignes sont courtes et donc rebouclées souvent, cette borne
plafonne la décroissance réelle avant les 6 s affichés (autour de 4 à 5 s). Le
plafond est assumé : une réverbe qui ne s'éteint pas n'est pas une réverbe.

## Les deux filtres de boucle

- **Amortissement** — passe-bas à un pôle dans chaque peigne : la queue perd son
  aigu avant son grave, comme une pièce meublée. À 0, il est exactement
  transparent.
- **Grave** — passe-haut à un pôle dans chaque peigne, **normalisé** par
  `(1+R)/2`. Sa forme brute gagnerait `2/(1+R)` dans l'aigu, ce qui ferait passer
  la boucle au-dessus de 1 quand le grave est coupé haut et la décroissance
  longue. Normalisé, le gain de boucle reste celui du peigne seul.

## Diffusion — des passe-tout vrais

Les quatre passe-tout de sortie sont de la forme `(z^-L - g)/(1 - g·z^-L)` :
leur module vaut 1 à toutes les fréquences. Ils étalent les réflexions en nappe
**sans colorer ni changer la durée de la queue**, qui reste celle des peignes
seuls. C'est aussi ce qui rend la décroissance testable au dB près.

## Préallocation

Les douze lignes de chaque canal sont allouées **une seule fois** dans
`prepare()`, à leur longueur maximale (facteur de taille 1, plus le décalage du
canal droit) — des constantes de compilation. Changer la taille ne fait que
raccourcir la **longueur utile** à l'intérieur de la même ligne : rien n'est
jamais réalloué.

## Largeur

Le canal droit voit toutes ses lignes décalées d'une demi-milliseconde fixe :
les deux queues sont donc naturellement décorrélées. La largeur mélange ensuite
les deux : à 0, les deux sorties sont **exactement** le même signal (queue mono) ;
à 1, chacune garde la sienne.

## Queue à la désactivation (§3.3.2)

Le mode de queue est **entièrement tenu par le socle** (`Engine.cpp`). En
*laissée mourir* (défaut des effets à queue), il cesse d'alimenter la skill —
elle reçoit du silence — mais continue de mélanger sa sortie. En *coupée*, la
skill reçoit l'entrée entière et c'est le gain du traité que le socle met à zéro.

À la charge de la skill, et vérifié : **que la queue se tienne quand l'entrée
devient silencieuse**. Chaque peigne continue alors sur son seul contenu et perd
son facteur `g` à chaque tour, donc l'énergie ne peut que décroître.
`reset()` purge les douze lignes et tous les états de filtre.

## Cas numériques (`selfTest`, 48 kHz)

1. **Décroissance** — mesurée sur une impulsion : -59,9 dB en 1 s pour 1 s
   demandé, -19,9 dB pour 3 s demandé (60 et 20 attendus).
2. **Amortissement** — à fond, la queue à 150-250 ms perd 25,5 dB à 10 kHz et
   0,85 dB à 200 Hz.
3. **Taille** — la première réflexion arrive à l'échantillon 1425 à 100 % et 498
   à 35 %, exactement la longueur du plus court peigne.
4. **Diffusion** — facteur de crête sur les 150 premières ms : 20,1 à 0 (échos
   isolés) contre 6,4 à fond (nappe dense).
5. **Grave** — à fond, la queue à 300-400 ms perd 48,8 dB à 60 Hz et 0,49 dB à
   4 kHz.
6. **Largeur** — à 0, les deux canaux sont identiques au bit près (écart
   0,000000000000) ; à 1, l'écart entre eux vaut 141 % de la queue.
7. **Queue** — impulsion puis silence : 12 fenêtres de 100 ms strictement
   décroissantes, la dernière à -77,9 dB de la première, et silence **exact**
   après `reset()`.
