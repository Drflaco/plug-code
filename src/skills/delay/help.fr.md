# Délai — `core.delay`

Écho à une ligne par canal, **tête de lecture continue** (dite « bande »),
**latence nulle**. Sert les gestes 2 (granulaire → pitch → délai) et 3
(repitch → écho → passe-haut).

| | |
| --- | --- |
| Identité | `core.delay` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-3 dB** — le traité est décalé dans le temps, jamais en phase avec le sec (§3.7) |
| Latence | **0 échantillon** (rien à aligner, rien à déclarer à l'hôte) |
| Entrées occupées | `main`, `paramA`, `paramB`, `stereo` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Temps | 1 ms → 2 s, exponentielle (milieu ≈ 45 ms) | libre | glissement |
| `paramA` | Réinjection | 0 → **0,95**, jamais davantage | libre | glissement |
| `paramB` | Amortissement | 0 = transparent · 0,5 ≈ 11 kHz · 1 ≈ 800 Hz (coupure du passe-bas de boucle, à 48 kHz) | libre | glissement |
| `stereo` | Décalage stéréo | 0,5 = même temps des deux côtés ; 0 et 1 = ±50 % par canal (rapport 1 à 3 entre gauche et droite) | libre | glissement |

Les quatre sont **libres** : le CdC range explicitement temps de délai et
réinjection dans le terrain naturel de la variation par pas (§3.3.1).

## Le choix de lecture : la bande, pas le saut

Un anneau peut se relire de deux façons quand le temps change pendant que la
ligne sonne. On peut **sauter** la tête de lecture à sa nouvelle place : le
temps est juste immédiatement, mais le signal lu est discontinu — c'est un clic.
On peut la **déplacer** : le signal lu reste continu, il n'y a jamais de clic,
mais la hauteur glisse le temps du déplacement — c'est une bande.

**C'est le déplacement qui est retenu.** La tête se déplace d'au plus **un quart
d'échantillon par échantillon**, avec interpolation linéaire entre deux
échantillons de l'anneau. Conséquences, toutes assumées :

- un saut de temps ne peut **jamais** produire de clic, quel que soit l'écart ;
- il produit à la place un glissement de hauteur d'au plus ±25 % de vitesse ;
- un écart ordinaire (1/8 → 1/16 à 120 BPM, soit 250 ms → 125 ms) s'installe en
  une demi-seconde ; la course entière (1 ms → 2 s) met huit secondes ;
- l'interpolation linéaire adoucit très légèrement l'aigu aux temps courts.

C'est pour cela que le temps est déclaré en **glissement** : la transition entre
pas et la mécanique interne racontent alors la même chose au pilote. Après
`reset()`, en revanche, la tête se pose d'un coup sur le temps demandé — il n'y
a plus de queue à préserver, donc rien à glisser.

## Préallocation

L'anneau est alloué **une seule fois** dans `prepare()`, à la taille du temps
maximal (2 s, constante de compilation), et n'est **jamais** réalloué. Changer
le temps ne réserve rien : on lit plus loin ou moins loin dans le même anneau.
Le décalage stéréo peut demander jusqu'à +50 % — le temps résultant reste
plafonné à 2 s, donc l'anneau suffit toujours.

## Réinjection bornée

La réinjection est bornée à **0,95, strictement sous 1**, dans le code et pas
seulement dans l'aide. Une réinjection à 1 sur un délai est un oscillateur : la
boucle ne perd rien, l'entrée s'accumule et la ligne finit par saturer. À 1 au
réglage, la queue perd 5 % par tour et s'éteint donc toujours.

Deux pièces de plus tiennent la boucle sous 1 :

- **Bloqueur de continu fixe à 20 Hz**, non exposé. Sans lui, une composante
  continue se multiplierait par 1/(1 - 0,95) = 20 et emporterait la ligne. Son
  gain maximal (2/(1+R)) reste sous 1,002, donc la boucle reste sous 1.
- **Amortissement**, exposé : un passe-bas à un pôle n'a jamais de gain
  au-dessus de 1, il ne peut que réduire le gain de boucle.

## Queue à la désactivation (§3.3.2)

Le partage des rôles est net, et il est **entièrement du côté du socle pour le
mode** : c'est `Engine.cpp` qui décide. En *laissée mourir* (défaut des effets à
queue), le socle cesse d'alimenter la skill — elle reçoit du silence — mais
continue de mélanger sa sortie. En *coupée*, la skill reçoit l'entrée entière et
c'est le gain du traité que le socle met à zéro.

Ce qui est à la charge de la skill, et qui est vérifié : **que la ligne se tienne
correctement quand l'entrée devient silencieuse**. Elle continue alors de tourner
sur son seul contenu, perd au plus 5 % par tour, et ne remonte jamais.
`reset()` purge l'anneau, le passe-bas et le bloqueur de continu.

## Cas numériques (`selfTest`, 48 kHz)

1. **Temps** — une impulsion ressort à l'échantillon 4800 pour 100 ms demandés,
   12000 pour 250 ms, et rien avant.
2. **Réinjection** — réglée à 0,50 : les rapports entre répétitions successives
   valent 0,4999 et 0,5000.
3. **Réinjection bornée** — réglée à 1 : le rapport de boucle mesuré vaut 0,9500
   et la crête totale (0,9998) ne dépasse jamais l'impulsion d'entrée.
4. **Amortissement** — à fond, la deuxième répétition perd 19,6 dB à 8 kHz et
   0,26 dB à 200 Hz : c'est bien un passe-bas dans la boucle.
5. **Décalage stéréo** — à 1 : la gauche répète à 2400 échantillons (la moitié
   du temps), la droite à 7200 (une fois et demie).
6. **Queue** — impulsion puis silence : 19 fenêtres de 100 ms strictement
   décroissantes, dernière crête à 5,0·10⁻⁴, et silence **exact** après `reset()`.
