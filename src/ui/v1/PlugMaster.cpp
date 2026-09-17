#include "PlugMaster.h"

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        // Les libellés des deux entrées à choix. Ils doublent ceux de ParameterGrid.cpp,
        // que la frontière v1/v2 interdit d'inclure ici ; ils sont figés avec la grille
        // (Rév. 2), donc ce doublon ne peut pas dériver sans que la grille change — et
        // la grille ne change plus.
        const char* const kRouting[] = { "Pré", "Post" };
        const char* const kQuality[] = { "Éco", "Normal", "Haute" };

        constexpr int kLawIds[] = { 0, 1, 2 };
        const char* const kLawText[] = { "-6 dB", "-3 dB", "0 dB" };
        const char* const kLawHelp =
            "Loi de mélange du master (§3.7), entre le sec retardé et la chaîne :\n"
            "-6 dB — linéaire. Les deux gains somment à 1 : c'est la loi juste quand le "
            "traité est EN PHASE avec le sec (un gain, une découpe).\n"
            "-3 dB — puissance constante, cosinus et sinus. La loi juste quand le traité "
            "est décalé dans le temps (délai, réverbe, FM) : la somme garde son énergie.\n"
            "0 dB — les deux à plein au milieu, puis on retire. Le mélange le plus fort, "
            "au risque de la saturation ; utile pour un parallèle qu'on veut entendre.";
    }

    //==========================================================================
    PlugMaster::PlugMaster (Presenter& p) : presenter (p)
    {
        for (auto* l : { &outTitle, &masterTitle, &volumeValue, &mixValue })
        {
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*l);
        }
        outTitle.setText ("SORTIE"_fr, juce::dontSendNotification);
        masterTitle.setText ("MASTER"_fr, juce::dontSendNotification);
        outTitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        masterTitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
        volumeValue.setJustificationType (juce::Justification::centredRight);
        mixValue.setJustificationType (juce::Justification::centredRight);
        volumeValue.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        mixValue.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));

        for (auto* s : { &volume, &mix })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s->setRange (0.0, 1.0, 0.0);
            s->setWantsKeyboardFocus (false);
            addAndMakeVisible (*s);
        }
        volume.setTooltip ("Volume de sortie : 0,5 = 0 dB (unité), 1 = +6 dB, 0 = silence. "
                           "Appliqué par le moteur (§3.7)."_fr);
        mix.setTooltip ("Dry/Wet du master : la part de chaîne dans la sortie. 0 % = le sec seul, "
                        "retardé de la latence déclarée ; 100 % = la chaîne seule. "
                        "Appliqué par le moteur (§3.7)."_fr);
        wireGesture (volume, "master.volume");
        wireGesture (mix, "master.mix");

        for (size_t i = 0; i < law.size(); ++i)
        {
            law[i].setButtonText (String::fromUTF8 (kLawText[i]));
            law[i].setWantsKeyboardFocus (false);
            law[i].setTooltip (String::fromUTF8 (kLawHelp));
            law[i].setClickingTogglesState (false);
            law[i].onClick = [this, i] { presenter.setParam ("master.mixLaw", (float) kLawIds[i]); refresh(); };
            addAndMakeVisible (law[i]);
        }

        // §3.10 — les entrées que personne ne lit. Elles écrivent dans la grille (l'hôte
        // les voit, on ne peut pas les cacher) et elles le disent.
        for (const char* id : { "master.drive", "master.tone", "master.comp", "master.lowFreq" })
        {
            Inert e;
            e.gridId = id;
            e.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            e.slider->setRange (0.0, 1.0, 0.0);
            e.slider->setWantsKeyboardFocus (false);
            e.slider->setAlpha (0.45f);
            wireGesture (*e.slider, e.gridId);
            e.label = std::make_unique<juce::Label>();
            e.label->setInterceptsMouseClicks (false, false);
            e.label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.4f));
            addAndMakeVisible (*e.slider);
            addAndMakeVisible (*e.label);
            inerts.push_back (std::move (e));
        }

        for (auto* b : { &routing, &quality })
        {
            b->setWantsKeyboardFocus (false);
            b->setAlpha (0.45f);
            addAndMakeVisible (*b);
        }
        routing.onClick = [this] { showRoutingMenu(); };
        quality.onClick = [this] { showQualityMenu(); };

        refresh();
    }

    PlugMaster::~PlugMaster() = default;

    //==========================================================================
    void PlugMaster::wireGesture (juce::Slider& s, const String& gridId)
    {
        // Un geste = UNE transaction : on ouvre à la prise, on ferme au relâché.
        s.onDragStart = [this, gridId] { presenter.beginGesture (gridId); };
        s.onDragEnd   = [this, gridId] { presenter.endGesture (gridId); };
        s.onValueChange = [this, gridId, &s] { presenter.setParam (gridId, (float) s.getValue()); };
    }

    //==========================================================================
    void PlugMaster::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        // La sortie a son propre cadre : elle n'est pas un réglage du master parmi
        // d'autres, c'est ce qu'on cherche quand il faut baisser tout de suite.
        auto out = getLocalBounds().removeFromRight (360).reduced (4);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (out.toFloat(), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawRoundedRectangle (out.toFloat(), 3.0f, 1.0f);
    }

    void PlugMaster::resized()
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto out = r.removeFromRight (352);
        r.removeFromRight (12);

        // --- SORTIE
        outTitle.setBounds (out.removeFromTop (14));
        auto volRow = out.removeFromTop (20);
        volumeValue.setBounds (volRow.removeFromRight (64));
        volume.setBounds (volRow.withTrimmedLeft (56));
        auto mixRow = out.removeFromTop (20);
        mixValue.setBounds (mixRow.removeFromRight (64));
        mix.setBounds (mixRow.withTrimmedLeft (56));
        auto lawRow = out.removeFromTop (20);
        for (auto& b : law) { b.setBounds (lawRow.removeFromLeft (64).reduced (1)); lawRow.removeFromLeft (2); }

        // --- MASTER (§3.10, inerte)
        masterTitle.setBounds (r.removeFromTop (14));
        auto top = r.removeFromTop (22);
        auto bottom = r.removeFromTop (22);
        const int cell = juce::jmax (80, top.getWidth() / 3);

        auto place = [&cell] (juce::Rectangle<int>& row, Inert& e)
        {
            auto c = row.removeFromLeft (cell);
            e.label->setBounds (c.removeFromLeft (92));
            e.slider->setBounds (c);
        };
        if (inerts.size() >= 4)
        {
            place (top, inerts[0]);
            place (top, inerts[1]);
            routing.setBounds (top.removeFromLeft (juce::jmin (cell, top.getWidth())).reduced (2, 1));
            place (bottom, inerts[2]);
            place (bottom, inerts[3]);
            quality.setBounds (bottom.removeFromLeft (juce::jmin (cell, bottom.getWidth())).reduced (2, 1));
        }
    }

    //==========================================================================
    void PlugMaster::refresh()
    {
        const auto v = presenter.masterView();

        volume.setValue (v.volume, juce::dontSendNotification);
        mix.setValue (v.mix, juce::dontSendNotification);
        volumeValue.setText (v.volumeText + " dB", juce::dontSendNotification);
        mixValue.setText (v.mixText + " %", juce::dontSendNotification);

        for (size_t i = 0; i < law.size(); ++i)
        {
            const bool on = ((int) i == v.law);
            law[i].setColour (juce::TextButton::buttonColourId,
                              juce::Colours::white.withAlpha (on ? 0.22f : 0.08f));
            law[i].setColour (juce::TextButton::textColourOffId,
                              juce::Colours::white.withAlpha (on ? 0.95f : 0.55f));
        }

        // Les six entrées inertes : libellé + « (J4c) » et l'aide qui dit la vérité.
        for (const auto& e : v.inertEntries)
        {
            for (auto& slot : inerts)
            {
                if (slot.gridId != e.id) continue;
                slot.slider->setValue (e.raw, juce::dontSendNotification);
                slot.slider->setTooltip (e.help);
                slot.label->setText (e.label, juce::dontSendNotification);
                slot.label->setTooltip (e.help);
            }

            if (e.id == "master.driveRouting")
            {
                const int k = juce::jlimit (0, 1, (int) std::lround (e.raw));
                routing.setButtonText (String::fromUTF8 (kRouting[k]) + "  (J4c)");
                routing.setTooltip (e.help);
            }
            else if (e.id == "master.quality")
            {
                const int k = juce::jlimit (0, 2, (int) std::lround (e.raw));
                quality.setButtonText (String::fromUTF8 (kQuality[k]) + "  (J4c)");
                quality.setTooltip (e.help);
            }
        }
    }

    //==========================================================================
    void PlugMaster::showRoutingMenu()
    {
        juce::PopupMenu m;
        for (int i = 0; i < 2; ++i) m.addItem (i + 1, String::fromUTF8 (kRouting[i]));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&routing),
                         [this] (int r) { if (r > 0) { presenter.setParam ("master.driveRouting", (float) (r - 1)); refresh(); } });
    }

    void PlugMaster::showQualityMenu()
    {
        juce::PopupMenu m;
        for (int i = 0; i < 3; ++i) m.addItem (i + 1, String::fromUTF8 (kQuality[i]));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&quality),
                         [this] (int r) { if (r > 0) { presenter.setParam ("master.quality", (float) (r - 1)); refresh(); } });
    }
}
