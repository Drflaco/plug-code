#include "StateSchema.h"
#include "GridMap.h"
#include "Skill.h"
#include <algorithm>

namespace plug::state
{
    using juce::ValueTree;
    using juce::String;

    namespace
    {
        ValueTree child (ValueTree& parent, const juce::Identifier& type, Undo um)
        {
            auto c = parent.getChildWithName (type);
            if (! c.isValid()) { c = ValueTree (type); parent.addChild (c, -1, um); }
            return c;
        }

        ValueTree childWith (ValueTree& parent, const juce::Identifier& type, const juce::Identifier& key, const juce::var& v, Undo um)
        {
            auto c = parent.getChildWithProperty (key, v);
            if (! c.isValid() || ! c.hasType (type))
            {
                c = ValueTree (type); c.setProperty (key, v, um); parent.addChild (c, -1, um);
            }
            return c;
        }

        void setDefault (ValueTree& t, const juce::Identifier& key, const juce::var& v, Undo um)
        {
            if (! t.hasProperty (key)) t.setProperty (key, v, um);
        }

        // Complète un emplacement sans rien enlever.
        void ensureSlot (ValueTree& slotTree, Undo um)
        {
            setDefault (slotTree, id::skill, "", um);
            setDefault (slotTree, id::skillVersion, 0, um);
            setDefault (slotTree, id::tail, "ring", um);
            for (int m = 0; m < grid::kModulableCount; ++m)
            {
                auto p = childWith (slotTree, id::Param, id::name, String (grid::kModulableName[(size_t) m]), um);
                setDefault (p, id::min, 0.0, um); setDefault (p, id::max, 1.0, um); setDefault (p, id::prob, 1.0, um);
                setDefault (p, id::locked, false, um); setDefault (p, id::transition, "step", um);
            }
            auto line = child (slotTree, id::Line, um);
            for (int s = 1; s <= kSteps; ++s)
            {
                auto st = childWith (line, id::Step, id::i, s, um);
                setDefault (st, id::on, true, um); setDefault (st, id::mode, "base", um);
                setDefault (st, id::seed, 0, um); setDefault (st, id::density, 1.0, um);
            }
            auto mod = child (slotTree, id::Mod, um);
            auto env = child (mod, id::Env1, um);
            setDefault (env, id::rate, 0.5, um); setDefault (env, id::sync, true, um);
            setDefault (env, id::loop, true, um); setDefault (env, id::trigger, "transport", um);
            auto s2 = child (mod, id::Source2, um);
            setDefault (s2, id::kind, "env", um); setDefault (s2, id::rate, 0.5, um); setDefault (s2, id::sync, true, um);
            setDefault (s2, id::loop, true, um); setDefault (s2, id::trigger, "transport", um);
            setDefault (s2, id::attack, 0.01, um); setDefault (s2, id::release, 0.1, um);
        }
    }

    //==========================================================================
    ValueTree createDefault()
    {
        ValueTree s (id::PlugState);
        ensureSchema (s);
        return s;
    }

    void ensureParams (ValueTree& s)
    {
        const auto ids = grid::allIds();
        for (int i = 0; i < (int) ids.size(); ++i)
            if (! s.getChildWithProperty (id::paramId, ids[(size_t) i]).isValid())
            {
                ValueTree p (id::PARAM);
                p.setProperty (id::paramId, ids[(size_t) i], nullptr);
                p.setProperty (id::value, (double) grid::entryDefault (i), nullptr);
                s.addChild (p, -1, nullptr);
            }
    }

    void ensureSchema (ValueTree& s)
    {
        Undo um = nullptr;   // la migration n'est pas une édition annulable
        s.setProperty (id::schemaVersion, kSchemaVersion, um);
        setDefault (s, id::pluginVersion, "0.3.0", um);

        auto gen = child (s, id::Generation, um);
        setDefault (gen, id::masterSeed, 20260916, um);
        setDefault (gen, id::counter, 0, um);
        setDefault (gen, id::density, 0.5, um);

        auto slots = child (s, id::Slots, um);
        for (int i = 1; i <= kSlots; ++i)
        {
            auto sl = childWith (slots, id::Slot, id::index, i, um);
            ensureSlot (sl, um);
        }

        auto macros = child (s, id::Macros, um);
        for (int m = 1; m <= grid::kMacroCount; ++m)
            childWith (macros, id::Macro, id::index, m, um);
    }

