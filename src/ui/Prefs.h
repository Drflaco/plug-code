// Prefs — l'espace préférences du §3.11 : %APPDATA%\LascauxLab\Plug\prefs.xml.
// Invariants et leur raison :
//   · HORS preset et HORS état : ce sont des réglages de la machine du pilote, pas
//     du morceau. Un preset qui embarquerait le zoom rendrait un projet dépendant
//     de l'écran sur lequel il a été fait ;
//   · rien ici n'est un paramètre : l'hôte n'en sait rien, la grille reste à 284 ;
//   · lecture et écriture au message thread uniquement, comme tout le reste de la vue.
#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace plug::ui
{
    class Prefs
    {
    public:
        Prefs();

        static juce::File file();       // %APPDATA%\LascauxLab\Plug\prefs.xml

        bool ratioTwoThirds() const;    // partage de la fenêtre : 2/3 (vrai) ou 1/3
        double zoom() const;            // 0,75 à 2,0 ; appliqué par setTransform sur le contenu
        bool hoverHelp() const;
        int helpDelayMs() const;
        int defaultMixLaw() const;      // 0 = -6 dB, 1 = -3 dB, 2 = 0 dB
        int shownSlots() const;         // 10 par défaut, 16 au plus (Q6)

        void setRatioTwoThirds (bool);
        void setZoom (double);
        void setHoverHelp (bool);
        void setHelpDelayMs (int);
        void setDefaultMixLaw (int);
        void setShownSlots (int);

        void save();

    private:
        std::unique_ptr<juce::PropertiesFile> props;
    };
}
