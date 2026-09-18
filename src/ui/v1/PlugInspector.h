// PlugInspector — §2 de la zone d'édition (J4b étape 4).
// Invariants et leur raison :
//   · il édite le ou les pas SÉLECTIONNÉS de l'emplacement sélectionné : valeur,
//     plage, probabilité, verrou, transition — chacun par sa commande, chacune
//     une transaction nommée en français ;
//   · l'avertissement de re-dérivation (d-2) s'écrit SOUS les boutons et AVANT
//     le clic, depuis StateQuery::stepCounts : « Générer » retire les V des pas
//     figés, une nouvelle graine re-dérive tous les pas générés. Jamais modal,
//     jamais de confirmation — le compte est donné, et Ctrl+Z est derrière ;
//   · « Figer la passe » n'est cliquable que s'il y a quelque chose à figer ;
//   · aucune allocation par frame : les textes sont composés à la notification.
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
    class PlugInspector : public juce::Component
    {
    public:
        explicit PlugInspector (Presenter&);
        ~PlugInspector() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

    private:
        // Une ligne du tableau : Paramètre · Valeur du pas · Plage · Proba · Verrou ·
        // Transition. Peinte à la main pour tenir la règle de rendu et rester dense.
        class Row : public juce::Component,
                    public juce::SettableTooltipClient
        {
        public:
            Row (Presenter&, PlugInspector&);

            // `shownInLine` : la ligne du séquenceur montre ce paramètre (6a) ; cliquer le nom le montre.
            void setView (const ParamView&, int slot1, int step1, bool shownInLine);
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;

        private:
            enum class Grab { None, Value, Min, Max, Prob };

            juce::Rectangle<int> lockArea() const;
            juce::Rectangle<int> nameArea() const;
            juce::Rectangle<int> valueArea() const;
            juce::Rectangle<int> rangeArea() const;
            juce::Rectangle<int> probArea() const;
            juce::Rectangle<int> transitionArea() const;

            Presenter& presenter;
            PlugInspector& owner;
            ParamView view;
            int slot1 = 1, step1 = 1;
            bool shown = false;   // la ligne montre ce paramètre : pastille pleine (6a)
            juce::String caption, valueText, rangeText, probText, transitionText, lockTag;
            Grab grab = Grab::None;
            // Valeurs à la prise : le glisser part d'ici. Les relire dans la View à chaque
            // événement ajoutait la distance totale à une valeur qui la contenait déjà.
            float grabRaw = 0.0f, grabMin = 0.0f, grabMax = 1.0f, grabProb = 1.0f;
            int grabFirst = 1, grabLast = 1;   // les pas visés par la colonne « Pas » (correction 4)
        };

        Presenter& presenter;

        juce::Label title, warning, seedLabel, densityLabel;
        std::vector<std::unique_ptr<Row>> rows;
        juce::Slider density;
        juce::TextEditor seed;
        juce::TextButton newSeed, generate, capture;

        void applyWarnings (const StepCountsView&, const GenerationView&);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugInspector)
    };
}
