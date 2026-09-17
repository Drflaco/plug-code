#include "PlugEdition.h"

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    //==========================================================================
    PlugEdition::Handle::Handle (Presenter& p) : presenter (p)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTooltip ("Partage séquenceur / inspecteur : un clic bascule entre 2/3–1/3 et "
                    "1/3–2/3. Deux positions seulement, et le réglage est global "
                    "(préférences, hors preset)."_fr);
    }

    void PlugEdition::Handle::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 2.0f);

        // Trois traits au centre : ça se prend, et ça dit que c'est une poignée.
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        const float cx = r.getCentreX(), cy = r.getCentreY();
        for (int i = -1; i <= 1; ++i)
            g.fillRect (cx + (float) i * 8.0f - 5.0f, cy - 0.5f, 10.0f, 1.0f);
    }

    void PlugEdition::Handle::mouseDown (const juce::MouseEvent&)
    {
        presenter.toggleRatio();
        if (onToggle) onToggle();
    }

    //==========================================================================
    PlugEdition::PlugEdition (Presenter& p)
        : presenter (p), sequencer (p), handle (p), inspector (p)
    {
        handle.onToggle = [this] { resized(); };

        freeRunning.setJustificationType (juce::Justification::centredRight);
        freeRunning.setInterceptsMouseClicks (false, false);
        freeRunning.setColour (juce::Label::textColourId, juce::Colour (0xffe2b34a));
        freeRunning.setVisible (false);
        addChildComponent (freeRunning);

        addAndMakeVisible (sequencer);
        addAndMakeVisible (handle);
        addAndMakeVisible (inspector);
    }

    void PlugEdition::paint (juce::Graphics&) {}

    void PlugEdition::resized()
    {
        auto r = getLocalBounds();
        auto banner = r.removeFromTop (16);
        freeRunning.setBounds (banner);

        // Partage vertical à deux positions. Le séquenceur garde toute la largeur : en
        // mode compact, seules les cases s'aplatissent, les 32 pas restent tous là.
        const double part = presenter.prefsView().ratioTwoThirds ? 2.0 / 3.0 : 1.0 / 3.0;
        const int seqH = (int) ((double) (r.getHeight() - kHandleHeight) * part);

        sequencer.setBounds (r.removeFromTop (seqH));
        handle.setBounds (r.removeFromTop (kHandleHeight));
        inspector.setBounds (r);
    }

    void PlugEdition::refresh()
    {
        sequencer.refresh();
        inspector.refresh();
    }

    void PlugEdition::setTransport (const TransportView& t)
    {
        sequencer.setTransport (t);

        // §3.3.3 : sans position hôte, l'horloge tourne à 120 BPM de secours. Le dire,
        // sinon le pilote croit que son transport est suivi alors qu'il ne l'est pas.
        const bool show = t.freeRunning;
        if (show != freeRunning.isVisible())
        {
            freeRunning.setText (show ? "120 BPM de secours — l'hôte ne donne pas de position"_fr : String(),
                                 juce::dontSendNotification);
            freeRunning.setVisible (show);
        }
    }
}
