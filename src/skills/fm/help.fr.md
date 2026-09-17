# FM — `core.fm`

Modulation de fréquence **du signal entrant**, pas un synthé : le son qui arrive
est la porteuse. On module le point de lecture d'une ligne à retard qui ne
contient que l'entrée — moduler un retard, c'est moduler une phase. Pas
d'entrée, pas de son. **Latence nulle.** Première moitié du geste 1 (FM → pitch
→ gate → réverbe sur une loop).

| | |
| --- | --- |
| Identité | `core.fm` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-3 dB** — le traité est décalé dans le temps, donc hors phase avec le sec (§3.7) |
| Latence | **0 échantillon** (le retard part de zéro, l'interpolation ne lit que du passé) |
| Entrées occupées | `main`, `paramA`, `paramB`, `paramC`, `stereo` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition | Affichage |
| --- | --- | --- | --- | --- | --- |
| `main` | Profondeur | 0 → 2 ms d'excursion de retard, quadratique (≈ 6,3 rad d'index à 1 kHz) | libre | glissement | `0,500 ms` |
| `paramA` | Fréquence | 0,5 Hz → 2 kHz, exponentielle (milieu ≈ 32 Hz) | libre | glissement | `31,6 Hz` |
| `paramB` | Rapport | 11 paliers : 1/4, 1/3, 1/2, 1, 2, 3, 4, 5, 6, 7, 8 (palier le plus proche de la valeur × 10) | verrouillé par défaut | saut | `×2` (rapport) |
| `paramC` | Auto-modulation | 0 → 2 ms d'excursion, quadratique | libre | glissement | `0,500 ms` |
| `stereo` | Décalage stéréo | 0,5 = modulateurs en phase ; 0 et 1 = une demi-période d'écart de part et d'autre | libre | glissement | `0,00 %` |

> **Affichage (J4b c-2).** La colonne donne ce que l'interface écrit sous le
> contrôle, à la valeur brute 0,5 — la skill déclare `unit` et `display`, et
> l'interface ne convertit rien elle-même. Sans déclaration, le repli est la
> valeur brute `0,00`–`1,00`.


**L'index dépend de la fréquence traitée.** Moduler un retard, ce n'est pas
moduler une phase à index constant : pour une composante à `f`, l'index vaut
`π · f · D` où `D` est l'excursion. À 2 ms, une composante à 1 kHz reçoit
6,3 radians, une à 100 Hz en reçoit 0,63. C'est le comportement d'une vraie FM
sur matière audio, et c'est aussi pourquoi l'aigu part le premier.

**Les deux garde-fous.** Le CdC §2 pose le risque du geste 1 : « la FM produit
facilement des partiels sans rapport avec la tonalité de la source ». Ce module
en borne deux choses, et deux seulement :

1. **L'index est borné** à 2 ms d'excursion. Au-delà, on ne fabrique plus une
   texture mais un autre son.
2. **Le rapport est accroché** à des multiples et sous-multiples simples de la
   fréquence de base : les partiels engendrés tombent alors sur une même série
   harmonique au lieu de se disperser.

**Ce qui n'est PAS ici, et reste à trancher.** Le rapport accroche le modulateur
à la fréquence de base, **pas à la hauteur de la source**. Le mécanisme
d'accroche à une gamme évoqué au CdC §2 n'est pas dans ce module : il demande un
suivi de hauteur, qui est un autre problème et un autre coût. Tant qu'il
n'existe pas, la cohérence harmonique se règle à l'oreille avec `paramA`.

**Pourquoi le rapport est verrouillé par défaut.** Le faire sauter d'un pas à
l'autre est exactement ce qui disperse les partiels et casse la cohérence que le
geste 1 cherche. Ce n'est pas un coût technique — le saut est propre, la phase
du modulateur est continue — c'est un garde-fou musical. Déverrouille-le si
c'est l'effet voulu.

**L'auto-modulation ne peut pas diverger.** Le signal module son propre retard,
mais la ligne ne reçoit **jamais** la sortie : elle ne contient que l'entrée.
L'auto-modulation déplace le point de lecture, elle n'ajoute aucun gain, donc la
sortie reste bornée par l'entrée quoi qu'il arrive. Une réinjection audio, elle,
aurait pu diverger.

**Interpolation linéaire, assumée.** Lire deux échantillons passés et les
interpoler linéairement est ce qui permet au retard de descendre exactement à
zéro, donc à la latence d'être nulle et à la profondeur nulle de rendre l'entrée
**au bit près**. Un interpolateur d'ordre supérieur demanderait un échantillon
d'avance, donc un échantillon de latence.

## Cas numériques (`selfTest`, 48 kHz)

1. **Profondeur** — à 0 (auto-modulation à 0) : sortie = entrée, écart maximal
   exactement 0 sur 4096 échantillons de bruit, deux canaux.
2. **Profondeur** — excursion réglée pour un index de 1 radian à 1 kHz : la
   porteuse vaut 0,38209 (0,5 × J₀(1) = 0,38260 attendu) et le premier partiel
   0,57514 fois la porteuse (J₁(1)/J₀(1) = 0,57508 attendu). Les valeurs de
   Bessel vérifient la loi de profondeur sans passer par l'oreille.
3. **Fréquence** — modulateur à 300 Hz : raie à 1300 Hz = 0,21974, à 1700 Hz
   = 0,00003. À 700 Hz : 1700 Hz = 0,21974, 1300 Hz = 0,00001.
4. **Rapport** — base 350 Hz : la raie à 1350 Hz vaut 0,21974 au palier ×1
   contre 0,00000 au palier ×2 ; celle à 2400 Hz vaut 0,05735 au palier ×2
   (rang 2) contre 0,00126 au palier ×1 (rang 4, J₄(1) attendu = 0,00124).
5. **Auto-modulation** — profondeur nulle, donc elle seule module : harmonique 3
   relative de 0,000000004 à 0 (la sortie est le sinus pur) et 0,21576 à 0,6.
6. **Décalage stéréo** — écart gauche/droite exactement 0 à 0,5 (modulateurs en
   phase), 0,84277 à 0.
