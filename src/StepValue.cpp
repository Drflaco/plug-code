#include "StepValue.h"
#include "StateSchema.h"
#include "GridMap.h"

namespace plug
{
    const char* stepModeName (StepMode m) noexcept
    {
        switch (m) { case StepMode::Generated: return "generated"; case StepMode::Explicit: return "explicit"; default: return "base"; }
    }

    StepMode stepModeFromName (const juce::String& name) noexcept
    {
        if (name == "generated") return StepMode::Generated;
        if (name == "explicit")  return StepMode::Explicit;
        return StepMode::Base;
    }

    uint64_t hashMix (uint64_t x) noexcept
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    float unitDraw (uint32_t seed, int paramIndex, int salt) noexcept
    {
        const uint64_t h = hashMix ((uint64_t) seed | ((uint64_t) (uint32_t) paramIndex << 32) ^ ((uint64_t) (uint32_t) salt << 48));
        return (float) (h >> 40) * (1.0f / 16777216.0f);   // 24 bits → [0,1), exact en float
    }

    uint32_t seedForDraw (uint32_t masterSeed, uint32_t counter) noexcept
    {
        return (uint32_t) hashMix (((uint64_t) masterSeed << 32) | counter);
    }

    void effectiveRange (const ParamSpec& p, float density, float& lo, float& hi) noexcept
    {
        // Autour du centre de la plage, pas de la base : la valeur reste une fonction de l'état seul (J3-3).
        const float d = juce::jlimit (0.0f, 1.0f, density);
        const float c = 0.5f * (p.min + p.max);
        lo = c - d * (c - p.min);
        hi = c + d * (p.max - c);
    }

    std::optional<float> stepTarget (const ParamSpec& p, const StepSpec& s, int paramIndex,
                                     std::optional<float> explicitValue) noexcept
    {
        if (p.structural)                      // §3.3.1 : jamais par pas
            return std::nullopt;

        switch (s.mode)
        {
            case StepMode::Base:
                return std::nullopt;

            case StepMode::Explicit:
                return explicitValue;

            case StepMode::Generated:
            {
                // J3-6 : une valeur POSÉE (V) prime sur le tirage — c'est ce que le verrou
                // matérialise pour protéger ce qui jouait. Verrouillé sans V : la base, jamais
                // un tirage, sinon la prochaine graine le ferait bouger.
                if (explicitValue) return explicitValue;
                if (p.locked) return std::nullopt;

                const float gate = unitDraw (s.seed, paramIndex, 1);
                if (gate > p.prob * juce::jlimit (0.0f, 1.0f, s.density))
                    return std::nullopt;                            // ce paramètre reste à la base sur ce pas
                float lo, hi;
                effectiveRange (p, s.density, lo, hi);
                const float u = unitDraw (s.seed, paramIndex, 0);
                return juce::jlimit (0.0f, 1.0f, lo + u * (hi - lo));
            }
        }
        return std::nullopt;
    }

    std::optional<float> stepTargetFromState (const juce::ValueTree& plugState, int slot1, int step1,
                                              const juce::String& paramName)
    {
        const int m = grid::modulableIndex (paramName);
        if (m < 0) return std::nullopt;

        const auto slot = state::slot (plugState, slot1);
        if (! slot.isValid()) return std::nullopt;

        const auto p = state::readParamSpec (slot, paramName);
        const auto st = state::step (slot, step1);
        const auto s = state::readStepSpec (st);
        return stepTarget (p, s, m, state::readExplicit (st, paramName));
    }
}
