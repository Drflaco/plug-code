// StateQuery — les quatre lectures dont la vue a besoin et que personne ne
// calculait encore (J4b c-5 à c-8, ETAT Rév. 9).
// Invariants et leur raison :
//   · fonctions PURES de l'arbre : aucune horloge, aucun état caché, donc deux
//     appels sur le même état donnent le même résultat (§3.6, REGIME §4) ;
//   · aucune lecture de Engine::valueCurve : ce tampon appartient au thread
//     audio. Le halo et la valeur d'un pas se déduisent de l'état seul, jamais
//     de la valeur composée vivante ;
//   · elles enveloppent readParamSpec / effectiveRange / stepTargetFromState
//     plutôt que de refaire leur arithmétique : la fonction pure du pas reste
//     à un seul endroit (contrat J3 a).
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "StepValue.h"

namespace plug::StateQuery
{
    // c-5 — le halo d'un knob : la plage de génération et son rétrécissement par
    // la densité. effLo/effHi valent min/max quand la densité vaut 1.
    struct Halo { float min = 0.0f, max = 1.0f, effLo = 0.0f, effHi = 1.0f; };
    Halo haloRange (const juce::ValueTree& s, int slot1, const juce::String& paramName, float density);

    // c-6 — la valeur affichée d'un pas : sa cible si le pas en a une, sinon la
    // base PARAM de l'emplacement (le pas laisse passer la base).
    float stepDisplayValue (const juce::ValueTree& s, int slot1, int step1, const juce::String& paramName);

    // c-7 — de quoi est faite une sélection de pas : active « Figer » et nourrit
    // l'avertissement de re-dérivation (d-2). `off` compte les pas inactifs de la
    // sélection, quel que soit leur mode : un pas éteint garde son mode.
    struct Counts { int generated = 0; int explicitCount = 0; int off = 0; };
    Counts stepCounts (const juce::ValueTree& s, int slot1, int first1, int last1);

    // c-8 — la ligne porte-t-elle un motif que le pilote perdrait de vue ?
    // Vrai dès qu'un pas est inactif, ou en mode généré ou figé, ou porte un V.
    // Nourrit l'avertissement de glisser (d-1).
    bool lineHasPattern (const juce::ValueTree& s, int slot1);
}
