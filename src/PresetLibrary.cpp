#include "PresetLibrary.h"
#include "StateSchema.h"

namespace plug
{
    namespace
    {
        constexpr const char* kExtension = ".plugstate";

        juce::File appDataRoot()
        {
            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("LascauxLab").getChildFile ("Plug");
        }
    }

    juce::File PresetLibrary::userDir() { return appDataRoot().getChildFile ("presets"); }

    juce::File PresetLibrary::bundledDir()
    {
        // À côté du binaire chargé : pour un VST3, le dossier du module ; en
        // développement, le dépôt lui-même via le dossier courant.
        auto here = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (int up = 0; up < 6 && here.exists(); ++up)
        {
            auto candidate = here.getChildFile ("presets");
            if (candidate.isDirectory()) return candidate;
            here = here.getParentDirectory();
        }
        return juce::File::getCurrentWorkingDirectory().getChildFile ("presets");
    }

    void PresetLibrary::rescan()
    {
        entries.clear();
        juce::StringArray seen;   // un nom ne paraît qu'une fois : l'utilisateur l'emporte

        for (const auto& dir : { userDir(), bundledDir() })
        {
            if (! dir.isDirectory()) continue;
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension))
            {
                const auto name = f.getFileNameWithoutExtension();
                if (seen.contains (name)) continue;
                seen.add (name);
                entries.push_back ({ name, f });
            }
        }

        std::sort (entries.begin(), entries.end(),
                   [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
    }

    juce::String PresetLibrary::name (int index) const
    {
        return juce::isPositiveAndBelow (index, (int) entries.size()) ? entries[(size_t) index].name : juce::String();
    }

    juce::File PresetLibrary::file (int index) const
    {
        return juce::isPositiveAndBelow (index, (int) entries.size()) ? entries[(size_t) index].file : juce::File();
    }

    juce::ValueTree PresetLibrary::load (int index) const
    {
        if (! juce::isPositiveAndBelow (index, (int) entries.size())) return {};
        auto xml = juce::XmlDocument::parse (entries[(size_t) index].file);
        if (xml == nullptr) return {};
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.hasType (state::id::PlugState)) return {};
        state::ensureSchema (tree);   // un preset d'une version antérieure se migre comme un projet
        return tree;
    }

    bool PresetLibrary::write (const juce::File& f, const juce::ValueTree& plugState)
    {
        if (! plugState.hasType (state::id::PlugState)) return false;
        f.getParentDirectory().createDirectory();
        auto xml = plugState.createXml();
        return xml != nullptr && xml->writeTo (f);
    }
}
