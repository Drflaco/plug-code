// PlugMacros — les huit macros, zone 2 du §3.7 (J4b étape 3, décision pilote Q2).
// Invariants et leur raison :
//   · huit knobs FONCTIONNELS : un geste = une transaction annulable, l'hôte voit
//     passer la valeur comme n'importe quelle autre entrée de la grille ;
//   · les ROUTES sont en lecture seule, dans l'aide au survol. Les éditer est une
//     interface à part entière, et elle est annoncée dans AVENIR.md — une absence
//     annoncée n'est pas un défaut ;
//   · aucune allocation dans paint() : libellés et textes de valeur sont composés à
//     la notification ;
//   · on n'inclut que le Presenter, les ViewTypes et les voisins de v1/ (frontière).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include <array>

namespace plug::ui::v1
{
    class PlugMacros : public juce::Component
    {
    public:
        explicit PlugMacros (Presenter&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        static constexpr int kHeight = 70;

    private:
        class MacroKnob : public juce::Component,
                          public juce::SettableTooltipClient
        {
        public:
            MacroKnob (Presenter&, int macro1);

            void setValue (float raw, const juce::String& valueText);
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;
            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        private:
            Presenter& presenter;
            juce::String gridId, caption, value;
            float raw = 0.0f, gestureStart = 0.0f;
            bool gesturing = false;
        };

        Presenter& presenter;
        std::array<std::unique_ptr<MacroKnob>, kMacros> knobs;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugMacros)
    };
}
