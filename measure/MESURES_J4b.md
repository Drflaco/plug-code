# Mesures — J4b (interface)

Ce fichier ne raconte rien : il porte des chiffres et la manière de les refaire.
Le rendu audio n'entre pas ici — il est jugé par `PlugRender test` T13, octet par
octet, contre `measure/j4a/ref/`.

## En un coup d'œil — phase 1 close

| Ce qui a été mesuré | Avant | Après | Où |
| --- | --- | --- | --- |
| Automation qui joue → historique d'annulation | **1 action** polluante, Ctrl+Z rembobinait l'automation | **0 action**, Ctrl+Z défait le geste du pilote | PlugBench §5 |
| Rendu de l'éditeur ENTIER, une frame | — | p50 **2,6 ms**, p99 **3,1 ms** (19 % d'une frame à 60 Hz) | PlugBench §7 |
| Un événement de glisser dans le séquenceur | **2673 µs** | **79,5 µs** (facteur 33) | PlugBench §7 |
| Préférences dans l'état sauvegardé | — | **0 octet** : 60 280 avant, 60 280 après | PlugBench §8 |
| Libellés accentués des skills | « DÃ©calage stÃ©rÃ©o » | **15 points de code**, intact | PlugBench §6 |
| Emplacements actifs sur l'état par défaut | alternance vue à l'écran | **16/16**, modèle prouvé juste | PlugBench §6 |
| Matrice de rendu audio | 36 cas | **71 cas**, T13 vert aux deux taux | PlugRender test |

Toutes ces mesures sont **permanentes** : le banc échoue si l'une régresse.

---

## Undo et automation (étape 1)

**La question**, posée par la phase 0 (ETAT Rév. 9, a) « Piège connu ») : l'APVTS
recopie périodiquement dans l'arbre les valeurs venues de l'hôte, **avec
l'UndoManager qu'on lui a donné**. Si c'est celui du pilote, un clip automatisé qui
joue écrit dans son historique. Ctrl+Z ne défait alors plus son geste : il rembobine
l'automation.

**Comment c'est mesuré** — `PlugBench`, section 5, permanent : le banc échoue si le
piège revient.

1. un geste du pilote passe par le Presenter (`setTail`) : une transaction nommée ;
2. 32 valeurs arrivent sur `slot03.main` par `setValueNotifyingHost`, hors de tout
   geste d'interface et hors de toute commande, en 8 salves ;
3. le recopiage est forcé à chaque salve par `apvts.copyState()`, qui appelle la
   même `flushParameterValuesToValueTree()` que le timer de l'APVTS.
   `MessageManager::runDispatchLoopUntil` est compilé hors du binaire par
   `JUCE_MODAL_LOOPS_PERMITTED=0` : on ne relâche pas une contrainte du produit
   pour une mesure, et l'appel direct rend l'épreuve déterministe plutôt que
   dépendante d'un minutage ;
4. on lit `getNumActionsInCurrentTransaction()`, `getUndoDescription()`, puis on
   appelle `undo()` et on regarde ce qui a bougé.

### Avant la parade — le piège est réel

| Grandeur | Valeur |
| --- | --- |
| Transaction du pilote | « Couper la queue · emplacement 2 », 1 action |
| Actions ajoutées par l'automation | **1** |
| Transaction à annuler après l'automation | « Couper la queue · emplacement 2 » |
| Après Ctrl+Z : geste du pilote rétabli | oui |
| Après Ctrl+Z : automation défaite | **OUI** — `slot03.main` 0,820 → 0,500 |

Une seule action pour 32 valeurs et 8 recopiages : `ValueTree` **fusionne** les
écritures successives d'une même propriété dans une transaction
(`createCoalescedAction`). Le compte reste donc petit, et c'est ce qui rend le
défaut sournois — il ne se voit pas dans la taille de l'historique, il se voit
quand Ctrl+Z rembobine un paramètre que le pilote n'a jamais touché.

### La parade retenue — deux UndoManager

Le pilote avait accepté d'avance « le Presenter ferme la transaction hors geste et
les flushs hors geste n'entrent pas dans une transaction utilisateur
(`UndoManager::setEnabled(false)`, ou équivalent mesuré) ». **`UndoManager::setEnabled`
n'existe pas dans JUCE 8.0.15**, et `perform()` n'est pas virtuel : impossible de
désactiver l'objet. L'équivalent retenu, plus simple et plus sûr :

- `PlugProcessor` tient **deux** gestionnaires. `undo` est celui du pilote ;
  `flushUndo { 0, 0 }` est donné à l'APVTS et jette tout ce qu'il reçoit
  (0 unité gardée, donc mémoire bornée).
