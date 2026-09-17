// PlugGlyphs — les signes que l'interface DESSINE au lieu de les écrire (J4b étape 3).
// Invariants et leur raison :
//   · aucun caractère qui dépende d'une police absente. Le pilote a vu le 17/09 un
//     « ⚙ » rendu en « … » et un « — » en « â□□ » : la moitié venait de l'encodage,
//     l'autre de la couverture de la police par défaut de Windows (Segoe UI et
//     Verdana n'ont ni U+2699 « ⚙ », ni U+25BE « ▾», ni U+26A0 « ⚠ »). Un Path n'a
//     pas de police, donc pas de glyphe manquant possible ;
//   · les tracés sont construits DANS paint(), mais uniquement en primitives
//     (lignes, ellipses, triangles) : aucune allocation de juce::String, aucune
//     construction de Path dynamique par frame au-delà de quelques points ;
//   · U+2014 « — » et U+00B7 « · » restent écrits : ils sont dans toutes les polices
//     de la machine (Latin-1 et ponctuation générale de base).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace plug::ui::v1::glyph
{
    // Chevron vers le bas : le « ▾ » des menus.
    void chevronDown (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

    // Engrenage : le « ⚙ » des préférences.
    void gear (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

    // Triangle d'alerte : le « ⚠ » d'une skill absente du registre (§3.9).
    void warning (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

    // Cadenas : fermé (verrouillé) ou ouvert. Sert les contrôles du §3.3.1.
    void padlock (juce::Graphics&, juce::Rectangle<float> area, juce::Colour, bool closed);
}

namespace plug::ui::v1
{
    // Un bouton qui porte un texte ET/OU un signe dessiné. Il se peint lui-même plutôt
    // que de passer par le LookAndFeel, pour que le signe soit un tracé et jamais un
    // caractère. Le texte est composé à la notification, jamais dans paintButton().
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { None, ChevronDown, Gear };

        IconButton (const juce::String& componentName, Icon iconToDraw);

        void setLabelText (const juce::String& text);   // hors paint : à la notification
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;

    private:
        juce::String labelText;
        Icon icon;
    };
}