    //==========================================================================
    ValueTree slot (const ValueTree& s, int slot1)
    {
        return s.getChildWithName (id::Slots).getChildWithProperty (id::index, slot1);
    }

    ValueTree step (const ValueTree& slotTree, int step1)
    {
        return slotTree.getChildWithName (id::Line).getChildWithProperty (id::i, step1);
    }

    ValueTree param (const ValueTree& slotTree, const String& name)
    {
        return slotTree.getChildWithProperty (id::name, name);
    }

    ParamSpec readParamSpec (const ValueTree& slotTree, const String& name)
    {
        ParamSpec p;
        auto t = param (slotTree, name);
        if (! t.isValid()) return p;
        p.min = (float) (double) t.getProperty (id::min, 0.0);
        p.max = (float) (double) t.getProperty (id::max, 1.0);
        p.prob = (float) (double) t.getProperty (id::prob, 1.0);
        p.locked = (bool) t.getProperty (id::locked, false);
        p.glide = t.getProperty (id::transition, "step").toString() == "glide";

        if (auto* info = SkillRegistry::instance().info (slotTree.getProperty (id::skill).toString()))
            for (const auto& d : info->params)
                if (d.lockClass == LockClass::Structural && name == juce::String (grid::kModulableName[(size_t) d.modulable]))
                    p.structural = true;
        return p;
    }

    StepSpec readStepSpec (const ValueTree& st)
    {
        StepSpec s;
        if (! st.isValid()) return s;
        s.on = (bool) st.getProperty (id::on, true);
        s.mode = stepModeFromName (st.getProperty (id::mode, "base").toString());
        s.seed = (uint32_t) (juce::int64) st.getProperty (id::seed, 0);
        s.density = (float) (double) st.getProperty (id::density, 1.0);
        return s;
    }

    std::optional<float> readExplicit (const ValueTree& st, const String& name)
    {
        auto v = st.getChildWithProperty (id::name, name);
        if (! v.isValid()) return std::nullopt;
        return (float) (double) v.getProperty (id::value, 0.0);
    }

    float readParam (const ValueTree& s, const String& gridId)
    {
        auto p = s.getChildWithProperty (id::paramId, gridId);
        return p.isValid() ? (float) (double) p.getProperty (id::value) : grid::entryDefault (grid::indexOf (gridId));
    }

    //==========================================================================
    void setParam (ValueTree& s, const String& gridId, float raw, Undo um)
    {
        auto p = childWith (s, id::PARAM, id::paramId, gridId, um);
        p.setProperty (id::value, (double) raw, um);
    }

    void setSkill (ValueTree& s, int slot1, const String& skillId, Undo um)
    {
        auto sl = slot (s, slot1);
        sl.setProperty (id::skill, skillId, um);
        if (auto* info = SkillRegistry::instance().info (skillId))
        {
            sl.setProperty (id::skillVersion, info->version, um);
            // J3-5 : la classe déclarée est le verrou initial ; la transition déclarée, la transition initiale.
            for (const auto& d : info->params)
            {
                auto p = param (sl, grid::kModulableName[(size_t) d.modulable]);
                p.setProperty (id::locked, d.lockClass == LockClass::LockedByDefault, um);
                p.setProperty (id::transition, d.glideByDefault ? "glide" : "step", um);
            }
        }
    }

    void setTail (ValueTree& s, int slot1, bool ring, Undo um) { slot (s, slot1).setProperty (id::tail, ring ? "ring" : "cut", um); }

    void setRange (ValueTree& s, int slot1, const String& name, float min, float max, Undo um)
    {
        auto p = param (slot (s, slot1), name);
        p.setProperty (id::min, (double) juce::jmin (min, max), um);
        p.setProperty (id::max, (double) juce::jmax (min, max), um);
    }

