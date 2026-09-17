#include "StateEdit.h"
#include "GridMap.h"
#include "Skill.h"
#include "StateSchema.h"
#include <array>
#include <vector>

namespace plug::StateEdit
{
    using juce::String;
    using juce::ValueTree;

    namespace
    {
        // La charge qui déménage : l'identité de l'effet et ses 13 bases. Rien d'autre.
        struct Payload
        {
            String skill;
            int skillVersion = 0;
            std::array<float, (size_t) grid::kModulableCount> base {};
        };

        String gridId (int slot1, int modulable)
        {
            return "slot" + String (slot1).paddedLeft ('0', 2) + "." + grid::kModulableName[(size_t) modulable];
        }

        Payload read (const ValueTree& s, int slot1)
        {
            Payload p;
            auto sl = state::slot (s, slot1);
            p.skill = sl.getProperty (state::id::skill, "").toString();
            p.skillVersion = (int) sl.getProperty (state::id::skillVersion, 0);
            for (int m = 0; m < grid::kModulableCount; ++m)
                p.base[(size_t) m] = state::readParam (s, gridId (slot1, m));
            return p;
        }

        void write (ValueTree& s, int slot1, const Payload& p, juce::UndoManager* um)
        {
            // La version enregistrée est restaurée APRÈS la pose : elle suit l'effet,
            // même plus ancienne que celle du registre — un preset qui référence une
            // skill v1 la garde en v1 après un déménagement.
            setSkill (s, slot1, p.skill, um);
            state::slot (s, slot1).setProperty (state::id::skillVersion, p.skillVersion, um);
            for (int m = 0; m < grid::kModulableCount; ++m)
                state::setParam (s, gridId (slot1, m), p.base[(size_t) m], um);
        }
    }

    //==========================================================================
    void setSkill (ValueTree& s, int slot1, const String& skillId, juce::UndoManager* um)
    {
        if (! juce::isPositiveAndNotGreaterThan (slot1, state::kSlots)) return;

        // 1. StateSchema pose l'identité, la version du registre, et rend aux entrées
        //    DÉCLARÉES leur classe de verrou et leur transition (amendement J3-5).
        state::setSkill (s, slot1, skillId, um);

        // 2. Ce que StateSchema ne fait pas, et qui manquait : les entrées que la skill
        //    arrivante NE déclare PAS sont libérées. Sans ce passage, un verrou posé pour
        //    l'effet précédent survivrait à son départ et parlerait d'un paramètre qui
        //    n'existe plus (décision pilote du 17/09, ETAT Rév. 9 Q1).
        const auto* info = SkillRegistry::instance().info (skillId);
        for (int m = 0; m < grid::kModulableCount; ++m)
        {
            bool declared = false;
            if (info != nullptr)
                for (const auto& d : info->params)
                    if (d.modulable == m) { declared = true; break; }

            if (! declared)
                state::setLocked (s, slot1, grid::kModulableName[(size_t) m], false, um);
        }
    }

    //==========================================================================
    void moveSlot (ValueTree& s, int from1, int to1, Mode mode, juce::UndoManager* um)
    {
        if (! juce::isPositiveAndNotGreaterThan (from1, state::kSlots)) return;
        if (! juce::isPositiveAndNotGreaterThan (to1, state::kSlots)) return;
        if (from1 == to1) return;

        const auto source = read (s, from1);

        switch (mode)
        {
            case Mode::Copy:
                write (s, to1, source, um);
                break;

            case Mode::Swap:
            {
                const auto target = read (s, to1);
                write (s, to1, source, um);
                write (s, from1, target, um);
                break;
            }

            case Mode::Insert:
            {
                // L'effet va en to1 ; ceux qui sont entre les deux reculent d'un cran
                // vers from1 en gardant leur ordre relatif. Aucun autre emplacement
                // n'est touché : la chaîne se referme sur elle-même.
                const int step = to1 > from1 ? 1 : -1;
                for (int i = from1; i != to1; i += step)
                {
                    const auto next = read (s, i + step);
                    write (s, i, next, um);
                }
                write (s, to1, source, um);
                break;
            }
        }
    }

    void clearSlot (ValueTree& s, int slot1, juce::UndoManager* um)
    {
        if (! juce::isPositiveAndNotGreaterThan (slot1, state::kSlots)) return;
        auto sl = state::slot (s, slot1);
        if (! sl.isValid()) return;
        sl.setProperty (state::id::skill, "", um);
    }
}
