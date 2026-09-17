// BuildStamp — l'identité du binaire, affichée dans la barre et l'à-propos de
// l'interface v1 (J4a, 17/09 ; J4b étapes 1 et 6).
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
}