- Les valeurs réglées **par l'interface** ne passent plus par
  `setValueNotifyingHost` : `ui::Presenter::setParam` écrit la valeur **dans
  l'arbre** avec l'UndoManager du pilote. L'APVTS écoute son propre arbre, relaie
  au paramètre, donc à l'hôte — et le recopiage suivant ne trouve plus rien à
  écrire.
- Conséquence à ne pas oublier : `apvts.replaceState()` ne vide plus l'historique
  du pilote (il vide celui qu'il tient). Les trois chemins de remplacement d'état
  (`setStateInformation`, `loadPresetFile`, `setCurrentProgram`) appellent
  désormais `undo.clearUndoHistory()` explicitement.

### Après la parade

| Grandeur | Valeur |
| --- | --- |
| Actions ajoutées par l'automation | **0** |
| Après Ctrl+Z : geste du pilote rétabli | oui |
| Après Ctrl+Z : automation défaite | **non** — `slot03.main` reste à 0,820 |
| Réglage d'interface `slot04.main` | 0,500 → 0,800, paramètre exposé à l'hôte = 0,800 |
| Après Ctrl+Z sur ce réglage | 0,500 — **annulable**, la parade ne coûte pas l'undo du knob |

L'automation atteint toujours l'arbre : la vue la voit, le moteur l'entend. Seul
l'historique d'annulation est épargné.

### Ce qui reste au pilote

La confirmation dans l'hôte : un clip automatisé qui joue, puis Ctrl+Z, et vérifier
que c'est bien le dernier geste d'interface qui se défait. Le banc prouve le
mécanisme ; Live prouve le cas réel.

---

## Rendu du séquenceur (étape 4)

**La règle du pilote** : aucune allocation par frame. La géométrie des 16 × 32
cases vit dans des tableaux redimensionnés dans `resized()` ; les étiquettes de
ligne, les libellés de paramètre et les textes de l'inspecteur sont composés à la
notification ; `paint()` ne fabrique ni `juce::String` ni `Path` dynamique. La tête
de lecture ne repeint que les **deux colonnes** qui changent (`repaint (rect)`).

**Comment c'est mesuré** — `PlugBench`, section 7, permanente. On ne peut pas faire
tourner l'éditeur à 60 Hz sans hôte : on force donc **600 `paint()`** (après 100
tours de chauffe) sur une `Image` ARGB 1280×800, ce qui exécute exactement le même
code de dessin, sans le compositeur de Windows. L'état mesuré n'est pas une grille
vide : `core.fm` en 1, `core.delay` en 2, 32 pas générés sur la ligne 1, et cette
ligne en **mode B** — celui qui dessine une barre par pas.

| Grandeur | Éditeur ENTIER, une frame |
| --- | --- |
| moyenne | **2166 µs** |
| p50 | **2147 µs** |
| p99 | **2654 µs** |
| max | 3007 µs |

Une frame à 60 Hz vaut 16 700 µs : le p99 en occupe **16 %**, et c'est le cas
défavorable — on redessine *toute* l'interface à chaque tour, là où l'affichage réel
ne repeint que ce qui a changé. Le banc échoue si le p99 dépasse 8 ms.

**Le chiffre est un plancher, pas une promesse d'affichage** : il ne contient ni le
compositeur, ni la carte graphique, ni l'ordonnancement de Live. L'option CMake
`PLUG_UI_TIMING` (OFF par défaut, **jamais dans le binaire livré**) ajoute au
séquenceur un compteur qui écrit ses p50/p99 dans
`%APPDATA%\LascauxLab\Plug\measure\` : c'est par là que la mesure dans l'hôte se
fera, et elle reste au pilote.

### Balayage de sélection — un défaut trouvé par la mesure (étape 5)

Le glisser du séquenceur rappelait `refresh()` à **chaque événement de souris**, et
`refresh()` relit les dix lignes : dix `slotView` (130 `ParamView`, chacune avec son
halo, son texte et sa raison de verrou) plus dix `lineView`. Mesuré sur 100
événements, après 100 tours de chauffe :

| Chemin | moyenne | p99 |
| --- | --- | --- |
| Avant — relecture complète à chaque événement | **2673 µs** | 3524 µs |
| Après — chemin léger | **79,5 µs** | 114 µs |

**Ce qui a changé** : déplacer la sélection n'est pas un changement d'état.
`Presenter::selectSteps` ne marque plus que `Session` (et non `Params`), le
séquenceur a un `refreshSelection()` qui relit trois entiers et repeint au lieu de
relire vingt Views, et l'éditeur choisit le chemin d'après le masque. Seul
l'inspecteur relit vraiment — il montre le pas sélectionné, il n'a pas le choix.

**Facteur 33**, et rien n'a été « optimisé » avant d'avoir le chiffre (REGIME §2).
Le banc garde les DEUX mesures : si le chemin léger repasse au-dessus de 0,7 ms,
c'est que quelqu'un l'a rebranché sur la relecture complète.
