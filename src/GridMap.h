// GridMap — la lecture interne de la grille figée (CdC §3.9, ETAT Rév. 2).
// Garantit : les 284 entrées ont un indice fixe (ordre de déclaration), les 13
// entrées modulables par pas d'un emplacement sont nommées ici et nulle part
// ailleurs, et chaque entrée continue 0..1 a UNE fonction de conversion.
// Sert §3.3 (longueur, division, swing, glide, fondu) et §3.7 (loi de mélange).
#pragma once
#include "ParameterGrid.h"
#include <array>
#include <cmath>

namespace plug::grid
{
    // Indices de base dans l'ordre de déclaration (voir ParameterGrid.cpp).
    constexpr int kMacroBase  = 0;    // macro1..8
    constexpr int kMasterBase = 8;    // drive, tone, comp, lowFreq, volume, mix, driveRouting, mixLaw, quality, res1..3
    constexpr int kSeqBase    = 20;   // length, division, swing, res1..5
    constexpr int kSlotBase   = 28;   // slot01.active … slot16.res3
    constexpr int kPerSlot    = 16;
    constexpr int kSteps      = 32;

    enum Master { MDrive = 0, MTone, MComp, MLowFreq, MVolume, MMix, MDriveRouting, MMixLaw, MQuality, MRes1, MRes2, MRes3 };
    enum Seq    { SLength = 0, SDivision, SSwing };
    enum Slot   { Active = 0, Main, ParamA, ParamB, ParamC, ParamD, ParamE, ParamF, Mix, Gain, Glide, Stereo, Fade, Res1, Res2, Res3 };

    // Les 13 entrées d'un emplacement qu'un pas peut porter (§3.3). active, glide et
    // fade sont des réglages d'emplacement, jamais des valeurs de pas.
    constexpr int kModulableCount = 13;
    constexpr std::array<int, kModulableCount> kModulable { Main, ParamA, ParamB, ParamC, ParamD, ParamE, ParamF, Mix, Gain, Stereo, Res1, Res2, Res3 };
    constexpr std::array<const char*, kModulableCount> kModulableName { "main", "paramA", "paramB", "paramC", "paramD", "paramE", "paramF", "mix", "gain", "stereo", "res1", "res2", "res3" };

    inline int slotIndex (int slot0, int entry) noexcept { return kSlotBase + slot0 * kPerSlot + entry; }
    inline int modulableIndex (const juce::String& name) noexcept
    {
        for (int m = 0; m < kModulableCount; ++m)
            if (name == kModulableName[(size_t) m]) return m;
        return -1;
    }

    // Indice d'un identifiant dans la grille, -1 s'il n'existe pas.
    int indexOf (const juce::String& id);
    float entryDefault (int index);

    //==========================================================================
    // Conversions des entrées continues 0..1 (mapping interne, ETAT Rév. 2 :
    // les types hôte restent continus, le sens vit ici).

    inline int seqLength (float v) noexcept { return 2 + (int) std::lround (juce::jlimit (0.0f, 1.0f, v) * 30.0f); }   // 2..32 pas

    // 15 divisions : 1/4, 1/4T, 1/4D, 1/8, 1/8T, 1/8D, 1/16, … 1/64D — en temps (noire = 1).
    constexpr int kDivisionCount = 15;
    inline int divisionIndex (float v) noexcept { return (int) std::lround (juce::jlimit (0.0f, 1.0f, v) * (kDivisionCount - 1)); }
    inline float divisionValueFor (int index) noexcept { return (float) index / (kDivisionCount - 1); }
    inline double divisionBeats (int index) noexcept
    {
        static constexpr double base[5] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };
        static constexpr double kind[3] = { 1.0, 2.0 / 3.0, 1.5 };
        return base[index / 3] * kind[index % 3];
    }

    // Swing : le second pas de chaque paire recule de v × (durée de pas / 3) ; v = 1 est le triolet.
    inline double swingOffsetBeats (float v, double stepBeats) noexcept { return juce::jlimit (0.0f, 1.0f, v) * stepBeats / 3.0; }

    // Glide : 0..4 pas ; 0,5 = 2 pas (cas de test de l'amendement J3-1).
    inline double glideSteps (float v) noexcept { return 4.0 * juce::jlimit (0.0f, 1.0f, v); }

    // Fondu d'activation : 0..500 ms, quadratique pour de la finesse près de zéro.
    inline double fadeSeconds (float v) noexcept { const double c = juce::jlimit (0.0f, 1.0f, v); return 0.5 * c * c; }

    // Amortissement (phase 3, 20/09) : rampe minimale entre deux pas, 0..250 ms, quadratique
    // — 0,2 = 10 ms (le clic disparaît), 0,5 = 63 ms, 1 = 250 ms (un fondu musical).
    // Hors grille : c'est l'attribut `damp` du Slot, pas un PARAM.
    inline double dampSeconds (float v) noexcept { const double c = juce::jlimit (0.0f, 1.0f, v); return 0.25 * c * c; }

    // Gain et volume : 0,5 = unité, 1 = +6 dB, 0 = silence.
    inline float gainLinear (float v) noexcept { return 2.0f * juce::jlimit (0.0f, 1.0f, v); }
}

namespace plug
{
    enum class MixLaw { Minus6 = 0, Minus3 = 1, Zero = 2 };

    // Loi de mélange (§3.7) : gains sec et traité pour un mix m ∈ [0,1].
    inline void mixGains (MixLaw law, float m, float& dry, float& wet) noexcept
    {
        m = juce::jlimit (0.0f, 1.0f, m);
        switch (law)
        {
            case MixLaw::Minus6: dry = 1.0f - m; wet = m; break;                                  // linéaire, signaux en phase
            case MixLaw::Minus3:
                // Puissance constante. Les deux bornes sont posées à la main : cos(π/2)
                // ne vaut pas zéro en virgule flottante mais −4,4e−8, et ce résidu suffit
                // à empêcher le plugin d'être transparent quand on ne lui demande rien
                // (relevé par T14 le 17/09 : 0,980000019 ressortait à 0,979999959).
                if (m <= 0.0f)      { dry = 1.0f; wet = 0.0f; }
                else if (m >= 1.0f) { dry = 0.0f; wet = 1.0f; }
                else                { dry = std::cos (m * juce::MathConstants<float>::halfPi);
                                      wet = std::sin (m * juce::MathConstants<float>::halfPi); }
                break;
            case MixLaw::Zero:   dry = juce::jmin (1.0f, 2.0f * (1.0f - m)); wet = juce::jmin (1.0f, 2.0f * m); break;
        }
    }

    inline MixLaw mixLawFromIndex (float rawChoice) noexcept { return (MixLaw) juce::jlimit (0, 2, (int) std::lround (rawChoice)); }
}
