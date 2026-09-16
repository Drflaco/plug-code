# Mesures J3 — 16 septembre 2026

Binaire : Plug.vst3 Release 0.3.0, MSVC 19.44, JUCE 8.0.15. Outils : PlugRender
(rendu hors hôte et matrice), PlugBench, pluginval 1.0.4, Live 12.4.6.
Rapports bruts : `measure/j3/rapport_J3.txt`, `measure/j3/*.wav`,
`measure/bench_J3_160926.txt`, `measure/raw/J3_recall Project/`.

## Condition de passage J3 (CdC §7) — cinq points

| Exigence | Preuve | Résultat |
| --- | --- | --- |
| Rendu déterministe vérifié par fichier (§3.6) | T1 : deux passes de PlugRender sur le même WAV et le même état, fichiers 32 bits comparés octet par octet ; état relu depuis XML, même fichier | identique |
| Variation générée reproductible à graine égale (§3.3) | T3 : deux états, même graine maîtresse, Générer sur 32 pas → mêmes valeurs ; graine différente → valeurs différentes ; plage resserrée → même forme ; capture → valeurs figées | identique |
| Rappel de session à l'identique dans Live (§3.6) | état de référence chargé par le crochet, Set sauvegardé par Live, `scripts/als_state_compare.py` : arbre hors PARAM identique, 0 PARAM différent sur 284, schemaVersion 2 | identique |
| Désactivation par pas sans changement de latence (§3.3.2) | T6 : latence déclarée constante = 256 sur 3 000 blocs, un pas sur deux inactif ; sec du master aligné (impulsion à 256) ; Live affiche « Latency: 256 samples » | constant |
| Resynchronisation exacte au saut de position (§3.3.3) | T5 : saut de fin de mesure 1 au début de mesure 3, comparé au rendu continu à la position cible, égalité stricte (1e-6) sur une mesure après purge des mémoires | exact |

## Matrice complète (`PlugRender test`) — 27 vérifications, toutes passées

- T2 : blocs de 128 contre blocs 1/7/128/500/0/64/300 → rendu identique (horloge à l'échantillon, lissage par échantillon).
- T4 : glide de 2 pas (amendement J3-1) → saut maximal entre deux échantillons 6e-5 (pente de la rampe), aucun saut au bord de pas.
- T7 : migration v1→v2 (Set J2 : PARAM seuls) → 16 emplacements, 32 pas, nœud inconnu conservé, aller-retour XML équivalent.
- T8 : paramètre verrouillé portant pas générés, macro, enveloppe et glide → valeur composée = base sur tout le rendu (amendement J3-2).
- T9 : lois de mélange au point milieu : −6 dB 0,5/0,5 ; −3 dB 0,7071/0,7071 ; 0 dB 1/1.
- T10 : transport arrêté (pas figé) et hôte sans position (roue libre 120 BPM) → déterministes.
- T11 : auto-tests des trois factices (gain 0,25 → 0,5 ; délai 10 échantillons ; latent 256 = déclaré).

## Coût par bloc à 48 kHz / 128 (bloc 2 666,7 µs, budget 25 % = 666,7 µs), 200 000 blocs

| Contexte (`PlugRender bench`) | moyenne | p50 | p99 | p99,9 | max |
| --- | --- | --- | --- | --- | --- |
| Socle vide, 16 emplacements sans skill | 0,25 µs | 0,2 | 0,5 | 1,1 (0,04 %) | 31,9 |
| État de référence : 3 factices, séquence, macro, enveloppe, suiveur | 26,8 µs | 24,1 | 51,2 | 80,1 (3,0 %) | 357 |

Lecture : le socle coûte ~9 µs par emplacement occupé, soit ~80 µs pour les
neuf effets du catalogue avant tout DSP (12 % du budget). La part principale
est la composition par échantillon (13 valeurs) et le mélange local avec
cos/sin par échantillon. Piste J4 : gains de mélange par tranche à pas
constant plutôt que par échantillon. Le max de 357 µs est un à-coup isolé.

PlugBench (processeur complet, état de référence chargé par le crochet) :
moyenne 23,2 µs, p99,9 46,8 µs, max 78,4 µs — cohérent.

## État

- Taille de l'état par défaut sérialisé (PARAM + schéma v2 complet) : ~61 Ko
  d'XML, ~120 Ko une fois hexadécimal dans un .als. Acceptable, à surveiller
  quand les pas explicites se multiplient (fil ouvert J2, mesuré).
- pluginval niveau 5 : SUCCESS.

## Écarts et notes

- Les factices ne remplissent pas le contrat de skill (§3.9) et le disent.
- `stereo` (entrée 9) est composé mais aucun factice ne le lit.
- La roue libre sans position hôte n'est pas signalée à l'écran (pas d'interface) ;
  `Engine::isFreeRunning()` l'expose pour J4.
