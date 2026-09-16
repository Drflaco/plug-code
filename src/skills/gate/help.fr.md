# Gate — `core.gate`

Porte de bruit à seuil, détection liée entre les deux canaux, **latence nulle**.
L'autre moitié du geste 1 : gate et réverbe séquencés. Un gate qui claque
annule le geste, qui se joue justement sur des découpes rapides — toutes les
transitions sont donc lissées en S.

| | |
| --- | --- |
| Identité | `core.gate` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-6 dB** — le traité est le sec multiplié, donc en phase (§3.7) |
| Latence | **0 échantillon** (aucune pré-écoute, rien à déclarer) |
| Entrées occupées | `main`, `paramA`, `paramB`, `paramC`, `paramD` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Seuil | -60 dB (tout passe) → 0 dB (rien ne passe), linéaire en dB | libre | glissement |
| `paramA` | Attaque | 0,1 ms → 100 ms, exponentielle | libre | saut |
| `paramB` | Maintien | 0 → 500 ms, quadratique | libre | saut |
| `paramC` | Relâchement | 1 ms → 1000 ms, exponentielle | libre | saut |
| `paramD` | Profondeur | 0 = silence franc · 0,5 = -30 dB · 1 = la porte n'atténue plus | libre | glissement |

Les cinq sont **libres** : aucune ne touche à la latence ni à la structure, et
seuil comme temps sont exactement ce qu'on veut faire varier d'un pas à l'autre.

**Rampe en S.** Le gain suit une rampe linéaire mise en forme par `3c² - 2c³`,
dont la pente est nulle aux deux bouts : ni l'ouverture ni la fermeture ne
produisent de marche, même à l'attaque la plus courte. Le temps déclaré est le
temps réel de la transition complète, mi-course à la moitié du temps.

**Détecteur.** Suiveur de crête à attaque instantanée et relâchement **fixe de
3 ms**, non exposé : sans lui, la porte battrait à chaque passage par zéro de la
forme d'onde. Le relâchement que règle le pilote est celui du gain, pas celui du
détecteur — c'est pourquoi la fermeture commence quelques millisecondes après
que le signal est réellement passé sous le seuil.

**Maintien.** Il garde la porte ouverte après le passage sous le seuil. C'est ce
qui empêche le battement sur un signal qui frôle le seuil, et ce qui permet de
laisser passer la queue d'un son au lieu de la trancher.

**Profondeur.** À 0, la porte fermée rend un silence franc — c'est le réglage de
la découpe rythmique. Au-dessus, elle ne fait qu'atténuer : le signal reste
présent en fond, ce qui est plus musical sur une boucle entière.

## Cas numériques (`selfTest`, 48 kHz)

1. **Seuil (sous)** — un signal à -48 dB sous un seuil à -30 dB, profondeur 0 :
   sortie strictement nulle.
2. **Seuil (au-dessus)** — le même seuil, signal à -6 dB : sortie = entrée, au bit près.
3. **Attaque** — réglée à 10 ms : gain 0,50 à 5 ms, 1,00 à 10 ms.
4. **Maintien** — 100 ms après la chute du signal : fermée sans maintien,
   encore ouverte avec 200 ms.
5. **Relâchement** — 50 ms après la même chute : fini à 5 ms, encore à 98 % à 500 ms.
6. **Profondeur** — réglée à -30 dB, porte fermée : la sortie vaut l'entrée
   atténuée d'exactement 30 dB.
