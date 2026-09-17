// PlugEditor — l'interface v1 (J4b phase 1, étape 0 : la coquille).
// Invariants et leur raison :
//   · ce fichier et tout src/ui/v1/ n'incluent QUE ui/ViewTypes.h, ui/Presenter.h,
//     ui/Format.h, ui/Prefs.h, JUCE et la bibliothèque standard. Jamais StateSchema,
//     Engine, PlugProcessor, ParameterGrid, Skill ni GridMap : c'est la frontière
//     v1/v2 qu'exige le pilote (« l'interface aura plusieurs vies », 17/09), et
//     scripts/check_ui_boundary.ps1 la vérifie avant chaque commit ;
//   · l'éditeur ne possède rien : le Presenter vit dans le processeur et lui survit,
//     parce que Live ferme et rouvre la fenêtre sans arrêt ;
//   · aucune boucle modale (JUCE_MODAL_LOOPS_PERMITTED=0) : FileChooser::launchAsync.
// Étape 0 : six zones vides nommées, le bandeau d'identité du binaire et les deux
// boutons de preset repris de l'échafaudage — le pilote garde le chargement à
// chaque étape. Tout le reste arrive aux étapes 1 à 7.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include <memory>

namespace plug::ui::v1
{
    class PlugEditor : public juce::AudioProcessorEditor,
                       private Presenter::Listener
    {
    public:
        PlugEditor (juce::AudioProcessor& processor, Presenter& presenter);
        ~PlugEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int kBaseWidth = 1280;
        static constexpr int kBaseHeight = 800;

    private:
        // Une zone vide, nommée : elle dit ce qui viendra, et où.
        class Zone : public juce::Component
        {
        public:
            explicit Zone (const juce::String& name);
            void paint (juce::Graphics&) override;
        private:
            juce::String title;
        };

        void viewChanged (const ViewMask& mask) override;
        void transportChanged (const TransportView&) override;
        void chooseToLoad();
        void chooseToSave();

        Presenter& presenter;

        // Le contenu porte le zoom des préférences par setTransform : la fenêtre garde
        // ses pixels réels, la mise en page garde ses 1280×800 logiques.
        juce::Component content;
        juce::Label stamp, status;
        juce::TextButton load { "Charger un preset..." }, save { "Enregistrer sous..." };
        Zone bar { "Barre" }, macros { "Macros" }, tabs { "Onglets" },
             controls { "Contrôles" }, edition { "Édition" }, master { "Master + Sortie" };
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::TooltipWindow> tooltips;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugEditor)
    };
}
