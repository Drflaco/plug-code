#include "StateQuery.h"
#include "GridMap.h"
#include "StateSchema.h"

namespace plug::StateQuery
{
    using juce::String;
    using juce::ValueTree;

    Halo haloRange (const ValueTree& s, int slot1, const String& paramName, float density)
    {
        Halo h;
        auto sl = state::slot (s, slot1);
        if (! sl.isValid()) return h;

        const auto spec = state::readParamSpec (sl, paramName);
        h.min = spec.min;
        h.max = spec.max;
        effectiveRange (spec, juce::jlimit (0.0f, 1.0f, density), h.effLo, h.effHi);
        return h;
    }

    float stepDisplayValue (const ValueTree& s, int slot1, int step1, const String& paramName)
    {
        // La base PARAM est la valeur de repli : un pas en mode base, ou un paramètre
        // verrouillé, laisse passer la base — c'est exactement ce que le knob doit dire.
        const float base = state::readParam (s, "slot" + String (slot1).paddedLeft ('0', 2) + "." + paramName);
        if (auto target = stepTargetFromState (s, slot1, step1, paramName))
            return *target;
        return base;
    }

    Counts stepCounts (const ValueTree& s, int slot1, int first1, int last1)
    {
        Counts c;
        auto sl = state::slot (s, slot1);
        if (! sl.isValid()) return c;

        const int from = juce::jmax (1, juce::jmin (first1, last1));
        const int to   = juce::jmin (state::kSteps, juce::jmax (first1, last1));
        for (int i = from; i <= to; ++i)
        {
            const auto spec = state::readStepSpec (state::step (sl, i));
            if (! spec.on) ++c.off;
            if (spec.mode == StepMode::Generated) ++c.generated;
            else if (spec.mode == StepMode::Explicit) ++c.explicitCount;
        }
        return c;
    }

    bool lineHasPattern (const ValueTree& s, int slot1)
    {
        auto sl = state::slot (s, slot1);
        if (! sl.isValid()) return false;

        for (int i = 1; i <= state::kSteps; ++i)
        {
            auto st = state::step (sl, i);
            if (! st.isValid()) continue;
            const auto spec = state::readStepSpec (st);
            if (! spec.on || spec.mode != StepMode::Base) return true;
            for (const auto& child : st)
                if (child.hasType (state::id::V)) return true;
        }
        return false;
    }
}
