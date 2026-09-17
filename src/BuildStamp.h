// BuildStamp — l'identité du binaire, affichée en tête de l'éditeur (J4a, 17/09).
// Garantit : on ne peut plus écouter un binaire sans savoir lequel. Un vieux
// module chargé par l'hôte s'est fait passer une soirée entière pour un défaut
// du moteur ; le commit et l'heure de compilation à l'écran l'auraient dit tout
// de suite. Texte seul, hors grille, sans paramètre : rien de tout cela n'entre
// dans l'état ni dans ce que l'hôte automatise (§3.9).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#ifndef PLUG_GIT_COMMIT
 #define PLUG_GIT_COMMIT "inconnu"
#endif

namespace plug
{
    // __DATE__ et __TIME__ datent la compilation de CE fichier : c'est la seule
    // horloge qui ne peut pas mentir sur l'âge du binaire qui tourne.
    inline juce::String buildStamp()
    {
        // JucePlugin_VersionString n'existe que dans les cibles plugin ; les outils
        // hors hôte partagent ce fichier et doivent compiler aussi.
       #ifdef JucePlugin_VersionString
        const juce::String version (JucePlugin_VersionString);
       #else
        const juce::String version ("dev");
       #endif
        // fromUTF8 : juce::String(const char*) décoderait « compilé » octet par octet
        // (CharPointer_ASCII) et l'afficherait « compilÃ© » — défaut vu dans Live le 17/09.
        return "Plug " + version
             + juce::String::fromUTF8 ("  ·  commit ") + PLUG_GIT_COMMIT
             + juce::String::fromUTF8 ("  ·  compilé le ") + __DATE__
             + juce::String::fromUTF8 (" à ") + __TIME__;
    }

    // Bandeau d'identité posé au-dessus de l'éditeur générique (échafaudage J2,
    // jusqu'à l'interface du §3.7 au J4b, qui le reprendra dans son à-propos §3.11).
    class BuildStampBar : public juce::Component
    {
    public:
        BuildStampBar()
        {
            label.setText (buildStamp(), juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centredLeft);
            label.setInterceptsMouseClicks (false, false);
            label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
            addAndMakeVisible (label);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.55f)); }
        void resized() override { label.setBounds (getLocalBounds().reduced (8, 0)); }

        static constexpr int kHeight = 22;

    private:
        juce::Label label;
    };
}
