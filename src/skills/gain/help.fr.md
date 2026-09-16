# Gain / découpe — `core.gain`

Un gain, et les deux temps qui le rendent jouable au pas. **Latence nulle.**
C'est la moitié du geste 1 : le séquenceur commande le gain, la découpe naît
du rythme des pas, pas d'un LFO.

| | |
| --- | --- |
| Identité | `core.gain` (figée, jamais réutilisée) |
| Version | 1 |
| Loi de mélange | **-6 dB** — le traité est le sec multiplié, donc en phase (§3.7) |
| Latence | **0 échantillon** |
| Entrées occupées | `main`, `paramA`, `paramB` |

## Paramètres

| Entrée | Nom | Unité et plage | Verrou (§3.3.1) | Transition |
| --- | --- | --- | --- | --- |
| `main` | Gain | 0 = silence · 0,5 = unité · 1 = +6 dB (loi linéaire de la grille) | libre | glissement |
| `paramA` | Montée | 0 → 50 ms, course quadratique (temps des 99 %) | libre | saut |
| `paramB` | Descente | 0 → 200 ms, course quadratique (temps des 99 %) | libre | saut |

**Ce qui rend la découpe utilisable.** Un pas qui commande un saut de gain
produit un clic : la marche est instantanée, le haut-parleur la suit. Les deux
temps sont donc **séparés** — une découpe se veut franche à l'attaque et douce
au relâchement, pas symétrique. Quelques millisecondes de montée suffisent à
retirer le clic sans émousser le rythme ; la descente se règle à l'oreille,
selon que la découpe doit trancher ou respirer.

À 0, un temps est un saut net : c'est la découpe franche voulue, celle du gate
rythmique (§3.3.2). Le glide d'emplacement et le fondu d'activation restent des
réglages du socle : ils agissent en amont, sur la valeur du pas ; ces deux temps
agissent sur le gain lui-même, après composition.

**Première pose.** Après un `reset()`, le gain colle à sa cible au premier
échantillon au lieu de monter depuis zéro : un transport qui démarre ne doit pas
faire entendre un fondu d'entrée que personne n'a demandé.

## Cas numériques (`selfTest`, 48 kHz)

1. **Gain** — 0 / 0,25 / 1 donnent exactement les facteurs 0 / 0,5 / 2, en saut
   net quand les deux temps sont à zéro.
2. **Montée** — réglée à 5 ms : 0,900 à 2,5 ms, 0,990 à 5 ms (la loi des 99 %).
3. **Descente** — réglée à 20 ms : 0,100 à 10 ms, 0,010 à 20 ms ; la montée, elle,
   reste franche — les deux temps sont bien distincts.
4. **Anti-clic** — pendant cette descente, la plus grande marche entre deux
   échantillons vaut 0,0048.
