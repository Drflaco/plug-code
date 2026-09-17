# Mesures — J4b (interface)

Ce fichier ne raconte rien : il porte des chiffres et la manière de les refaire.
Le rendu audio n'entre pas ici — il est jugé par `PlugRender test` T13, octet par
octet, contre `measure/j4a/ref/`.

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

## Rendu du séquenceur

À venir : compteur `PLUG_UI_TIMING` (Debug seulement), p50/p99 de `paint()` du
séquenceur à 60 Hz. Étape 2.
