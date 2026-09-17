#include "PlugPanels.h"

namespace plug::ui::v1
{
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        constexpr int kHeaderHeight = 26;
    }

    //==========================================================================
    Panel::Panel (const juce::String& titleText) : title (titleText)
    {
        close.setTooltip ("Fermer"_fr);
        close.setWantsKeyboardFocus (false);
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (close);
        setWantsKeyboardFocus (false);
    }

    void Panel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff26262b));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (14.0f);
        g.drawText (title, getLocalBounds().removeFromTop (kHeaderHeight).reduced (10, 0),
                    juce::Justification::centredLeft, false);
    }

    void Panel::resized()
    {
        auto header = getLocalBounds().removeFromTop (kHeaderHeight);
        close.setBounds (header.removeFromRight (kHeaderHeight).reduced (3));
    }

    juce::Rectangle<int> Panel::bodyArea() const
    {
        return getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (10, 6);
    }

    //==========================================================================
    AboutPanel::AboutPanel (Presenter& presenter) : Panel ("À propos"_fr)
    {
        const auto about = presenter.aboutView();

        identity.setText (about.buildStamp, juce::dontSendNotification);
        identity.setJustificationType (juce::Justification::topLeft);
        identity.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
        identity.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (identity);

        note.setText ("JUCE, le catalogue des effets et leurs versions : étape 6."_fr, juce::dontSendNotification);
        note.setJustificationType (juce::Justification::topLeft);
        note.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
        note.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (note);
    }

    void AboutPanel::resized()
    {
        Panel::resized();
        auto r = bodyArea();
        identity.setBounds (r.removeFromTop (44));
        note.setBounds (r);
    }

    //==========================================================================
    PrefsPanel::PrefsPanel() : Panel ("Préférences — étape 6"_fr)
    {
        todo.setText ("Ratio, zoom, aide au survol et son délai, loi de mélange par défaut, "
                      "nombre d'emplacements affichés, et la liste « À venir ». "
                      "Le fichier existe déjà : %APPDATA%\\LascauxLab\\Plug\\prefs.xml."_fr,
                      juce::dontSendNotification);
        todo.setJustificationType (juce::Justification::topLeft);
        todo.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        todo.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (todo);
    }

    void PrefsPanel::resized()
    {
        Panel::resized();
        todo.setBounds (bodyArea());
    }
}
