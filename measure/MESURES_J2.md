# Mesures J2 — 16 septembre 2026

Machine : Ryzen 7 5800XT, Windows 10, UMC404HD (ASIO), Live 12.4.6.
Binaire : Plug.vst3 Release, MSVC 19.44, JUCE 8.0.15, `PLUG_J2_TIMING=1`.
Toutes les valeurs sont des µs par appel de `processBlock` sur le passe-tout.

## Référence zéro du budget §4.4 (48 kHz / 128, bloc = 2 666,7 µs, budget 25 % = 666,7 µs)

| Contexte | Blocs | moyenne | p50 | p99 | p99,9 | max |
| --- | --- | --- | --- | --- | --- | --- |
| Live, latence 0 (`live_48k128/…172800_6794`) | 217 766 | 0,079 | 0,1 | 0,2 | **0,5** | **75,3** |
| Live, latence 256 (`live_48k128/…172800_8792`) | 195 266 | 0,404 | 0,4 | 1,0 | **1,9** | **79,1** |
| Banc hors hôte, latence 0 (`bench_J2_160926.txt`) | 200 000 | 0,067 | 0,1 | 0,1 | 0,1 | 12,7 |
| Banc hors hôte, latence 256 | 200 000 | 0,220 | 0,2 | 0,3 | 0,3 | 33,4 |

Lecture : le p99,9 du passe-tout vaut 0,02 % du bloc ; le budget de 666,7 µs
est intégralement disponible pour le DSP. Les maxima (75–79 µs, ≈ 3 % du bloc)
sont des à-coups d'ordonnancement isolés (1 bloc sur ~200 000), pas un coût
du plugin ; à surveiller quand le DSP arrivera, c'est le point A.0 de l'annexe.
Résolution du chronomètre : 100 ns (QueryPerformanceCounter à 10 MHz).

Mesure de contrôle avant réglage (Live à 44,1 kHz / 1024) :
p99,9 = 1,0 µs, max = 53,6 µs sur 27 278 blocs (`live_48k128/…170722_1909`).

## Latence et alignement dans Live (48 kHz / 128)

- Live affiche « Latency: 0 samples » pour l'instance à 0 et
  « Latency: 256 samples (5.3 ms) » pour l'instance à 256.
- Trois exports du même Set (clic de test, impulsions à 37 402 / 76 363 /
  116 883 échantillons) comparés par `scripts/wav_align.py` :
  latente active, latente en bypass (bouton Live), latente retirée →
  **écart 0 échantillon dans les trois cas**.

## Banc hors hôte (`bench_J2_160926.txt`)

Grille 284 / 284, ordre et identifiants conformes, 2 non automatisables
(master.mixLaw, master.quality), aucune collision de hash VST3.
Latence réelle = déclarée à 0 et 256 (processBlock, processBlockBypassed,
blocs irréguliers 1/7/128/500/0/64). État : aller-retour exact, 10 815 octets.

## pluginval 1.0.4, niveau 5

SUCCESS, code de retour 0, deux fois (avant et après l'ajout de l'éditeur
générique). Journaux dans `measure/raw/`.

## Live 12.4.6 — scan, chargement, automation, sauvegarde

- Scan : « Plug » apparaît sous Plug-ins › Lascaux Lab.
- Chargement sur piste audio : OK, panneau vide (voir échafaudage éditeur).
- Configure + automation dessinée sur « Slot 01 Main » (slot01.main).
- Set sauvegardé puis rouvert (`measure/raw/J2_test Project/J2_test.als`) :
  `scripts/als_inspect.py` y lit schemaVersion 1, 284 PARAM, macro1 = 0,6397,
  master.drive = 0,5368, slot01.main = 0,7778, master.quality = 1,
  et une enveloppe de 21 événements sur slot01.main. Valeurs identiques à
  l'écran après réouverture.

## Écran de démarrage JUCE

JUCE 8.0.15 ne contient plus d'écran de démarrage : `juce_gui_basics.cpp`
émet un avertissement de compilation si `JUCE_DISPLAY_SPLASH_SCREEN` est
défini (« this version of JUCE does not use the splash screen »). Rien à
désactiver, rien à afficher, sous AGPLv3 comme sous licence commerciale.
