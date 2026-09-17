// PlugPrefsPanel — l'espace préférences du §3.11, schéma §5 (J4b étape 6).
// Invariants et leur raison :
//   · RIEN ici n'entre dans un preset ni dans l'état. Ce sont des réglages de la
//     machine du pilote, pas du morceau : un preset qui embarquerait le zoom
//     rendrait un projet dépendant de l'écran sur lequel il a été fait. PlugBench
//     le vérifie : toucher aux six préférences ne change pas un octet de PlugState ;
//   · l'onglet « À propos » porte l'identité du binaire et le catalogue réel, lu
//     du registre. Une soirée a été perdue le 17/09 sur un module de la veille ;
//   · l'onglet « À venir » lit AVENIR.md EMBARQUÉ (BinaryData, étape 0) : la
//     liste ne peut pas diverger du dépôt, elle EST le fichier du dépôt ;
//   · ce qui n'existe pas est affiché grisé et marqué « (J4c) », jamais caché ;
//   · non modal, comme tous les panneaux (JUCE_MODAL_LOOPS_PERMITTED=0).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugGlyphs.h"
#include "PlugPanels.h"
#include <array>
#include <memory>
#include <vector>

namespace plug::ui::v1
{
    class PlugPrefsPanel : public Panel
    {
    public:
        explicit PlugPrefsPanel (Presenter&);
        ~PlugPrefsPanel() override;

        void resized() override;

        // L'éditeur réapplique le zoom et le délai d'aide dès qu'ils changent : le
        // pilote qui coupe l'aide ne la perd pas, un re-clic la rend.
        std::function<void()> onPrefsChanged;

        static constexpr int kWidth = 760, kHeight = 430;

    private:
        enum class Tab { About, Help, Coming, Work };

        void select (Tab);
        void buildAbout();
        void applyPrefs();
        void refreshWork();

        Presenter& presenter;
        Tab current = Tab::About;

        std::array<juce::TextButton, 4> tabs;
        juce::Component aboutPage, helpPage, comingPage, workPage;

        // À propos
        juce::Label identity, juceLine, dspLine, catalogueTitle;
        juce::TextEditor catalogue;

        // Aide
        juce::ToggleButton hoverHelp;
        juce::Slider helpDelay;
        juce::Label helpDelayLabel, helpNote;

        // À venir
        juce::Label comingTitle;
        juce::TextEditor coming;

        // Travail
        juce::Label ratioLabel, zoomLabel, lawLabel, slotsLabel, inertNote;
        std::array<juce::TextButton, 2> ratio;
        juce::Slider zoom, slots;
        juce::TextButton defaultLaw, latencyMode, defaultQuality;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugPrefsPanel)
    };
}
