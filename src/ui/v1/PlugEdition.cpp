#include "PlugEdition.h"
#include "../Format.h"

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    //==========================================================================
    PlugEdition::Handle::Handle (Presenter& p) : presenter (p)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTooltip ("Partage séquenceur / inspecteur : un clic bascule entre 2/3–1/3 et "
                    "1/3–2/3 en largeur. Deux positions seulement, et le réglage est "
                    "global (préférences, hors preset)."_fr);
    }

    void PlugEdition::Handle::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 2.0f);

        // Trois traits empilés au centre : le « ║ » du schéma. Ça se prend, et ça dit
        // que c'est une poignée.
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        const float cx = r.getCentreX(), cy = r.getCentreY();
        for (int i = -1; i <= 1; ++i)
            g.fillRect (cx - 0.5f, cy + (float) i * 8.0f - 5.0f, 1.0f, 10.0f);
    }

    void PlugEdition::Handle::mouseDown (const juce::MouseEvent&)
    {
        presenter.toggleRatio();
        if (onToggle) onToggle();
    }

    //==========================================================================
    PlugEdition::SeqBar::SeqBar (Presenter& p) : presenter (p)
    {
        for (auto* l : { &lengthLabel, &divisionLabel, &swingLabel })
        {
            l->setJustificationType (juce::Justification::centredLeft);
            l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
            l->setFont (juce::Font (juce::FontOptions (12.0f)));
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*l);
        }
        lengthLabel.setText ("Longueur"_fr, juce::dontSendNotification);
        divisionLabel.setText ("Division"_fr, juce::dontSendNotification);
        swingLabel.setText ("Swing"_fr, juce::dontSendNotification);

        // Longueur : entière, 2..32, la case de texte accepte une valeur tapée. Une
        // transaction par valeur posée (au relâché ou à la saisie), nommée par le Presenter.
        length.setSliderStyle (juce::Slider::LinearHorizontal);
        length.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 18);
        length.setRange (2.0, 32.0, 1.0);
        length.setWantsKeyboardFocus (false);
        length.setTooltip (Format::seqSettingHelp ("length"));
        length.onDragEnd = [this] { presenter.setSeqLength (juce::roundToInt (length.getValue())); };
        length.onValueChange = [this]
        {
            // Valeur tapée ou molette : pas de geste ouvert, on pose tout de suite. Pendant
            // un glisser, on attend le relâché — une seule transaction pour le mouvement.
            if (! length.isMouseButtonDown()) presenter.setSeqLength (juce::roundToInt (length.getValue()));
        };
        addAndMakeVisible (length);

        // Division : un bouton qui porte le libellé courant et ouvre la liste, comme le
        // routage du master. Les libellés viennent de la couche de présentation.
        division.setWantsKeyboardFocus (false);
        division.setTooltip (Format::seqSettingHelp ("division"));
        division.onClick = [this] { showDivisionMenu(); };
        addAndMakeVisible (division);

        // Swing : geste continu sur seq.swing, une transaction de la prise au relâché.
        swing.setSliderStyle (juce::Slider::LinearHorizontal);
        swing.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 18);
        swing.setRange (0.0, 1.0, 0.0);
        swing.setWantsKeyboardFocus (false);
        swing.setTooltip (Format::seqSettingHelp ("swing"));
        swing.textFromValueFunction = [] (double v) { return Format::swingText ((float) v); };
        swing.valueFromTextFunction = [] (const String& t) { return t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue() / 100.0; };
        swing.onDragStart = [this] { presenter.beginGesture ("seq.swing"); };
        swing.onDragEnd   = [this] { presenter.endGesture ("seq.swing"); };
        swing.onValueChange = [this] { presenter.setParam ("seq.swing", (float) swing.getValue()); };
        swing.updateText();   // le texte de la case n'est recalculé qu'au changement de valeur : ici, une fois
        addAndMakeVisible (swing);
    }

    void PlugEdition::SeqBar::resized()
    {
        auto r = getLocalBounds().reduced (0, 2);
        // Trois groupes ; les libellés et le bouton ont une largeur fixe, les deux
        // curseurs se partagent le reste. En compact (séquenceur à 1/3) les libellés
        // disparaissent — l'aide au survol les porte — et les curseurs gardent leur place.
        const bool compact = r.getWidth() < 420;
        const int labels = compact ? 0 : 58 + 56 + 40;
        const int fixed = labels + 66 + 3 * 8;
        const int slider = juce::jmax (40, (r.getWidth() - fixed) / 2);
        for (auto* l : { &lengthLabel, &divisionLabel, &swingLabel }) l->setVisible (! compact);

        if (! compact) lengthLabel.setBounds (r.removeFromLeft (58));
        length.setBounds (r.removeFromLeft (slider));
        r.removeFromLeft (8);
        if (! compact) divisionLabel.setBounds (r.removeFromLeft (56));
        division.setBounds (r.removeFromLeft (66).reduced (0, 1));
        r.removeFromLeft (8);
        if (! compact) swingLabel.setBounds (r.removeFromLeft (40));
        swing.setBounds (r.removeFromLeft (slider));
    }

    void PlugEdition::SeqBar::refresh()
    {
        const auto v = presenter.sequencerView();
        length.setRange ((double) v.lengthMin, (double) v.lengthMax, 1.0);
        length.setValue ((double) v.length, juce::dontSendNotification);
        divisionChoices = v.divisionChoices;
        division.setButtonText (v.divisionChoices[juce::jlimit (0, juce::jmax (0, v.divisionChoices.size() - 1), v.division)]);
        swing.setValue ((double) v.swing, juce::dontSendNotification);
    }

    void PlugEdition::SeqBar::showDivisionMenu()
    {
        juce::PopupMenu m;
        const int current = presenter.sequencerView().division;
        for (int i = 0; i < divisionChoices.size(); ++i)
            m.addItem (juce::PopupMenu::Item (divisionChoices[i]).setID (i + 1).setTicked (i == current));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&division),
                         [this] (int r) { if (r > 0) { presenter.setSeqDivision (r - 1); refresh(); } });
    }

    //==========================================================================
    PlugEdition::PlugEdition (Presenter& p)
        : presenter (p), seqBar (p), sequencer (p), handle (p), inspector (p)
    {
        handle.onToggle = [this] { resized(); };

        freeRunning.setJustificationType (juce::Justification::centredRight);
        freeRunning.setInterceptsMouseClicks (false, false);
        freeRunning.setColour (juce::Label::textColourId, juce::Colour (0xffe2b34a));
        freeRunning.setVisible (false);
        addChildComponent (freeRunning);

        addAndMakeVisible (seqBar);
        addAndMakeVisible (sequencer);
        addAndMakeVisible (handle);
        addAndMakeVisible (inspector);
    }

    void PlugEdition::paint (juce::Graphics&) {}

    void PlugEdition::resized()
    {
        auto r = getLocalBounds();
        auto banner = r.removeFromTop (kBannerHeight);

        // Partage EN LARGEUR à deux positions, séquenceur à gauche (schéma §1 et §2).
        // À 1/3 le séquenceur passe en compact : les 32 pas y sont tous, sans chiffre.
        const double part = presenter.prefsView().ratioTwoThirds ? 2.0 / 3.0 : 1.0 / 3.0;
        const int seqW = (int) ((double) (r.getWidth() - kHandleWidth) * part);

        // Le bandeau des réglages de la grille est au-dessus du séquenceur, à sa largeur ;
        // le bandeau « 120 BPM de secours » garde la droite, au-dessus de l'inspecteur.
        seqBar.setBounds (banner.removeFromLeft (seqW));
        freeRunning.setBounds (banner);

        sequencer.setBounds (r.removeFromLeft (seqW));
        handle.setBounds (r.removeFromLeft (kHandleWidth));
        inspector.setBounds (r);
    }

    void PlugEdition::refresh()
    {
        seqBar.refresh();
        sequencer.refresh();
        inspector.refresh();
    }

    void PlugEdition::refreshSelection()
    {
        sequencer.refreshSelection();
        inspector.refresh();          // lui DOIT relire : il montre le pas sélectionné
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
