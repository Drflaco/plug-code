#include "ParameterGrid.h"

namespace plug::grid
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using juce::ParameterID;
    using juce::String;

    namespace
    {
        enum class Kind { Unit, Toggle, DriveRouting, MixLaw, Quality };

        struct Entry
        {
            String id;
            String name;
            Kind kind;
            float def;
            bool automatable;
        };

        String slotId (int slot, const String& suffix)
        {
            return "slot" + String (slot).paddedLeft ('0', 2) + "." + suffix;
        }

        String slotName (int slot, const String& label)
        {
            return "Slot " + String (slot).paddedLeft ('0', 2) + " " + label;
        }

        std::vector<Entry> entries()
        {
            std::vector<Entry> e;
            e.reserve ((size_t) kTotalCount);

            // Macros
            for (int m = 1; m <= kMacroCount; ++m)
                e.push_back ({ "macro" + String (m), "Macro " + String (m), Kind::Unit, 0.0f, true });

            // Master
            e.push_back ({ "master.drive",        "Master Drive",         Kind::Unit,         0.0f, true });
            e.push_back ({ "master.tone",         "Master Tone",          Kind::Unit,         0.5f, true });
            e.push_back ({ "master.comp",         "Master Comp",          Kind::Unit,         0.0f, true });
            e.push_back ({ "master.lowFreq",      "Master Low Freq",      Kind::Unit,         0.5f, true });
            e.push_back ({ "master.volume",       "Master Volume",        Kind::Unit,         0.5f, true });
            e.push_back ({ "master.mix",          "Master Mix",           Kind::Unit,         1.0f, true });
            e.push_back ({ "master.driveRouting", "Master Drive Routing", Kind::DriveRouting, 0.0f, true });
            e.push_back ({ "master.mixLaw",       "Master Mix Law",       Kind::MixLaw,       1.0f, false });
            e.push_back ({ "master.quality",      "Master Quality",       Kind::Quality,      1.0f, false });
            for (int r = 1; r <= 3; ++r)
                e.push_back ({ "master.res" + String (r), "Master Res " + String (r), Kind::Unit, 0.0f, true });

            // Séquenceur (globaux)
            e.push_back ({ "seq.length",   "Seq Length",   Kind::Unit, 0.5f, true });
            e.push_back ({ "seq.division", "Seq Division", Kind::Unit, 0.5f, true });
            e.push_back ({ "seq.swing",    "Seq Swing",    Kind::Unit, 0.0f, true });
            for (int r = 1; r <= 5; ++r)
                e.push_back ({ "seq.res" + String (r), "Seq Res " + String (r), Kind::Unit, 0.0f, true });

            // Emplacements
            for (int s = 1; s <= kSlotCount; ++s)
            {
                e.push_back ({ slotId (s, "active"), slotName (s, "Active"),  Kind::Toggle, 1.0f, true });
                e.push_back ({ slotId (s, "main"),   slotName (s, "Main"),    Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "paramA"), slotName (s, "Param A"), Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "paramB"), slotName (s, "Param B"), Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "paramC"), slotName (s, "Param C"), Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "paramD"), slotName (s, "Param D"), Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "paramE"), slotName (s, "Param E"), Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "paramF"), slotName (s, "Param F"), Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "mix"),    slotName (s, "Mix"),     Kind::Unit,   1.0f, true });
                e.push_back ({ slotId (s, "gain"),   slotName (s, "Gain"),    Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "glide"),  slotName (s, "Glide"),   Kind::Unit,   0.0f, true });
                e.push_back ({ slotId (s, "stereo"), slotName (s, "Stereo"),  Kind::Unit,   0.5f, true });
                e.push_back ({ slotId (s, "fade"),   slotName (s, "Fade"),    Kind::Unit,   0.0f, true });
                for (int r = 1; r <= 3; ++r)
                    e.push_back ({ slotId (s, "res" + String (r)), slotName (s, "Res " + String (r)), Kind::Unit, 0.0f, true });
            }

            jassert ((int) e.size() == kTotalCount);
            return e;
        }

        std::unique_ptr<juce::RangedAudioParameter> make (const Entry& en)
        {
            const ParameterID pid { en.id, kVersionHint };

            switch (en.kind)
            {
                case Kind::Unit:
                    return std::make_unique<juce::AudioParameterFloat> (pid, en.name,
                                                                        juce::NormalisableRange<float> (0.0f, 1.0f), en.def);
                case Kind::Toggle:
                    return std::make_unique<juce::AudioParameterBool> (pid, en.name, en.def >= 0.5f);
                case Kind::DriveRouting:
                    return std::make_unique<juce::AudioParameterChoice> (pid, en.name, juce::StringArray { "Pre", "Post" }, (int) en.def,
                                                                         juce::AudioParameterChoiceAttributes().withAutomatable (en.automatable));
                case Kind::MixLaw:
                    return std::make_unique<juce::AudioParameterChoice> (pid, en.name, juce::StringArray { "-6 dB", "-3 dB", "0 dB" }, (int) en.def,
                                                                         juce::AudioParameterChoiceAttributes().withAutomatable (en.automatable));
                case Kind::Quality:
                    return std::make_unique<juce::AudioParameterChoice> (pid, en.name, juce::StringArray { "Eco", "Normal", "High" }, (int) en.def,
                                                                         juce::AudioParameterChoiceAttributes().withAutomatable (en.automatable));
            }

            jassertfalse;
            return nullptr;
        }
    }

    Layout createLayout()
    {
        Layout layout;
        for (const auto& en : entries())
            layout.add (make (en));
        return layout;
    }

    std::vector<String> allIds()
    {
        std::vector<String> ids;
        for (const auto& en : entries())
            ids.push_back (en.id);
        return ids;
    }

    std::vector<String> nonAutomatableIds()
    {
        std::vector<String> ids;
        for (const auto& en : entries())
            if (! en.automatable)
                ids.push_back (en.id);
        return ids;
    }
}
