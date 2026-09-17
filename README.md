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
| Signalsmith Stretch | tag **1.1.0**, commit `44c8f865af9da8c29cc4a70a2d5a3ec83639c711`, sous-module `external/signalsmith-stretch`, **MIT** |
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

## Dépendances et licences

| Brique | Licence | Usage |
| --- | --- | --- |
| JUCE 8.0.15 | AGPLv3 (usage personnel, §5 du CdC) | framework, VST3, paramètres, état |
| Signalsmith Stretch 1.1.0 | MIT | pitch-shift de la famille granulaire / repitch |

**chowdsp_utils n'est pas utilisé** : sa licence est mixte par module (annexe A.5
du CdC) et les effets du catalogue de départ ne le demandent pas. Règle retenue :
en cas de doute sur une licence, on écrit la brique nous-mêmes. Aucune dépendance
GPL n'entre dans ce dépôt.

Écart relevé le 16/09 : l'annexe A.3 du CdC prête à Signalsmith Stretch un drapeau
`splitComputation` qui étalerait le calcul spectral. Il n'existe pas à la version
1.1.0. La régularité du coût par bloc se mesure donc, elle ne se suppose pas.

## Presets d'état (.plugstate)

Un preset porte l'état **complet** (§3.6) : identités de skill, ordre, valeurs,
motifs de pas, verrous, graines, routes — pas un sous-ensemble. Format : l'arbre
`PlugState` en XML, avec son `schemaVersion`, donc migrable comme un projet.

Les fichiers vivent dans `presets/` et sont lus depuis, par ordre de priorité :
`%APPDATA%\LascauxLab\Plug\presets` puis le dossier `presets` voisin du binaire.

## Cibles

- **Plug** — VST3. Grille de 284 paramètres figée (`src/ParameterGrid.h`) sur
  le socle J3 (`src/Engine.*`) : horloge et transport, séquenceur paramétrique,
  verrous, fondus et queues, graine et capture, modulation, macros, état v2,
  mélange local et global. Aucun effet réel, aucune interface : trois modules
  **factices** (`src/dummies/`) pour éprouver la chaîne.
- **PlugRender** — rendu hors hôte : `PlugRender test` (matrice J3, rendu
  déterministe comparé octet par octet), `PlugRender render --in --state --out`,
  `PlugRender bench` (coût du socle), `PlugRender gen-input`.
- **PlugBench** — banc J2 : grille, presets, aller-retour d'état, coût par bloc
  du processeur complet, et depuis le J4b le piège APVTS/undo (une automation qui
  joue ne doit pas entrer dans l'historique d'annulation du pilote).
- **PlugSkillTest** — le vérificateur du contrat de skill (§3.9) : identité,
  aides, latence déclarée contre latence réelle, blocs vides et irréguliers,
  silence après reset, bornes, déterminisme, coût par bloc. Une skill n'entre au
  registre qu'après l'avoir passé. `PlugSkillTest --only core.filter`.

## Socle J3 — où lire quoi

| Fichier | Sert |
| --- | --- |
| `src/StepValue.*` | la fonction pure « valeur de ce pas pour cet état » (hash, densité, plage) |
| `src/StateSchema.*` | l'état v2, migration, Générer, Figer, verrous, routes |
| `src/StateEdit.*` | déménager et vider un emplacement (§3.2) : ce qui suit l'effet, ce qui reste |
| `src/StateQuery.*` | lectures pures pour la vue : halo, valeur d'un pas, comptes, motif |
| `src/Clock.h` | pas à l'échantillon depuis la position hôte, swing, arrêt, saut, roue libre |
| `src/Engine.*` | composition (contrat J3 b), chaîne, fondus et queues, mélange, publication bornée |
| `src/Modulation.h` | enveloppe et suiveur, déclenchement transport / audio / MIDI |
| `src/Skill.h`, `SKILL_TEMPLATE.md` | le contrat d'un module d'effet et son squelette |

Le crochet J3 `j3_state.xml` a été retiré au J4a : les presets `.plugstate` le
remplacent.

## Catalogue des skills

Un effet vit dans `src/skills/<nom>/`, avec son en-tête, sa source, son
`help.fr.md`. Il s'enregistre par une ligne dans `src/skills/Skills.cpp`, tenue
par l'intégrateur : aucune skill ne s'enregistre toute seule, un catalogue qui
dépend de l'ordre d'initialisation statique n'est pas un catalogue.

Pour travailler sur une skill sans compiler les autres :
`cmake -S . -B build_x -DPLUG_SKILLS="filter;gain"`.

## Interface J4b — `src/ui/`, frontière v1/v2, règle du script

