# plug-code

Code source du multi-effet séquencé « maison » (VST3, Windows x64, Ableton Live 12.4).
Licence : **AGPLv3**, usage personnel, aucune distribution prévue (voir `LICENSE`).

La spécification vit ailleurs : pièce `plug/` du dépôt `Drflaco/Activation`
(cahier des charges, conventions, état). Ce dépôt ne porte que le code, les
scripts de construction et les mesures.

## Chaîne de construction figée

| Élément | Valeur |
| --- | --- |
| Compilateur | MSVC 2022, Visual Studio Build Tools 17.14 (charge C++ x64) |
| CMake | ≥ 3.22 (Kitware 4.4.3 par winget ; 3.31.6 livré avec les Build Tools convient aussi) |
| JUCE | tag **8.0.15**, commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`, sous-module `external/JUCE` |
| Norme C++ | C++17 |
| Validation | pluginval 1.0.4 (`tools/pluginval/`, binaire non versionné) |

`CMakeLists.txt` refuse de configurer si le sous-module JUCE n'est pas au commit
attendu. Jamais de construction depuis une branche mobile.

## Construire

```
git clone --recurse-submodules https://github.com/Drflaco/plug-code.git
cd plug-code
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
```

Sorties (Release) :
- `build/Plug_artefacts/Release/VST3/Plug.vst3` — le plugin
- `build/PlugBench_artefacts/Release/PlugBench.exe` — le banc hors hôte

## Cibles

- **Plug** — VST3. Grille de 284 paramètres figée (`src/ParameterGrid.h`) sur
  le socle J3 (`src/Engine.*`) : horloge et transport, séquenceur paramétrique,
  verrous, fondus et queues, graine et capture, modulation, macros, état v2,
  mélange local et global. Aucun effet réel, aucune interface : trois modules
  **factices** (`src/dummies/`) pour éprouver la chaîne.
- **PlugRender** — rendu hors hôte : `PlugRender test` (matrice J3, rendu
  déterministe comparé octet par octet), `PlugRender render --in --state --out`,
  `PlugRender bench` (coût du socle), `PlugRender gen-input`.
- **PlugBench** — banc J2 : grille, aller-retour d'état, coût par bloc du
  processeur complet.

## Socle J3 — où lire quoi

| Fichier | Sert |
| --- | --- |
| `src/StepValue.*` | la fonction pure « valeur de ce pas pour cet état » (hash, densité, plage) |
| `src/StateSchema.*` | l'état v2, migration, Générer, Figer, verrous, routes |
| `src/Clock.h` | pas à l'échantillon depuis la position hôte, swing, arrêt, saut, roue libre |
| `src/Engine.*` | composition (contrat J3 b), chaîne, fondus et queues, mélange, publication bornée |
| `src/Modulation.h` | enveloppe et suiveur, déclenchement transport / audio / MIDI |
| `src/Skill.h`, `SKILL_TEMPLATE.md` | le contrat d'un module d'effet et son squelette |

Crochet J3 (sans interface) : `%APPDATA%\LascauxLab\Plug\j3_state.xml`, s'il
existe, remplace l'état par défaut d'une nouvelle instance. Retiré au J4.

## Échafaudage J2 : éditeur générique

Sans fenêtre de plug-in, Live ne peut « configurer » aucun paramètre et n'en
automatise donc aucun (mesuré le 16/09/2026 : le sélecteur d'automation ne
propose que « Device On »). Le plugin J2 embarque l'éditeur générique de JUCE
(liste de curseurs standard) derrière l'option CMake `PLUG_J2_GENERIC_EDITOR`
(ON par défaut). Ce n'est pas l'interface du produit ; l'option passe à OFF
quand l'interface réelle arrive.

## Scripts

- `scripts/build.ps1` — configuration + compilation Release.
- `scripts/validate.ps1` — pluginval, niveau 5 par défaut.
- `scripts/als_inspect.py` — lit un Live Set (.als) : état JUCE des instances
  Plug, paramètres configurés côté Live, enveloppes d'automation.
- `scripts/wav_align.py` — compare la position des impulsions entre exports
  WAV (test d'alignement de latence et de bypass).

## Mesures

`measure/MESURES_J2.md` résume les mesures du jalon ; `measure/bench_J2_*.txt`
et `measure/live_48k128/` contiennent les rapports bruts retenus.
`measure/raw/` (Live Set de test, exports, journaux pluginval) reste local.

## Mesure dans l'hôte

Le plugin compilé avec `PLUG_J2_TIMING=1` chronomètre chaque bloc et écrit un
rapport à la désactivation dans
`%APPDATA%\LascauxLab\Plug\measure\j3_timing_*.txt`. Le crochet de latence de
test du J2 (`j2_latency.txt`) a disparu : la latence vient des skills.

## Identité VST3 — figée

Nom `Plug`, fabricant `Lascaux Lab`, codes `Lscx` / `Plug`. Live mémorise cette
identité dans ses projets : elle ne change plus.
