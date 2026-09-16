# Pitch granulaire — `core.grain`

Décalage de hauteur **à durée préservée** : deux têtes de lecture tournent en
opposition de phase dans une fenêtre de grain, fondues par une fenêtre de Hann
complémentaire, pendant que l'écriture avance à vitesse normale. C'est le seul
effet **latent** du catalogue (§3.8) — et le seul du lot D à l'être.
Sert les gestes 1 (texture profonde) et 2 (granulaire de texture).

| | |
| --- | --- |
| Identité | `core.grain` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-3 dB** — le traité n'est pas en phase avec le sec (§3.7, « granulaire ») |
| Latence | **moitié du plus grand grain + 2 échantillons**, soit 2402 échantillons (50,0 ms) à 48 kHz, 2207 à 44,1 kHz, 4802 à 96 kHz |
| Entrées occupées | `main`, `paramA`, `paramB`, `stereo` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Hauteur | -24 → +24 demi-tons (0,5 = hauteur d'origine) | **verrouillé par défaut** | saut |
| `paramA` | Taille de grain | 5 ms → 100 ms, exponentielle (milieu ≈ 22 ms) | libre | glissement |
| `paramB` | Réinjection | 0 → 0,8 | libre | glissement |
| `stereo` | Écart stéréo | 0,5 = aucun écart ; 0 et 1 = 50 centièmes de demi-ton par canal, soit un demi-ton entre gauche et droite, le sens s'inversant de part et d'autre du centre | libre | glissement |

**Pourquoi la hauteur est verrouillée par défaut.** C'est le cas type du §3.3.1.
La raison retenue ici est musicale, pas technique : la hauteur est le réglage
qui éloigne le plus vite le traité de la tonalité de la source, et une valeur
tirée à chaque pas casse la cohérence harmonique que cherche le geste 1.
La raison *technique* que donne le §3.3.1 — « un saut brutal par pas impose une
resynchronisation » — **ne s'applique pas à cette implémentation** : un saut de
hauteur ne change que la vitesse des têtes, jamais leur position, et ne coûte
donc rien. Le déverrouiller est une décision musicale ; le cas de test 7 montre
que 128 paliers de hauteur d'affilée ne produisent ni marche ni pic de coût.

**Pourquoi la taille de grain est libre alors qu'elle touche à la fenêtre.**
La fenêtre est *centrée sur la latence déclarée* : les têtes s'écartent de
±taille/2 autour d'elle. La latence est donc calée sur le **plus grand** grain
possible et ne dépend pas du réglage courant. C'est ce qui permet à la taille
de moduler par pas sans jamais décaler la piste (§4.3). Le prix est payé une
fois, en latence fixe.

**Transparence à l'unité.** À 0,5, la phase reste à zéro : la tête B pèse
exactement 1 et lit exactement la latence déclarée, la tête A pèse 0. La sortie
est le sec retardé, sans peigne. Un pitch à l'unité qui colore serait un défaut.

**Ce que ça sonne.** C'est le pitch « lo-fi granulaire » que le CdC (annexe A.3)
donne comme proche du produit de référence : grain court, ça chante et ça
devient métallique ; grain long, ça s'étale et ça flange. Le coût est plat —
aucun paquet spectral, aucune FFT (annexe A.0, point 3).

## Cas numériques (`selfTest`, 48 kHz)

1. **Hauteur neutre** — une impulsion ressort intacte (1,000000000) à
   l'échantillon 2402 ; rien avant, rien après : la latence déclarée est exacte
   et la sortie est un retard pur.
2. **Hauteur** — un sinus à 500 Hz ressort à 999,4 Hz à +12 demi-tons et à
   249,9 Hz à -12, à niveau conservé (0,3535 pour 0,3536 attendus).
3. **Durée préservée** — une salve de 4800 échantillons est rendue en 5278
   (un grain de plus) ; un varispeed en rendrait 2400.
4. **Taille de grain** — le creux d'amplitude au croisement des deux têtes est
   mesuré à 1,889 (grain de 10 ms) et 1,919 (60 ms), pour 2,0 attendus ; avec
   une taille supposée fausse de 37 %, il tombe à 0,999. C'est la loi de
   conversion elle-même qui est vérifiée, pas seulement son effet.
5. **Réinjection** — énergie du second tour : 0 à réinjection nulle, 0,000057 à
   0,3 et 0,000228 à 0,6 (le carré du réglage).
6. **Écart stéréo** — 0,000000000 de différence gauche/droite au centre
   (canaux rigoureusement identiques), 1,422 à fond.
7. **Hauteur qui change à chaque pas** (cas demandé au J4a) — 128 paliers de
   hauteur à 128 échantillons, servis en blocs de 128 : crête 0,5000 pour une
   entrée à 0,5, plus grand écart entre deux échantillons 0,0575 alors que la
   pente propre du signal vaut déjà 0,06, latence 2402 inchangée.

## Coût mesuré (PlugSkillTest, 48 kHz, blocs de 128, machine §4.1)

Moyenne **3,1 à 4,0 µs** selon la charge de la machine, p99 **5,2 à 5,5 µs**,
p99.9 de 14 à 33 µs (0,5 à 1,2 % du bloc).

Le p99 est stable à ±4 % sur toutes les passes : le travail par échantillon est
le même pour tous les échantillons — deux lectures cubiques et un cosinus, pas
de FFT, pas de paquet tous les N blocs (annexe A.0, point 3). Le p99.9 et le
maximum, eux, bougent d'une passe à l'autre dans les mêmes proportions que ceux
de `core.filter` déjà validée (moyenne 1,4 µs mais p99.9 11,4 µs et max 62 µs) :
c'est le plancher de mesure de la machine, pas un à-coup de l'effet. À
recalibrer sur machine au repos au moment du budget §4.4.
