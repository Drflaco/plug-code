// PlugBar — la barre du haut, zone 1 du §3.7 (J4b étape 1).
// Invariants et leur raison :
//   · elle ne connaît que le Presenter et les ViewTypes : la frontière v1/v2 du
//     §5, vérifiée par scripts/check_ui_boundary.ps1 ;
//   · elle PORTE l'identité du binaire en permanence, et remplace le bandeau
//     d'échafaudage de l'étape 0. Une soirée entière a été perdue le 17/09 sur
//     un module de la veille : l'identité ne se cache pas dans un menu ;
//   · le pilote doit retrouver en un clic ce qu'il avait — « Charger un
//     fichier… » et « Enregistrer sous… » restent des entrées explicites ;
//   · Annuler / Refaire disent le NOM de la transaction dans leur aide au
//     survol : c'est ce qui rend la granularité visible (§3.11) ;
//   · aucun menu modal : PopupMenu::showMenuAsync (JUCE_MODAL_LOOPS_PERMITTED=0).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugGlyphs.h"
#include <memory>

namespace plug::ui::v1
{
    class PlugBar : public juce::Component
    {
    public:
        explicit PlugBar (Presenter& presenter);
        ~PlugBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Appelé par l'éditeur à chaque notification qui touche les presets ou l'undo.
        void refresh();

        // L'éditeur possède les panneaux : la barre demande, elle n'affiche pas.
        std::function<void()> onShowAbout, onShowPrefs;

        static constexpr int kHeight = 44;

    private:
        void showPresetMenu();
        void chooseToLoad();
        void chooseToSave();
        void applyTooltips (const PresetView& presets, const UndoView& history);

        Presenter& presenter;

        // Chevron et engrenage sont DESSINÉS, pas écrits : U+25BE et U+2699 manquent
        // dans la police par défaut de Windows, et le pilote a vu le 17/09 un « ⚙ »
        // rendu en « … ». Un tracé n'a pas de police, donc pas de glyphe absent.
        IconButton presetButton { "preset", IconButton::Icon::ChevronDown };
        IconButton aboutButton  { "apropos", IconButton::Icon::None };
        IconButton prefsButton  { "prefs", IconButton::Icon::Gear };
        juce::TextButton undoButton, redoButton;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Label status;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugBar)
    };
}
