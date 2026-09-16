// plug-code — grille de paramètres exposée à l'hôte.
// FIGÉE le 16/09/2026 (validation pilote, J2). 284 entrées.
// Identifiants stables, jamais des indices. Toute modification casse les
// automations des projets Live existants : on n'y revient pas.
//
// Composition :
//   macro1..macro8                                   8   continu 0..1
//   master.{drive,tone,comp,lowFreq,volume,mix}      6   continu 0..1
//   master.driveRouting                              1   choix 2 (Pre/Post), automatisable
//   master.mixLaw                                    1   choix 3 (-6/-3/0 dB), NON automatisable (structurel §3.3.1)
//   master.quality                                   1   choix 3 (Eco/Normal/High), NON automatisable (structurel, change la latence)
//   master.res1..res3                                3   continu 0..1
//   seq.{length,division,swing}                      3   continu 0..1
//   seq.res1..res5                                   5   continu 0..1
//   slotNN.active (NN = 01..16)                     16   interrupteur (bypass doux : latence inchangée)
//   slotNN.main                                     16   continu 0..1
//   slotNN.paramA..paramF                           96   continu 0..1
//   slotNN.{mix,gain,glide,stereo,fade}             80   continu 0..1
//   slotNN.res1..res3                               48   continu 0..1
//                                                  ---
//                                                  284
// L'enveloppe VST3 de JUCE ajoute en plus un paramètre « Bypass » ('byps')
// imposé par la norme VST3 ; il n'appartient pas à la grille.
// Les valeurs par défaut ne font PAS partie du contrat (elles n'affectent que
// les nouvelles instances).

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace plug::grid
{
    constexpr int kSlotCount        = 16;
    constexpr int kEntriesPerSlot   = 16;
    constexpr int kMacroCount       = 8;
    constexpr int kMasterCount      = 12;
    constexpr int kSeqCount         = 8;
    constexpr int kTotalCount       = kSlotCount * kEntriesPerSlot + kMacroCount + kMasterCount + kSeqCount; // 284
    constexpr int kVersionHint      = 1; // JUCE ParameterID version hint — ne change jamais.

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Liste ordonnée des identifiants, telle que déclarée à l'hôte.
    std::vector<juce::String> allIds();

    // Identifiants déclarés non automatisables à l'hôte.
    std::vector<juce::String> nonAutomatableIds();
}
