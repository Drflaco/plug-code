// PresetLibrary — les presets d'état livrés avec le plugin (CdC §3.6, décision
// pilote J4a : un preset est l'état COMPLET, pas un sous-ensemble).
// Garantit : un fichier .plugstate = un arbre PlugState versionné (identités de
// skill, ordre, valeurs, motifs, verrous, graines) ; la bibliothèque est lue au
// démarrage et exposée à l'hôte comme liste de programmes ; charger un preset
// remplace l'état entier. C'est le seul moyen, sans interface (J4b), de dire
// quel effet occupe quel emplacement — et il reste après l'interface.
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace plug
{
    class PresetLibrary
    {
    public:
        struct Entry { juce::String name; juce::File file; };

        // Dossiers fouillés, dans l'ordre : celui de l'utilisateur d'abord, pour
        // qu'un preset modifié l'emporte sur celui livré.
        static juce::File userDir();     // %APPDATA%/LascauxLab/Plug/presets
        static juce::File bundledDir();  // presets/ à côté du binaire ou du dépôt

        void rescan();
        int size() const noexcept { return (int) entries.size(); }
        juce::String name (int index) const;
        // Arbre PlugState complet, ou arbre invalide si le fichier est illisible.
        juce::ValueTree load (int index) const;

        static bool write (const juce::File& f, const juce::ValueTree& plugState);

    private:
        std::vector<Entry> entries;
    };
}