    void setProb (ValueTree& s, int slot1, const String& name, float prob, Undo um)      { param (slot (s, slot1), name).setProperty (id::prob, (double) juce::jlimit (0.0f, 1.0f, prob), um); }
    // J3-6 : matérialiser ce qu'un paramètre JOUE sur ses pas générés — un V par pas où
    // le tirage donne une valeur — pour que le verrou protège ce qui s'entendait, et que
    // la prochaine graine ne le change pas. Ne touche ni les pas de base ni les V existants.
    static void materialise (ValueTree& sl, const String& name, Undo um)
    {
        const int m = grid::modulableIndex (name);
        if (m < 0) return;
        auto p = readParamSpec (sl, name);
        if (p.structural) return;
        p.locked = false;                                       // le tirage tel qu'il joue AVANT le verrou
        for (int i = 1; i <= kSteps; ++i)
        {
            auto st = step (sl, i);
            const auto spec = readStepSpec (st);
            if (spec.mode != StepMode::Generated || readExplicit (st, name)) continue;
            if (auto v = stepTarget (p, spec, m, std::nullopt))
                childWith (st, id::V, id::name, name, um).setProperty (id::value, (double) *v, um);
        }
    }

    void setLocked (ValueTree& s, int slot1, const String& name, bool locked, Undo um)
    {
        auto sl = slot (s, slot1);
        if (locked && ! readParamSpec (sl, name).locked) materialise (sl, name, um);   // J3-6 : le verrou fige ce qui joue
        param (sl, name).setProperty (id::locked, locked, um);
    }
    void setTransition (ValueTree& s, int slot1, const String& name, bool glide, Undo um){ param (slot (s, slot1), name).setProperty (id::transition, glide ? "glide" : "step", um); }
    void setStepOn (ValueTree& s, int slot1, int step1, bool on, Undo um)                { step (slot (s, slot1), step1).setProperty (id::on, on, um); }
    void setStepMode (ValueTree& s, int slot1, int step1, StepMode mode, Undo um)        { step (slot (s, slot1), step1).setProperty (id::mode, stepModeName (mode), um); }

    void setExplicit (ValueTree& s, int slot1, int step1, const String& name, float value, Undo um)
    {
        auto st = step (slot (s, slot1), step1);
        auto v = childWith (st, id::V, id::name, name, um);
        v.setProperty (id::value, (double) value, um);
    }

    void setMasterSeed (ValueTree& s, uint32_t masterSeed, Undo um)
    {
        auto gen = s.getChildWithName (id::Generation);
        gen.setProperty (id::masterSeed, (juce::int64) masterSeed, um);
        gen.setProperty (id::counter, 0, um);
    }

    void generate (ValueTree& s, int slot1, int firstStep1, int lastStep1, float density, Undo um)
    {
        auto gen = s.getChildWithName (id::Generation);
        const auto master = (uint32_t) (juce::int64) gen.getProperty (id::masterSeed);
        auto counter = (uint32_t) (juce::int64) gen.getProperty (id::counter);
        auto sl = slot (s, slot1);

        // J3-6 : la génération ne touche JAMAIS un paramètre verrouillé (§3.3.1). Ce qu'il
        // joue est matérialisé avant le nouveau tirage, et ses V survivent au nettoyage.
        std::vector<String> lockedNames;
        for (int m = 0; m < grid::kModulableCount; ++m)
        {
            const String name (grid::kModulableName[(size_t) m]);
            const auto p = readParamSpec (sl, name);
            if (p.locked && ! p.structural) { lockedNames.push_back (name); materialise (sl, name, um); }
        }

        for (int i = juce::jmax (1, firstStep1); i <= juce::jmin (kSteps, lastStep1); ++i)
        {
            auto st = step (sl, i);
            st.setProperty (id::seed, (juce::int64) seedForDraw (master, counter++), um);
            st.setProperty (id::density, (double) juce::jlimit (0.0f, 1.0f, density), um);
            st.setProperty (id::mode, stepModeName (StepMode::Generated), um);
            // Une génération remplace d'anciennes valeurs explicites : le pas repart du tirage —
            // sauf pour les paramètres verrouillés, dont la valeur posée reste.
            for (int c = st.getNumChildren(); --c >= 0;)
            {
                auto v = st.getChild (c);
                if (! v.hasType (id::V)) continue;
                const auto name = v.getProperty (id::name, "").toString();
                if (std::find (lockedNames.begin(), lockedNames.end(), name) != lockedNames.end()) continue;
                st.removeChild (c, um);
            }
        }
        gen.setProperty (id::counter, (juce::int64) counter, um);
        gen.setProperty (id::density, (double) density, um);
    }

