// PlugControls — les contrôles de l'effet sélectionné, zone 4 du §3.7 (J4b étape 3).
// Invariants et leur raison :
//   · on n'inclut que le Presenter, les ViewTypes, Format et les voisins de v1/
//     (frontière v1/v2, scripts/check_ui_boundary.ps1) ;
//   · AUCUNE allocation dans paint() : tous les textes (libellé, valeur, unité, mot
//     du cadenas) sont composés à la notification et gardés dans le widget ; le halo
//     est tracé en arcs primitifs, sans Path dynamique ;
//   · le halo dit ce que la GÉNÉRATION peut faire : plage [min,max] en clair, plage
//     effective à la densité courante en plein. Il DISPARAÎT quand le paramètre est
//     verrouillé — la génération ne peut plus y toucher, et ça se voit sans lire (d-3) ;
//   · un geste = une transaction : beginGesture / setParam… / endGesture. La molette
//     règle finement. Aucun double-clic : rien qui ne soit demandé ;
//   · un paramètre STRUCTUREL ne bascule pas, et son aide dit pourquoi (§3.3.1).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Format.h"
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugGlyphs.h"
#include <memory>
#include <vector>

namespace plug::ui::v1
{
    // Le knob principal : halo de génération, trait de base, point de la valeur du pas.
    class Knob : public juce::Component,
                 public juce::SettableTooltipClient
    {
    public:
        explicit Knob (Presenter&);

        // Le knob suit l'emplacement SÉLECTIONNÉ : sa cible change avec lui.
        void setView (const ParamView&, int slot1);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void send (float raw);

        Presenter& presenter;
        juce::String gridId, caption, value;
        ParamView view;
        bool gesturing = false;
        float gestureStart = 0.0f;
    };

    // Une ligne de paramètre : cadenas, libellé, curseur, valeur + unité.
    class ParamRow : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        ParamRow (Presenter&, int slot1, int modulable);

        void setView (const ParamView&, int slot1);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> lockArea() const;
        juce::Rectangle<int> sliderArea() const;
        void sendFromX (int x);

        Presenter& presenter;
        int slot1 = 1, modulable = 0;
        juce::String gridId, caption, value, lockTag;
        ParamView view;
        bool gesturing = false;
    };

    class PlugControls : public juce::Component
    {
    public:
        explicit PlugControls (Presenter&);
        ~PlugControls() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        // L'éditeur ouvre le menu d'effet : les onglets le tiennent déjà.
        std::function<void (int)> onChooseSkill;

    private:
        Presenter& presenter;

        Knob main;
        std::vector<std::unique_ptr<ParamRow>> rows;      // paramA..F, stereo, res1..3
        std::unique_ptr<ParamRow> mixRow, gainRow;
        juce::Slider glide, fade;
        juce::Label glideLabel, fadeLabel, title, emptyHint;
        juce::TextButton activeButton, tailRing, tailCut, chooseButton;

        void applySlotSettings (const SlotView&);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugControls)
    };
}
