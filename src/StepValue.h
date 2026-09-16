// StepValue — la fonction pure « valeur de ce pas pour cet état » (CdC §3.3,
// §3.6 ; contrat J3 a/b et amendements 2, 3, 4).
// Garantit : la cible d'un pas ne dépend que de l'état (graine du pas, densité
// du pas, plage et probabilité du paramètre, verrou, valeur explicite). Aucune
// horloge, aucun état caché. Un pas généré est un tirage projeté sur la plage :
// resserrer la plage garde la forme, capturer fige les nombres.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <cstdint>
#include <optional>

namespace plug
{
    enum class StepMode : int { Base = 0, Generated = 1, Explicit = 2 };

    const char* stepModeName (StepMode m) noexcept;
    StepMode stepModeFromName (const juce::String& name) noexcept;

    struct ParamSpec
    {
        float min = 0.0f, max = 1.0f;   // plage de génération
        float prob = 1.0f;              // probabilité de variation
        bool locked = false;            // amendement J3-2 : verrouillé = base(t), rien d'interne ne bouge
        bool glide = false;             // transition : saut (false) ou glissement (true)
        bool structural = false;        // §3.3.1 : jamais par pas, jamais modulé
    };

    struct StepSpec
    {
        bool on = true;
        StepMode mode = StepMode::Base;
        uint32_t seed = 0;              // graine du pas, posée par Générer
        float density = 1.0f;           // densité du tirage, stockée avec la graine (amendement J3-3)
    };

    // Mélangeur 64 bits (SplitMix) : déterministe, sans état.
    uint64_t hashMix (uint64_t x) noexcept;

    // Tirage uniforme [0,1) pour (graine de pas, paramètre, sel). 24 bits de mantisse.
    float unitDraw (uint32_t seed, int paramIndex, int salt) noexcept;

    // Graine d'un pas au tirage numéro `counter` depuis la graine maîtresse.
    uint32_t seedForDraw (uint32_t masterSeed, uint32_t counter) noexcept;

    // Plage effective d'un tirage à densité d : [min,max] rétréci autour de son centre.
    void effectiveRange (const ParamSpec& p, float density, float& lo, float& hi) noexcept;

    // La fonction pure. paramIndex : indice modulable 0..12 (GridMap::kModulable).
    // Renvoie la cible du pas, ou rien si le pas laisse la base s'appliquer.
    std::optional<float> stepTarget (const ParamSpec& p, const StepSpec& s, int paramIndex,
                                     std::optional<float> explicitValue) noexcept;

    // Même fonction, lue directement dans l'arbre d'état (message thread, tests, interface J4).
    std::optional<float> stepTargetFromState (const juce::ValueTree& plugState, int slot1, int step1,
                                              const juce::String& paramName);
}
