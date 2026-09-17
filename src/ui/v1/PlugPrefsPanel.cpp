#include "PlugPrefsPanel.h"

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        void dress (juce::Label& l, float alpha, juce::Justification j = juce::Justification::centredLeft)
        {
            l.setInterceptsMouseClicks (false, false);
            l.setJustificationType (j);
            l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (alpha));
        }

        void dress (juce::TextEditor& t)
        {
            t.setMultiLine (true, true);
            t.setReadOnly (true);
            t.setScrollbarsShown (true);
            t.setCaretVisible (false);
            t.setWantsKeyboardFocus (false);
            t.setFont (juce::FontOptions (12.0f));
            t.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
            t.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.15f));
            t.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.8f));
        }
    }

    //==========================================================================
    PlugPrefsPanel::PlugPrefsPanel (Presenter& p) : Panel ("Préférences"_fr), presenter (p)
    {
        static const char* const kTabNames[] = { "À propos", "Aide", "À venir", "Travail" };
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            tabs[i].setButtonText (String::fromUTF8 (kTabNames[i]));
            tabs[i].setWantsKeyboardFocus (false);
            tabs[i].setClickingTogglesState (false);
            tabs[i].onClick = [this, i] { select ((Tab) i); };
            addAndMakeVisible (tabs[i]);
        }
        for (auto* pg : { &aboutPage, &helpPage, &comingPage, &workPage }) addChildComponent (*pg);

        //---------------------------------------------------------------- À propos
        dress (identity, 0.9f);
        dress (juceLine, 0.6f);
        dress (dspLine, 0.6f);
        dress (catalogueTitle, 0.75f);
        dress (catalogue);
        for (auto* c : { (juce::Component*) &identity, (juce::Component*) &juceLine,
                         (juce::Component*) &dspLine, (juce::Component*) &catalogueTitle,
                         (juce::Component*) &catalogue })
            aboutPage.addAndMakeVisible (*c);
        buildAbout();

        //-------------------------------------------------------------------- Aide
        hoverHelp.setButtonText ("Aide au survol"_fr);
        hoverHelp.setWantsKeyboardFocus (false);
        hoverHelp.setTooltip ("Coupe ou rend l'aide au survol de toute l'interface. "
                              "La couper ne la perd pas : un re-clic la rend (§3.11)."_fr);
        hoverHelp.onClick = [this] { presenter.setPrefHoverHelp (hoverHelp.getToggleState()); applyPrefs(); };
        helpPage.addAndMakeVisible (hoverHelp);

        helpDelay.setSliderStyle (juce::Slider::LinearHorizontal);
        helpDelay.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        helpDelay.setRange (100.0, 5000.0, 50.0);
        helpDelay.setTextValueSuffix (" ms");
        helpDelay.setWantsKeyboardFocus (false);
        helpDelay.setTooltip ("Délai avant que l'aide apparaisse, de 100 ms à 5 s."_fr);
        helpDelay.onDragEnd = [this] { presenter.setPrefHelpDelayMs ((int) helpDelay.getValue()); applyPrefs(); };
        helpPage.addAndMakeVisible (helpDelay);

        dress (helpDelayLabel, 0.65f);
        helpDelayLabel.setText ("Délai"_fr, juce::dontSendNotification);
        dress (helpNote, 0.45f);
        helpNote.setText ("Les deux réglages s'appliquent tout de suite, et vivent dans "
                          "%APPDATA%\\LascauxLab\\Plug\\prefs.xml — jamais dans un preset."_fr,
                          juce::dontSendNotification);
        helpPage.addAndMakeVisible (helpDelayLabel);
        helpPage.addAndMakeVisible (helpNote);

        //---------------------------------------------------------------- À venir
        dress (comingTitle, 0.75f);
        comingTitle.setText ("À venir — tenu à jour à chaque version"_fr, juce::dontSendNotification);
        dress (coming);
        // AVENIR.md tel qu'il est dans le dépôt : embarqué à la compilation, donc la
        // liste affichée ne PEUT pas diverger du fichier (étape 0).
        coming.setText (presenter.avenirText(), false);
        comingPage.addAndMakeVisible (comingTitle);
        comingPage.addAndMakeVisible (coming);

        //---------------------------------------------------------------- Travail
        dress (ratioLabel, 0.65f);  ratioLabel.setText ("Partage par défaut"_fr, juce::dontSendNotification);
        dress (zoomLabel, 0.65f);   zoomLabel.setText ("Zoom"_fr, juce::dontSendNotification);
        dress (lawLabel, 0.65f);    lawLabel.setText ("Loi de mélange"_fr, juce::dontSendNotification);
        dress (slotsLabel, 0.65f);  slotsLabel.setText ("Emplacements"_fr, juce::dontSendNotification);
        dress (inertNote, 0.45f);
        inertNote.setText ("Les deux derniers réglages sont affichés mais inertes : "
                           "le moteur ne les lit pas encore (J4c)."_fr, juce::dontSendNotification);

        static const char* const kRatioText[] = { "2/3 – 1/3", "1/3 – 2/3" };
        for (size_t i = 0; i < ratio.size(); ++i)
        {
            ratio[i].setButtonText (String::fromUTF8 (kRatioText[i]));
            ratio[i].setWantsKeyboardFocus (false);
            ratio[i].setTooltip ("Partage séquenceur / inspecteur au démarrage. La poignée « ║ » "
                                 "de la zone d'édition bascule le même réglage."_fr);
            ratio[i].onClick = [this, i]
            {
                if (presenter.prefsView().ratioTwoThirds != (i == 0)) presenter.toggleRatio();
                applyPrefs();
            };
            workPage.addAndMakeVisible (ratio[i]);
        }

        zoom.setSliderStyle (juce::Slider::LinearHorizontal);
        zoom.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        zoom.setRange (75.0, 150.0, 5.0);
        zoom.setTextValueSuffix (" %");
        zoom.setWantsKeyboardFocus (false);
        zoom.setTooltip ("Échelle de la fenêtre, de 75 % à 150 %. La mise en page reste en "
                         "1280 × 800 logiques : c'est l'affichage qui est mis à l'échelle."_fr);
        zoom.onDragEnd = [this] { presenter.setPrefZoom (zoom.getValue() / 100.0); applyPrefs(); };
        workPage.addAndMakeVisible (zoom);

        slots.setSliderStyle (juce::Slider::LinearHorizontal);
        slots.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        slots.setRange (10.0, 16.0, 1.0);
        slots.setWantsKeyboardFocus (false);
        slots.setTooltip ("Emplacements affichés : 10 par défaut, 16 au plus. Le compte monte "
                          "de lui-même si un emplacement plus haut est occupé (§3.2)."_fr);
        slots.onDragEnd = [this] { presenter.setPrefShownSlots ((int) slots.getValue()); applyPrefs(); };
        workPage.addAndMakeVisible (slots);

        defaultLaw.setWantsKeyboardFocus (false);
        defaultLaw.setTooltip ("Loi de mélange des NOUVEAUX presets. Réservé : en v1 chaque preset "
                               "porte la sienne, ce réglage ne s'applique nulle part — appliqué au J4c."_fr);
        defaultLaw.onClick = [this]
        {
            const auto choices = presenter.masterView().lawChoices;
            juce::PopupMenu m;
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i]);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&defaultLaw),
                             [this] (int r) { if (r > 0) { presenter.setPrefDefaultMixLaw (r - 1); applyPrefs(); } });
        };
        workPage.addAndMakeVisible (defaultLaw);

        latencyMode.setButtonText ("Constante  (J4c)"_fr);
        latencyMode.setTooltip ("Compensation de latence : pas encore implémentée — J4c. "
                                "Le plugin déclare sa latence à l'hôte, qui la compense lui-même."_fr);
        defaultQuality.setButtonText ("Normal  (J4c)"_fr);
        defaultQuality.setTooltip ("Qualité par défaut : pas encore implémentée — J4c. "
                                   "Le socle ne transmet pas encore master.quality aux skills."_fr);
        for (auto* b : { &latencyMode, &defaultQuality })
        {
            b->setEnabled (false);
            b->setAlpha (0.45f);
            b->setWantsKeyboardFocus (false);
            workPage.addAndMakeVisible (*b);
        }
        for (auto* l : { &ratioLabel, &zoomLabel, &lawLabel, &slotsLabel, &inertNote })
            workPage.addAndMakeVisible (*l);

        refreshWork();
        select (Tab::About);
    }

    PlugPrefsPanel::~PlugPrefsPanel() = default;

    //==========================================================================
    void PlugPrefsPanel::buildAbout()
    {
        const auto a = presenter.aboutView();

        identity.setText (a.buildStamp, juce::dontSendNotification);
        juceLine.setText ("JUCE "_fr + a.juceVersion, juce::dontSendNotification);
        // La vérité du README : le sous-module est là, le catalogue ne s'en sert pas.
        dspLine.setText ("Signalsmith Stretch 1.1.0 — en sous-module, non utilisé par le catalogue"_fr,
                         juce::dontSendNotification);
        catalogueTitle.setText ("Catalogue : "_fr + String ((int) a.skills.size()) + " effets"_fr,
                                juce::dontSendNotification);

        String t;
        for (const auto& s : a.skills)
        {
            t << s.label << "  (" << s.id << " v" << s.version << ")\n"
              << "    loi "_fr << s.mixLaw;
            if (s.latencyKnown)
                t << " · latence "_fr << s.latency << (s.latency > 1 ? " échantillons"_fr : " échantillon"_fr)
                  << " à 48 kHz"_fr;
            t << "\n";
        }
        catalogue.setText (t, false);
    }

    void PlugPrefsPanel::refreshWork()
    {
        const auto p = presenter.prefsView();
        hoverHelp.setToggleState (p.hoverHelp, juce::dontSendNotification);
        helpDelay.setValue (p.helpDelayMs, juce::dontSendNotification);
        zoom.setValue (p.zoom * 100.0, juce::dontSendNotification);
        slots.setValue (p.shownSlots, juce::dontSendNotification);

        const auto choices = presenter.masterView().lawChoices;
        defaultLaw.setButtonText (juce::isPositiveAndBelow (p.defaultMixLaw, choices.size())
                                      ? choices[p.defaultMixLaw] + "  (réservé)"_fr
                                      : "—"_fr);

        for (size_t i = 0; i < ratio.size(); ++i)
        {
            const bool on = (p.ratioTwoThirds == (i == 0));
            ratio[i].setColour (juce::TextButton::buttonColourId,
                                juce::Colours::white.withAlpha (on ? 0.22f : 0.08f));
            ratio[i].setColour (juce::TextButton::textColourOffId,
                                juce::Colours::white.withAlpha (on ? 0.95f : 0.55f));
        }
    }

    void PlugPrefsPanel::applyPrefs()
    {
        refreshWork();
        if (onPrefsChanged) onPrefsChanged();
    }

    //==========================================================================
    void PlugPrefsPanel::select (Tab t)
    {
        current = t;
        aboutPage.setVisible (t == Tab::About);
        helpPage.setVisible (t == Tab::Help);
        comingPage.setVisible (t == Tab::Coming);
        workPage.setVisible (t == Tab::Work);

        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const bool on = ((Tab) i == t);
            tabs[i].setColour (juce::TextButton::buttonColourId,
                               juce::Colours::white.withAlpha (on ? 0.22f : 0.08f));
            tabs[i].setColour (juce::TextButton::textColourOffId,
                               juce::Colours::white.withAlpha (on ? 0.95f : 0.55f));
        }
        resized();
    }

    void PlugPrefsPanel::resized()
    {
        Panel::resized();
        auto r = bodyArea();

        auto bar = r.removeFromTop (24);
        for (auto& b : tabs) { b.setBounds (bar.removeFromLeft (110).reduced (1)); bar.removeFromLeft (3); }
        r.removeFromTop (8);

        for (auto* pg : { &aboutPage, &helpPage, &comingPage, &workPage }) pg->setBounds (r);

        // --- À propos
        {
            auto a = aboutPage.getLocalBounds();
            identity.setBounds (a.removeFromTop (20));
            juceLine.setBounds (a.removeFromTop (16));
            dspLine.setBounds (a.removeFromTop (16));
            a.removeFromTop (6);
            catalogueTitle.setBounds (a.removeFromTop (16));
            catalogue.setBounds (a);
        }
        // --- Aide
        {
            auto h = helpPage.getLocalBounds();
            hoverHelp.setBounds (h.removeFromTop (24));
            h.removeFromTop (6);
            auto row = h.removeFromTop (22);
            helpDelayLabel.setBounds (row.removeFromLeft (60));
            helpDelay.setBounds (row.removeFromLeft (360));
            h.removeFromTop (10);
            helpNote.setBounds (h.removeFromTop (34));
        }
        // --- À venir
        {
            auto c = comingPage.getLocalBounds();
            comingTitle.setBounds (c.removeFromTop (18));
            c.removeFromTop (4);
            coming.setBounds (c);
        }
        // --- Travail
        {
            auto w = workPage.getLocalBounds();
            auto row = w.removeFromTop (24);
            ratioLabel.setBounds (row.removeFromLeft (150));
            for (auto& b : ratio) { b.setBounds (row.removeFromLeft (90).reduced (1)); row.removeFromLeft (4); }
            w.removeFromTop (6);

            row = w.removeFromTop (22);
            zoomLabel.setBounds (row.removeFromLeft (150));
            zoom.setBounds (row.removeFromLeft (330));
            w.removeFromTop (6);

            row = w.removeFromTop (22);
            slotsLabel.setBounds (row.removeFromLeft (150));
            slots.setBounds (row.removeFromLeft (330));
            w.removeFromTop (6);

            row = w.removeFromTop (24);
            lawLabel.setBounds (row.removeFromLeft (150));
            defaultLaw.setBounds (row.removeFromLeft (170).reduced (1));
            w.removeFromTop (10);

            row = w.removeFromTop (24);
            latencyMode.setBounds (row.removeFromLeft (170).reduced (1));
            row.removeFromLeft (8);
            defaultQuality.setBounds (row.removeFromLeft (170).reduced (1));
            w.removeFromTop (6);
            inertNote.setBounds (w.removeFromTop (32));
        }
    }
}
