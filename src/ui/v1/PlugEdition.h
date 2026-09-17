// PlugEdition — la zone d'édition : séquenceur au-dessus, inspecteur en dessous,
// et la poignée entre les deux (J4b étape 4, §C).
// Invariants et leur raison :
//   · la poignée a DEUX positions, pas un curseur continu : 2/3–1/3 ou 1/3–2/3.
//     Un partage libre invite à régler la fenêtre au lieu de travailler, et le
//     §3.7 ne demande qu'un basculement ;
//   · le partage est VERTICAL : le séquenceur garde ainsi toute la largeur pour
//     ses 32 pas, et le mode compact ne réduit que la HAUTEUR des cases — les 32
//     pas restent lisibles, ce que demande le §C ;
//   · le ratio vit dans les préférences (global, hors preset, §3.11) : c'est un
//     réglage de la machine du pilote, pas du morceau.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugInspector.h"
#include "PlugSequencer.h"

namespace plug::ui::v1
{
    class PlugEdition : public juce::Component
    {
    public:
        explicit PlugEdition (Presenter&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void setTransport (const TransportView&);

    private:
        // La poignée : un bandeau fin, cliquable, qui dit dans quel sens ça bascule.
        class Handle : public juce::Component,
                       public juce::SettableTooltipClient
        {
        public:
            explicit Handle (Presenter&);
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            std::function<void()> onToggle;

        private:
            Presenter& presenter;
        };

        Presenter& presenter;
        PlugSequencer sequencer;
        Handle handle;
        PlugInspector inspector;
        juce::Label freeRunning;

        static constexpr int kHandleHeight = 12;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugEdition)
    };
}
