// PlugPanels — les panneaux non modaux de l'interface v1 (J4b étape 1).
// Invariants et leur raison :
//   · NON MODAUX, toujours : JUCE_MODAL_LOOPS_PERMITTED=0 et un hôte n'attend
//     pas qu'on lui rende la main. Ce sont des enfants de l'éditeur, posés
//     par-dessus, qu'un clic sur ✕ ou en dehors referme ;
//   · ils ne connaissent que le Presenter et les ViewTypes, comme tout ce qui
//     vit dans src/ui/v1/ (frontière v1/v2, check_ui_boundary.ps1) ;
//   · l'À-PROPOS ne porte à l'étape 1 que l'identité du binaire — c'est elle
//     qui manquait la soirée où Live a joué un module de la veille (17/09).
//     JUCE, le catalogue des skills et leurs versions arrivent à l'étape 6.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"

namespace plug::ui::v1
{
    // Base commune : un cadre titré, un bouton de fermeture, un fond opaque.
    class Panel : public juce::Component
    {
    public:
        Panel (const juce::String& titleText);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override {}   // le panneau avale les clics

        std::function<void()> onClose;

    protected:
        juce::Rectangle<int> bodyArea() const;                 // la place laissée au contenu

    private:
        juce::String title;
        juce::TextButton close { "X" };
    };

    // Le panneau « À propos » de l'étape 1 : la ligne complète du buildStamp.
    class AboutPanel : public Panel
    {
    public:
        explicit AboutPanel (Presenter& presenter);
        void resized() override;

    private:
        juce::Label identity, note;
    };

}