L'interface du §3.7 vit dans `src/ui/`, en deux étages qui n'ont pas la même
espérance de vie :

| Étage | Fichiers | Une v2 |
| --- | --- | --- |
| Couche de présentation | `ui/ViewTypes.h`, `ui/Presenter.*`, `ui/Format.*`, `ui/Prefs.*` | **garde** |
| Widgets | `ui/v1/*` | **remplace** |

Le `Presenter` est possédé par `PlugProcessor`, pas par l'éditeur : Live ferme et
rouvre la fenêtre sans arrêt, et l'état de session (emplacement sélectionné, pas
sélectionnés, mode A/B par ligne) doit survivre à la fenêtre, pas au projet. Il
lit l'arbre et le registre, expose des **valeurs** (`ViewTypes.h`) et reçoit des
commandes qui ouvrent chacune une transaction d'annulation nommée en français.
Il apprend qu'un état a changé par `ValueTree::Listener` + `AsyncUpdater` — une
notification `viewChanged (masque)` par tour de boucle — et la position de lecture
par un timer à 30 Hz qui lit `Engine::uiSnapshot()`. Jamais `valueCurve` : ce
tampon appartient au thread audio.

**La règle, et le script qui l'exécute** : un fichier de `src/ui/v1/` n'inclut
que `ViewTypes.h`, `Presenter.h`, `Format.h`, `Prefs.h`, des en-têtes JUCE et des
en-têtes standard. Jamais `StateSchema.h`, `Engine.h`, `PlugProcessor.h`,
`ParameterGrid.h`, `Skill.h` ni `GridMap.h`.

```
powershell -ExecutionPolicy Bypass -File scripts/check_ui_boundary.ps1
```

Il échoue si un `#include` sort de la liste. À lancer avant tout commit qui
touche à `src/ui/` : une règle qu'on peut oublier doit devenir une règle qui
s'exécute. C'est ce qui rend vraie l'exigence du pilote — « l'interface aura
plusieurs vies » — au lieu de la laisser à la discipline.

**Ce que l'interface contient** (zones du §3.7) : la barre — menu de presets,
annuler / refaire dont l'aide dit le NOM de la transaction, identité du binaire,
préférences ; les huit macros ; les onglets d'emplacements, avec le choix de
l'effet et le glisser-réordonner (échanger, insérer, copier) ; les contrôles de
l'effet, halo de génération compris ; la zone d'édition, séquenceur 16 × 32 à
gauche et inspecteur de pas à droite, poignée « ║ » entre les deux ; le master et
la sortie. `AVENIR.md`, à la racine, liste ce que la v1 ne fait pas et l'assume :
il est embarqué dans le binaire (`juce_add_binary_data`), lu par
`ui::Presenter::avenirText()` et affiché dans l'onglet « À venir » des
préférences — la liste ne peut donc pas diverger du fichier du dépôt.

**Il n'y a plus d'option d'éditeur** : `createEditor()` rend l'interface v1 sans
condition. L'échafaudage J2 — liste de curseurs générique, bandeau d'identité,
deux boutons de preset — a été retiré au J4b étape 7 (REGIME §8). Les presets se
chargent par le menu de la barre.

**Mesure du rendu** : l'option CMake `PLUG_UI_TIMING` (OFF par défaut, jamais
livrée) chronomètre `paint()` du séquenceur et écrit ses p50/p99 dans
`%APPDATA%\LascauxLab\Plug\measure\`. Les chiffres hors hôte sont dans
`measure/MESURES_J4b.md`.

**Deux UndoManager, et pourquoi.** L'APVTS recopie périodiquement dans l'arbre les
valeurs venues de l'hôte, avec l'UndoManager qu'on lui donne : un clip automatisé
qui joue écrivait donc dans l'historique du pilote, et Ctrl+Z rembobinait
l'automation au lieu de défaire son geste (mesuré : `measure/MESURES_J4b.md`).
`PlugProcessor` tient désormais `undo` (celui du pilote) et `flushUndo` (donné à
l'APVTS, qui jette tout ce qu'il reçoit). Les réglages venus de l'interface restent
annulables parce que `ui::Presenter::setParam` écrit **dans l'arbre** avec
l'UndoManager du pilote — l'APVTS relaie ensuite au paramètre, donc à l'hôte.
`PlugBench` vérifie les deux sens à chaque exécution.

## Scripts

- `scripts/build.ps1` — configuration + compilation Release.
- `scripts/validate.ps1` — pluginval, niveau 5 par défaut.
- `scripts/check_ui_boundary.ps1` — la frontière v1/v2 de l'interface, exécutable.
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
