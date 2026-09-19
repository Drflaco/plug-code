// PlugTabs — la rangée d'onglets d'emplacements, zone 3 du §3.7 (J4b étape 2).
// Invariants et leur raison :
//   · elle ne connaît que le Presenter et les ViewTypes (frontière v1/v2,
//     scripts/check_ui_boundary.ps1) ;
//   · AUCUNE allocation pendant un glisser : les titres, les aides et le texte de
//     l'avertissement sont composés à la notification ou au seuil du geste, jamais
//     dans paint(). Le glisser ne déplace que des entiers ;
//   · le glisser dit AVANT le dépôt ce qui ne suivra pas l'effet (§3.2, ETAT d-1),
//     en ligne, jamais dans une boîte modale — l'hôte n'attend pas et une
//     confirmation à chaque geste rendrait le geste inutilisable. Ctrl+Z après,
//     offert trois secondes ;
//   · l'ordre de la chaîne EST l'indice de l'emplacement : déplacer un onglet
//     déplace l'effet, pas la ligne, pas les plages, pas les automations ;
//   · phase 3 (pilote, 20/09) : au-dessus de chaque onglet, un curseur Dry / Wet qui
//     règle la BASE du mélange de l'emplacement (slotNN.mix) — l'intensité globale
//     de l'effet dans la chaîne, quelle que soit la sélection de pas. Il ne pose
//     jamais de valeur par pas : c'est le panneau qui fait cela (geste A). La lampe
//     d'activité est verte quand l'emplacement traite, et respire (fondu lent) quand
//     un effet y est posé ; le minuteur ne repeint que les lampes, jamais l'onglet.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugGlyphs.h"
#include <memory>
#include <vector>

namespace plug::ui::v1
{
    class PlugTabs : public juce::Component,
                     private juce::Timer
    {
    public:
        explicit PlugTabs (Presenter& presenter);
        ~PlugTabs() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        // Relit les Views et recompose titres et aides. Appelée à chaque notification.
        void refresh();

        // Le menu d'effet d'un emplacement — les contrôles s'en servent aussi, pour
        // qu'il n'existe qu'un seul catalogue à tenir.
        void openSkillMenu (int slot1);

        static constexpr int kMixHeight = 12;    // la rangée Dry / Wet, au-dessus des onglets
        static constexpr int kMixGap = 3;
        static constexpr int kRowHeight = 34;
        static constexpr int kBannerHeight = 24;
        static constexpr int kHeight = kMixHeight + kMixGap + kRowHeight + 4 + kBannerHeight;

    private:
        // Un onglet. Il ne décide rien : il dessine son titre pré-composé et renvoie ses
        // événements de souris à la rangée, seule à connaître ses voisins.
        class Tab : public juce::Component,
                    public juce::SettableTooltipClient   // l'aide au survol du §3.11
        {
        public:
            Tab (PlugTabs& owner, int slotNumber);

            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;

            juce::Rectangle<int> lamp() const;      // l'indicateur d'activité, cliquable
            juce::Rectangle<int> lampHalo() const;  // la lampe et son halo : la zone que le minuteur repeint
            juce::Rectangle<int> arrow() const;     // le ▾ du menu d'effet

            juce::String title;          // « 3 · FM », composé au refresh
            int slot1 = 1;
            bool selected = false;
            bool active = true;
            bool unknown = false;
            bool present = false;
            bool highlighted = false;    // cible d'un échange ou d'une copie
            bool sourceOfDrag = false;

        private:
            PlugTabs& tabs;
        };

        // Le curseur Dry / Wet d'un emplacement. Un geste = UNE transaction sur la base
        // de slotNN.mix (beginGesture / setParam / endGesture) ; la molette pose un cran
        // par transaction ; un double clic remet 100 % (le défaut de la grille). Il ne
        // lit que `base`, posée au refresh : paint() n'alloue rien.
        class MixSlider : public juce::Component,
                          public juce::SettableTooltipClient
        {
        public:
            MixSlider (PlugTabs& owner, int slotNumber);

            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;
            void mouseDoubleClick (const juce::MouseEvent&) override;
            void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

            int slot1 = 1;
            float base = 1.0f;           // base de slotNN.mix, 0..1, relue au refresh
            bool enabled = false;        // un effet connu est posé : sinon grisé, inerte
            bool selected = false;

        private:
            void sendFromX (int x);

            PlugTabs& tabs;
            const juce::String gridId;   // « slot03.mix », composé une fois
            bool gesturing = false;
        };

        // Le minuteur de la lampe : séparé de celui du bandeau (juce::Timer privé de
        // la rangée), parce que les deux n'ont ni la même cadence ni la même vie.
        class Pulse : public juce::Timer
        {
        public:
            explicit Pulse (PlugTabs& owner) : tabs (owner) {}
            void timerCallback() override;
        private:
            PlugTabs& tabs;
        };

        enum class DropKind { None, Swap, Insert, Copy };

        void tabMouseDown (Tab&, const juce::MouseEvent&);
        void tabMouseDrag (Tab&, const juce::MouseEvent&);
        void tabMouseUp (Tab&, const juce::MouseEvent&);

        void showSkillMenu (int slot1);
        void updateDrag (const juce::MouseEvent& e);
        void applyDrop();
        void clearDropMarks();
        void setBanner (const juce::String& text, bool offerUndo);
        void timerCallback() override;
        juce::Rectangle<int> rowArea() const;
        juce::Rectangle<int> mixArea() const;

        Presenter& presenter;
        std::vector<std::unique_ptr<Tab>> tabs;
        std::vector<std::unique_ptr<MixSlider>> mixes;   // un par onglet, même indice

        // La respiration de la lampe : une phase, un niveau 0..1 lu par Tab::paint.
        Pulse pulseTimer { *this };
        float pulsePhase = 0.0f;
        float pulse = 0.0f;

        // État du glisser — des entiers, rien d'autre.
        bool dragging = false;
        int dragFrom = 0;                // emplacement source, 0 = aucun glisser
        int dropTo = 0;                  // emplacement de destination
        int insertLineX = -1;            // abscisse du trait d'insertion, -1 si aucun
        DropKind dropKind = DropKind::None;

        juce::Label banner;
        juce::TextButton bannerUndo;   // texte posé au constructeur, via la porte UTF-8
        const juce::String copyTag { "copie" };   // membre : jamais construit dans paint()

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugTabs)
    };
}
