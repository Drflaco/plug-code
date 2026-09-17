#include "Prefs.h"

namespace plug::ui
{
    namespace
    {
        // Les défauts vivent ici, à un seul endroit : un fichier de préférences absent
        // ou tronqué doit donner exactement la même interface qu'une première ouverture.
        constexpr bool   kRatioTwoThirds = true;
        constexpr double kZoom           = 1.0;
        constexpr bool   kHoverHelp      = true;
        constexpr int    kHelpDelayMs    = 700;
        constexpr int    kDefaultMixLaw  = 1;     // -3 dB, loi naturelle de la plupart des skills
        constexpr int    kShownSlots     = 10;    // Q6 : 10 par défaut, 16 au plus
    }

    juce::File Prefs::file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LascauxLab").getChildFile ("Plug").getChildFile ("prefs.xml");
    }

    Prefs::Prefs()
    {
        // Chemin explicite plutôt que les conventions de PropertiesFile : le dossier
        // %APPDATA%\LascauxLab\Plug est déjà celui des presets et des mesures, tout
        // ce que Plug pose sur la machine du pilote tient au même endroit.
        auto f = file();
        f.getParentDirectory().createDirectory();

        juce::PropertiesFile::Options o;
        o.applicationName = "Plug";
        o.filenameSuffix = "xml";
        o.folderName = "LascauxLab";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        props = std::make_unique<juce::PropertiesFile> (f, o);
    }

    bool   Prefs::ratioTwoThirds() const { return props->getBoolValue ("ratioTwoThirds", kRatioTwoThirds); }
    double Prefs::zoom() const           { return juce::jlimit (0.75, 2.0, props->getDoubleValue ("zoom", kZoom)); }
    bool   Prefs::hoverHelp() const      { return props->getBoolValue ("hoverHelp", kHoverHelp); }
    int    Prefs::helpDelayMs() const    { return juce::jlimit (100, 5000, props->getIntValue ("helpDelayMs", kHelpDelayMs)); }
    int    Prefs::defaultMixLaw() const  { return juce::jlimit (0, 2, props->getIntValue ("defaultMixLaw", kDefaultMixLaw)); }
    int    Prefs::shownSlots() const     { return juce::jlimit (1, 16, props->getIntValue ("shownSlots", kShownSlots)); }

    void Prefs::setRatioTwoThirds (bool v) { props->setValue ("ratioTwoThirds", v); }
    void Prefs::setZoom (double v)         { props->setValue ("zoom", juce::jlimit (0.75, 2.0, v)); }
    void Prefs::setHoverHelp (bool v)      { props->setValue ("hoverHelp", v); }
    void Prefs::setHelpDelayMs (int v)     { props->setValue ("helpDelayMs", juce::jlimit (100, 5000, v)); }
    void Prefs::setDefaultMixLaw (int v)   { props->setValue ("defaultMixLaw", juce::jlimit (0, 2, v)); }
    void Prefs::setShownSlots (int v)      { props->setValue ("shownSlots", juce::jlimit (1, 16, v)); }

    void Prefs::save() { props->saveIfNeeded(); }
}
