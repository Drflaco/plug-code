// PlugEdition — la zone d'édition : séquenceur au-dessus, inspecteur en dessous,
// et la poignée entre les deux (J4b étape 4, §C).
// Invariants et leur raison :
//   · la poignée a DEUX positions, pas un curseur continu : 2/3–1/3 ou 1/3–2/3.
//     Un partage libre invite à régler la fenêtre au lieu de travailler, et le
//     §3.7 ne demande qu'un basculement ;
//   · le partage est EN LARGEUR, séquenceur à GAUCHE et inspecteur à DROITE, avec
//     une poignée verticale : c'est ce que dessine SCHEMA_INTERFACE_v0.1 §1 et §2,
//     et le schéma fait foi (CdC §0 bis, §8.5 — on ne s'en écarte que sur décision
//     du pilote). À 1/3, le séquenceur passe en mode COMPACT : ni chiffre ni nom
//     d'effet, seulement ■ / · et les barres du mode B, l'étiquette réduite au
//     numéro d'emplacement ;
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
        void refreshSelection();     // chemin léger : seule la sélection a bougé
        void refreshHover();         // plus léger encore : la case survolée a bougé, l'inspecteur seul se relit (6b)
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

        // Le bandeau du séquenceur (correction 3 de la phase 2, 18/09) : Longueur,
        // Division, Swing — les trois réglages de la grille du §3.3.3, qui n'avaient
        // aucun widget. Ce sont des paramètres de l'état, automatisables : le bandeau
        // relit SequencerView et renvoie des commandes, il ne convertit rien.
        class SeqBar : public juce::Component
        {
        public:
            explicit SeqBar (Presenter&);
            void resized() override;
            void refresh();

        private:
            void showDivisionMenu();

            Presenter& presenter;
            juce::Label lengthLabel, divisionLabel, swingLabel;
            juce::Slider length, swing;
            juce::TextButton division;
            juce::StringArray divisionChoices;
        };

        Presenter& presenter;
        SeqBar seqBar;
        PlugSequencer sequencer;
        Handle handle;
        PlugInspector inspector;
        juce::Label freeRunning;

        static constexpr int kHandleWidth = 12;
        static constexpr int kBannerHeight = 24;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugEdition)
    };
}
