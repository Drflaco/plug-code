// PlugSequencer — la grille de pas, §1 de la zone d'édition (J4b étape 4).
// Invariants et leur raison :
//   · RÈGLE ABSOLUE, posée par le pilote : AUCUNE allocation par frame. La
//     géométrie des 16 × 32 cases vit dans des tableaux redimensionnés dans
//     resized() ; les étiquettes de ligne et les badges sont composés à la
//     notification ; paint() ne fabrique ni juce::String ni Path dynamique, il
//     ne fait que remplir des rectangles. Une grille qui alloue à 60 Hz réveille
//     le ramasse-miettes de l'allocateur pendant que l'audio tourne ;
//   · la tête de lecture ne repeint QUE les deux colonnes qui changent
//     (repaint (rect)) ; le motif repeint tout, mais seulement à la notification ;
//   · elle ne lit jamais l'audio : la valeur d'un pas vient de la fonction pure
//     (StateQuery::stepDisplayValue via LineView), pas de Engine::valueCurve ;
//   · elle ne connaît que le Presenter et les ViewTypes (frontière v1/v2).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugGlyphs.h"
#include <vector>

namespace plug::ui::v1
{
    class PlugSequencer : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        explicit PlugSequencer (Presenter&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        void refresh();                                  // relit les Views, recompose les textes
        void setTransport (const TransportView&);        // 30 Hz : ne repeint que deux colonnes

        static constexpr int kSteps = 32;
        static constexpr int kLabelWidth = 96;
        static constexpr int kTailWidth = 52;            // bouton A/B + menu du paramètre montré

       #if PLUG_UI_TIMING
        // Mesure du rendu (ETAT a) « Rendu ») : p50/p99 de paint(), jamais dans le
        // binaire livré. Écrit dans %APPDATA%\LascauxLab\Plug\measure\ à la destruction.
        ~PlugSequencer() override;
       #endif

    private:
        // Une case, telle qu'elle se dessine. Des valeurs, pas des chaînes.
        struct Cell
        {
            bool on = true;
            StepModeView mode = StepModeView::Base;
            float value = 0.0f;
            bool hasValue = false;
            bool beyondLength = false;   // au-delà de seq.length : grisé
        };

        struct Row
        {
            juce::String label;          // « 3 FM », composée à la notification
            juce::String paramLabel;     // le paramètre montré en mode B
            bool modeB = false;
            bool present = false;
            bool selected = false;
        };

        int rowAt (int y) const;
        int stepAt (int x) const;
        juce::Rectangle<int> cellBounds (int row, int step) const;
        juce::Rectangle<int> columnBounds (int step) const;
        juce::Rectangle<int> modeButtonBounds (int row) const;
        juce::Rectangle<int> paramButtonBounds (int row) const;
        void showParamMenu (int row);

        Presenter& presenter;

        std::vector<Row> rows;
        std::vector<Cell> cells;          // rows.size() * kSteps, à plat
        std::vector<int> columnX;         // kSteps + 1 abscisses, calculées dans resized()
        std::vector<int> rowY;            // rows.size() + 1 ordonnées

        int playhead = -1;
        int selectedSlot = 1, selFirst = 1, selLast = 32;
        bool dragging = false;
        int dragAnchor = 1;

       #if PLUG_UI_TIMING
        juce::int64 t0 = 0;
        std::vector<double> paintUs;
       #endif

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugSequencer)
    };
}
