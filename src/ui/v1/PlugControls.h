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
        // `shownInLine` : la ligne du séquenceur montre ce paramètre (6a) — la pastille est pleine.
        void setView (const ParamView&, int slot1, bool shownInLine);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void send (float raw);
        juce::Rectangle<int> captionArea() const;   // le nom sous le cadran : cliquer = montrer dans la ligne

        Presenter& presenter;
        int slot1 = 1;
        juce::String gridId, caption, value;
        ParamView view;
        bool shown = false;
        bool gesturing = false;
        bool onSteps = false;     // le geste en cours pose sur la sélection (partielle) plutôt que la base
        float gestureStart = 0.0f;
    };

    // Une ligne de paramètre : cadenas, libellé, curseur, valeur + unité.
    class ParamRow : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        ParamRow (Presenter&, int slot1, int modulable);

        // `shownInLine` : la ligne du séquenceur montre ce paramètre (6a). Cliquer le
        // NOM le montre ; le curseur, lui, règle.
        void setView (const ParamView&, int slot1, bool shownInLine);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        // Correction 2 de la phase 2 (18/09) : la ligne mesure 251 px logiques dans le
        // panneau, et ses marges fixes (150 + 110) rendaient la zone du curseur NÉGATIVE.
        // Le « caret fantôme » vu par le pilote était le trait de base d'un curseur de
        // largeur −9 px, posé sur le premier chiffre de la valeur ; et aucun curseur
        // n'était cliquable. Règle : cadenas, libellé et valeur ont une largeur fixe et
        // sobre, le curseur prend CE QUI RESTE, et sous kMinSlider il n'existe pas —
        // ni halo, ni remplissage, ni trait, jamais un pixel sur le texte.
        static constexpr int kLockW = 20, kCaptionW = 100, kValueW = 72, kGap = 4, kMinSlider = 24;
        juce::Rectangle<int> lockArea() const;
        juce::Rectangle<int> captionArea() const;
        juce::Rectangle<int> valueArea() const;
        juce::Rectangle<int> sliderArea() const;    // vide si la place manque
        void sendFromX (int x);

        Presenter& presenter;
        int slot1 = 1, modulable = 0;
        juce::String gridId, caption, value, lockTag;
        bool showTag = false;   // le mot du cadenas ne s'écrit que s'il tient à côté du libellé
        bool shown = false;     // la ligne montre ce paramètre : pastille pleine
        ParamView view;
        bool gesturing = false;
        bool onSteps = false;   // le geste en cours pose sur la sélection (partielle) plutôt que la base
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
        juce::Slider glide, fade, damp;          // damp : l'amortissement (phase 3), attribut du Slot
        juce::Label glideLabel, fadeLabel, dampLabel, title, emptyHint;
        juce::TextButton activeButton, tailRing, tailCut, chooseButton;

        void applySlotSettings (const SlotView&);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugControls)
    };
}