    void capture (ValueTree& s, int slot1, int firstStep1, int lastStep1, Undo um)
    {
        auto sl = slot (s, slot1);
        for (int i = juce::jmax (1, firstStep1); i <= juce::jmin (kSteps, lastStep1); ++i)
        {
            auto st = step (sl, i);
            if (readStepSpec (st).mode != StepMode::Generated) continue;
            for (int m = 0; m < grid::kModulableCount; ++m)
            {
                const String name (grid::kModulableName[(size_t) m]);
                if (auto v = stepTarget (readParamSpec (sl, name), readStepSpec (st), m, std::nullopt))
                {
                    auto node = childWith (st, id::V, id::name, name, um);
                    node.setProperty (id::value, (double) *v, um);
                }
            }
            st.setProperty (id::mode, stepModeName (StepMode::Explicit), um);
        }
    }

    //==========================================================================
    void setEnvelope (ValueTree& s, int slot1, const std::vector<std::pair<float, float>>& points,
                      float rate, bool sync, bool loop, const String& trigger, Undo um)
    {
        auto env = slot (s, slot1).getChildWithName (id::Mod).getChildWithName (id::Env1);
        env.removeAllChildren (um);
        for (const auto& pt : points)
        {
            ValueTree p (id::Point);
            p.setProperty (id::t, (double) pt.first, um); p.setProperty (id::v, (double) pt.second, um); p.setProperty (id::curve, 0.0, um);
            env.addChild (p, -1, um);
        }
        env.setProperty (id::rate, (double) rate, um); env.setProperty (id::sync, sync, um);
        env.setProperty (id::loop, loop, um); env.setProperty (id::trigger, trigger, um);
    }

    void setSource2 (ValueTree& s, int slot1, const String& kind, float attack, float release, Undo um)
    {
        auto s2 = slot (s, slot1).getChildWithName (id::Mod).getChildWithName (id::Source2);
        s2.setProperty (id::kind, kind, um); s2.setProperty (id::attack, (double) attack, um); s2.setProperty (id::release, (double) release, um);
    }

    void addModRoute (ValueTree& s, int slot1, const String& src, const String& dstParam, float depth, Undo um)
    {
        auto mod = slot (s, slot1).getChildWithName (id::Mod);
        ValueTree r (id::Route);
        r.setProperty (id::src, src, um); r.setProperty (id::dst, dstParam, um); r.setProperty (id::depth, (double) depth, um);
        mod.addChild (r, -1, um);
    }

    void clearModRoutes (ValueTree& s, int slot1, Undo um)
    {
        auto mod = slot (s, slot1).getChildWithName (id::Mod);
        for (int c = mod.getNumChildren(); --c >= 0;)
            if (mod.getChild (c).hasType (id::Route)) mod.removeChild (c, um);
    }

    void addMacroRoute (ValueTree& s, int macro1, int slot1, const String& dstParam, float lo, float hi, float curve, Undo um)
    {
        auto macro = s.getChildWithName (id::Macros).getChildWithProperty (id::index, macro1);
        ValueTree r (id::Route);
        r.setProperty (id::slot, slot1, um); r.setProperty (id::param, dstParam, um);
        r.setProperty (id::lo, (double) lo, um); r.setProperty (id::hi, (double) hi, um); r.setProperty (id::curve, (double) curve, um);
        macro.addChild (r, -1, um);
    }
}
