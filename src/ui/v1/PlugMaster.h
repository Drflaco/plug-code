// PlugMaster — Master et Sortie, zone 6 du §3.7 (J4b étape 5).
// Invariants et leur raison :
//   · la SORTIE est à part et jamais enfouie : volume, dry/wet et loi de mélange
//     sont les trois réglages qu'on cherche en urgence, et le moteur les applique
//     vraiment (§3.7). Ils ont leur panneau, à droite, toujours visible ;
//   · le MASTER du §3.10 n'est PAS implémenté : drive, routage, tonalité,
//     compression, grave préservé et qualité sont des entrées de la grille figée
//     que personne ne lit. Ils s'affichent, ils bougent et ils s'enregistrent —
//     l'hôte les voit, on ne peut pas les cacher — mais ils sont grisés et
//     marqués « (J4c) ». Une soirée a été perdue le 17/09 à chercher un défaut
//     dans un compresseur qui n'existe pas : l'interface dit la vérité ;
//   · aucun DSP inventé ici, aucune allocation par frame (textes composés à la
//     notification) ;
//   · on ne connaît que le Presenter et les ViewTypes (frontière v1/v2).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Presenter.h"
#include "../ViewTypes.h"
#include "PlugGlyphs.h"
#include <array>
#include <memory>
#include <vector>

namespace plug::ui::v1
{
    class PlugMaster : public juce::Component
    {
    public:
        explicit PlugMaster (Presenter&);
        ~PlugMaster() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        static constexpr int kHeight = 86;

    private:
        // Une entrée continue du master, grisée et marquée (J4c) : elle écrit bien dans
        // la grille, elle ne produit simplement aucun son.
        struct Inert
        {
            std::unique_ptr<juce::Slider> slider;
            std::unique_ptr<juce::Label> label;
            juce::String gridId;
        };

        void wireGesture (juce::Slider&, const juce::String& gridId);
        void showQualityMenu();
        void showRoutingMenu();

        Presenter& presenter;

        juce::Label outTitle, masterTitle, volumeValue, mixValue;
        juce::Slider volume, mix;
        std::array<juce::TextButton, 3> law;      // -6 dB | -3 dB | 0 dB
        std::vector<Inert> inerts;                // drive, tone, comp, lowFreq
        juce::TextButton routing, quality;        // choix : [Pré | Post] et Éco/Normal/Haute

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugMaster)
    };
}
