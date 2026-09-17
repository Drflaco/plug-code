// PlugEditor — l'interface v1 (J4b phase 1).
// Invariants et leur raison :
//   · ce fichier et tout src/ui/v1/ n'incluent QUE ui/ViewTypes.h, ui/Presenter.h,
//     ui/Format.h, ui/Prefs.h, les fichiers voisins de src/ui/v1/, JUCE et la
//     bibliothèque standard. Jamais StateSchema, Engine, PlugProcessor,
//     ParameterGrid, Skill ni GridMap : c'est la frontière v1/v2 qu'exige le
//     pilote (« l'interface aura plusieurs vies », 17/09), et
//     scripts/check_ui_boundary.ps1 la vérifie avant chaque commit ;
//   · l'éditeur ne possède rien du modèle : le Presenter vit dans le processeur
//     et lui survit, parce que Live ferme et rouvre la fenêtre sans arrêt ;
//   · aucune boucle modale (JUCE_MODAL_LOOPS_PERMITTED=0) : FileChooser::launchAsync,
//     PopupMenu::showMenuAsync, panneaux enfants au lieu de fenêtres modales.
// Étape 1 : la barre est réelle et porte l'identité du binaire, qui n'est donc plus
// un bandeau à part ; les cinq autres zones restent des emplacements nommés,
// remplis aux étapes 2 à 7.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugBar.h"
#include "PlugPanels.h"
#include "PlugTabs.h"
#include <memory>

namespace plug::ui::v1
{
    class PlugEditor : public juce::AudioProcessorEditor,
                       private Presenter::Listener,
                       private juce::KeyListener
    {
    public:
        PlugEditor (juce::AudioProcessor& processor, Presenter& presenter);
        ~PlugEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;   // un clic hors panneau le referme

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

        // Ctrl+Z / Ctrl+Y en MEILLEUR EFFORT : le plugin est déclaré
        // EDITOR_WANTS_KEYBOARD_FOCUS FALSE (décision figée, on n'y touche pas) et
        // Live garde le clavier la plupart du temps. On écoute quand l'hôte nous
        // laisse la main ; les boutons de la barre, eux, ne réclament aucun focus.
        bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;

        void showAbout();
        void showPrefs();
        void closePanels();
        void layOutPanel (juce::Component& panel, int w, int h);

        Presenter& presenter;

        // Le contenu porte le zoom des préférences par setTransform : la fenêtre garde
        // ses pixels réels, la mise en page garde ses 1280×800 logiques.
        juce::Component content;
        PlugBar bar;
        PlugTabs tabs;
        Zone macros { "Macros" },
             controls { "Contrôles" }, edition { "Édition" }, master { "Master + Sortie" };
        std::unique_ptr<AboutPanel> about;
        std::unique_ptr<PrefsPanel> prefs;
        std::unique_ptr<juce::TooltipWindow> tooltips;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugEditor)
    };
}
