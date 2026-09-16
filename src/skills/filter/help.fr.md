# Filtre — `core.filter`

Filtre résonant à variable d'état, deux pôles, **latence nulle**. Un seul filtre
par canal : le type choisit où l'on prend la sortie, pas quel filtre tourne.
Sert le geste 3 (repitch fondu) et, plus largement, tout balayage séquencé.

| | |
| --- | --- |
| Identité | `core.filter` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-6 dB** — le traité reste en phase avec le sec (§3.7) |
| Latence | **0 échantillon** |
| Entrées occupées | `main`, `paramA`, `paramB`, `stereo` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Coupure | 20 Hz → 20 kHz, exponentielle (milieu ≈ 630 Hz) | libre | glissement |
| `paramA` | Résonance | Q 0,5 → 12 (jusqu'à ≈ +21 dB de pointe) | libre | glissement |
| `paramB` | Type | 3 paliers : 0–0,33 passe-bas · 0,34–0,66 passe-bande · 0,67–1 passe-haut | verrouillé par défaut | saut |
| `stereo` | Décalage stéréo | 0,5 = aucun écart ; 0 et 1 = une demi-octave par canal, soit une octave entre gauche et droite, le sens s'inversant de part et d'autre du centre | libre | glissement |

**Pourquoi le type est verrouillé par défaut.** Passer d'un palier à l'autre
déplace la prise de sortie d'un tap à l'autre du même filtre : l'état ne bouge
pas, mais la valeur rendue saute. Sur un pas, ça s'entend comme une marche.
Déverrouille-le si c'est justement l'effet cherché — rien ne l'interdit, ni la
latence ni la structure.

**Passe-bande normalisé.** La sortie passe-bande est ramenée à un gain crête
unitaire : monter la résonance resserre la bande sans faire monter le niveau.
Les passe-bas et passe-haut, eux, gardent leur pointe naturelle (gain = Q à la
coupure) — c'est ce qu'on attend d'un filtre qu'on balaie.

## Cas numériques (`selfTest`, 48 kHz)

1. **Coupure** — passe-bas à 200 Hz : un sinus à 6 kHz est atténué de 60 dB (≥ 40 exigés).
2. **Coupure** — le même passe-bas laisse un sinus à 50 Hz à 0,05 dB.
3. **Type** — passe-haut à 200 Hz : 50 Hz à -24 dB, 6 kHz à 0 dB.
4. **Type** — passe-bande à 1 kHz : -0,01 dB à la coupure, -24,8 dB à 8 kHz.
5. **Résonance** — 27,6 dB d'écart à la coupure entre Q 0,5 et Q 12 (≥ 15 exigés).
6. **Décalage stéréo** — à 1, la droite ouvre 9,3 dB au-dessus de la gauche sur un sinus à 1414 Hz.
