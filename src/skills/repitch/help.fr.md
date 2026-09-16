# Repitch — `core.repitch`

**Varispeed** : une seule tête de lecture à taux variable dans une ligne à
retard. La bande accélère ou ralentit, **la hauteur et la durée changent
ensemble**. C'est ce qui le sépare de `core.grain`, qui préserve la durée.
Sert le geste 3 (repitch fondu) : `repitch → écho → passe-haut`, où le fondu
agit sur le repitch lui-même, pas sur le mélange.

| | |
| --- | --- |
| Identité | `core.repitch` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-3 dB** — le traité n'est pas en phase avec le sec (§3.7) |
| Latence | **2 échantillons de garde + la longueur du fondu de recalage**, soit 194 échantillons (4,0 ms) à 48 kHz, 178 à 44,1 kHz, 386 à 96 kHz |
| Entrées occupées | `main`, `paramA`, `paramB`, `stereo` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Hauteur | -12 → +12 demi-tons (0,5 = vitesse d'origine) | **verrouillé par défaut** | **glissement** |
| `paramA` | Portée | 10 ms → 500 ms de course avant recalage, exponentielle (milieu ≈ 70 ms) | libre | glissement |
| `paramB` | Inertie | 0 → 500 ms, quadratique | libre | glissement |
| `stereo` | Écart stéréo | 0,5 = aucun écart ; 0 et 1 = 25 centièmes de demi-ton par canal | libre | glissement |

**Le recalage, et pourquoi il existe.** Une tête lue plus vite que l'écriture
rattrape le présent ; lue moins vite, elle prend un retard sans fin. Une bande
infinie n'existe pas dans un insert. La tête est donc **recalée d'une portée
par un fondu de 4 ms** dès qu'elle atteint une butée. C'est la contrepartie
assumée du varispeed : plus la portée est courte, plus le recalage revient
souvent et devient lui-même une texture (la bande bégaie) ; plus elle est
longue, plus la bande plonge loin avant de revenir.

**Ce que coûte une montée à froid.** Un recalage vers le passé fait relire la
ligne à retard. Juste après un `reset()`, elle est vide : une hauteur montante
commence donc par un silence, le temps que la tête rattrape la matière réelle
(une demi-portée). En usage, la ligne est pleine et le cas ne se voit pas.

**Pourquoi la hauteur est verrouillée par défaut, et pourquoi elle glisse.**
C'est le cas type du §3.3.1 : une hauteur tirée à chaque pas emmène le traité
hors de la tonalité de la source et fait recaler la tête sans arrêt. Mais le
geste 3 se joue justement en la déverrouillant — c'est pourquoi sa transition
par défaut est le **glissement**, pas le saut, et pourquoi l'inertie est
exposée : c'est elle qui donne le poids de moteur de bande au lieu d'une marche.

**Fondu de recalage : 4 ms, fixe, non exposé.** Il fixe la latence déclarée
(la position de repos de la tête est juste assez profonde pour que la tête
sortante du fondu ne lise jamais le futur). L'exposer en paramètre structurel
coûterait un verrou de plus pour un gain musical nul.

## Cas numériques (`selfTest`, 48 kHz)

1. **Vitesse neutre** — une impulsion ressort intacte (1,000000000) à
   l'échantillon 194 ; rien avant, rien après : la latence déclarée est exacte
   et la sortie est un retard pur.
2. **Hauteur** — un sinus à 1000 Hz ressort à 1998,1 Hz à +12 demi-tons et à
   498,7 Hz à -12, mesuré entre deux recalages.
3. **Durée non préservée** — une salve de 4800 échantillons lue une octave plus
   bas est rendue en 9598, soit le double ; `core.grain` en rendrait 4800.
4. **Portée** — sur une rampe, les recalages se comptent un à un : 9 à une
   portée de 100 ms (9 attendus) et 1 à 500 ms (1 attendu).
5. **Inertie** — 30 ms après un pas d'une octave vers le bas : 500,0 Hz sans
   inertie (arrivée immédiate), 966,7 Hz à 500 ms d'inertie (le moteur est à
   peine parti).
6. **Écart stéréo** — 0,000000000 de différence gauche/droite au centre
   (canaux rigoureusement identiques), 1,415 à fond.
7. **Hauteur qui change à chaque pas** (cas demandé au J4a) — 128 paliers de
   hauteur à 128 échantillons, servis en blocs de 128 : crête 0,5000 pour une
   entrée à 0,5, plus grand écart entre deux échantillons 0,0202 alors que la
   pente propre du signal vaut déjà 0,06, latence 194 inchangée.

## Coût mesuré (PlugSkillTest, 48 kHz, blocs de 128, machine §4.1)

Moyenne **2,9 à 3,4 µs** selon la charge de la machine, p99 **4,4 à 4,8 µs**,
p99.9 de 17 à 33 µs (0,66 à 1,25 % du bloc).

Le p99 est stable à ±5 % sur toutes les passes. Comme pour `core.grain`, le
p99.9 et le maximum varient d'une passe à l'autre comme ceux de `core.filter`
déjà validée : c'est le plancher de mesure de la machine, à recalibrer au repos
au moment du budget §4.4. Les **deux** têtes sont lues à chaque
échantillon, même hors recalage — la sortante est alors confondue avec
l'entrante et pèse zéro — pour que le coût ne dépende pas de l'endroit où
tombe un recalage (annexe A.0, point 3).
