// StateEdit — les deux éditions d'emplacement que le §3.2 demande et que le
// schéma d'état ne portait pas encore (J4b c-3/c-4, ETAT Rév. 9).
// Invariants et leur raison :
//   · SUIT l'effet : `skill`, `skillVersion` et les 13 bases PARAM. RESTE sur
//     l'emplacement : `active`, `glide`, `fade`, `tail`, plages, probabilités,
//     transitions, la Line, le Mod, les routes macro — l'ordre de la chaîne EST
//     l'indice de l'emplacement (§3.2), une automation ne déménage pas ;
//   · les VERROUS se re-posent depuis la déclaration de la skill qui ARRIVE
//     (amendement J3-5, décision pilote Q1 du 17/09) : un verrou dit quelque
//     chose du sens d'un paramètre, et « paramB verrouillé parce que c'est le
//     rapport FM » est un mensonge quand un délai prend la place ;
//   · composé UNIQUEMENT des fonctions publiques de StateSchema : ce fichier
//     n'a aucune connaissance privée du schéma, et le schéma ne bouge pas.
// Tout passe par l'UndoManager fourni (§3.6) : un appel = une action annulable.
#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace plug::StateEdit
{
    // Swap   : les deux emplacements échangent leur effet.
    // Insert : l'effet se déplace et les autres se décalent en gardant leur ordre
    //          relatif — c'est le geste de glisser un onglet entre deux autres (§3.2).
    // Copy   : la cible reçoit une copie, l'original ne bouge pas (Alt enfoncé).
    enum class Mode { Swap, Insert, Copy };

    // slot1 / from1 / to1 : numérotation humaine 1..16. Hors bornes ou égaux : sans effet.
    void moveSlot (juce::ValueTree& s, int from1, int to1, Mode mode, juce::UndoManager* um);

    // Vide un emplacement : `skill` devient "". Tout le reste est GARDÉ — le schéma
    // v2 se complète, il ne purge jamais (§3.9) : une skill qui revient retrouve
    // ses plages, sa ligne et ses valeurs.
    void clearSlot (juce::ValueTree& s, int slot1, juce::UndoManager* um);
}
