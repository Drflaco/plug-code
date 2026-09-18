#include "PlugControls.h"
#include <cmath>

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        // Le knob balaie 270°, de 7 h 30 à 4 h 30 : la course habituelle, et les deux
        // butées restent distinctes à l'œil.
        constexpr float kStart = juce::MathConstants<float>::pi * 1.25f;
        constexpr float kSpan  = juce::MathConstants<float>::pi * 1.5f;
        constexpr float kFine  = 0.02f;     // molette : un pas fin
        constexpr float kDragSpan = 180.0f; // pixels pour parcourir toute la course

        String slotGridId (int slot1, const char* suffix)
        {
            return "slot" + String (slot1).paddedLeft ('0', 2) + "." + suffix;
        }

        float angleFor (float raw) { return kStart + kSpan * juce::jlimit (0.0f, 1.0f, raw); }

        void arc (juce::Graphics& g, juce::Rectangle<float> r, float from, float to,
                  juce::Colour c, float thickness)
        {
            if (to <= from) return;
            juce::Path p;
            p.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() * 0.5f, r.getHeight() * 0.5f,
                             0.0f, from, to, true);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness));
        }
    }

    //==========================================================================
    Knob::Knob (Presenter& p) : presenter (p) {}

    // La règle de la sélection (décision pilote du 18/09), dite dans l'aide : rien ne
    // demande de mémoire au pilote, le survol lui redit ce que son geste va faire.
    static String selectionRuleHelp (const Presenter& p)
    {
        if (p.selectionIsPartial())
            return "\nPas "_fr + String (p.firstSelectedStep()) + "–"_fr + String (p.lastSelectedStep())
                   + " sélectionnés : la valeur se pose sur ces pas et les fige (Ctrl+Z la défait d'un coup)."_fr;
        return "\nLigne entière sélectionnée : règle la base de l'emplacement. "
               "Sélectionne des pas pour leur poser une valeur."_fr;
    }

    static String showInLineHelp()
    {
        return "\nCliquer le nom : la ligne du séquenceur montre ce paramètre en barres (mode B)."_fr;
    }

    void Knob::setView (const ParamView& v, int slot, bool shownInLine)
    {
        slot1 = slot;
        shown = shownInLine;
        gridId = slotGridId (slot, v.name.toRawUTF8());
        view = v;
        caption = v.label;
        value = v.valueText;                       // composé ici, jamais dans paint()
        setTooltip (v.help + (v.lockReason.isNotEmpty() ? "\n" + v.lockReason : String())
                    + (v.inert || v.structural ? String() : selectionRuleHelp (presenter))
                    + (v.inert ? String() : showInLineHelp()));
        repaint();
    }

    juce::Rectangle<int> Knob::captionArea() const { return getLocalBounds().removeFromBottom (32).removeFromTop (16); }

    void Knob::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto text = r.removeFromBottom (32.0f);

        // La pastille de couleur du paramètre, à gauche du nom (6a).
        if (! view.inert)
            glyph::swatch (g, juce::Rectangle<float> (text.getX() + 6.0f, text.getY() + 4.0f, 8.0f, 8.0f),
                           glyph::paramColour (view.index), shown);
        auto dial = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()),
                                             juce::jmin (r.getWidth(), r.getHeight())).reduced (8.0f);

        // Halo : ce que la génération peut atteindre. Absent si verrouillé — la
        // génération ne peut plus y bouger, et ça se voit sans lire (d-3).
        if (! view.locked && ! view.structural && ! view.inert)
        {
            arc (g, dial.expanded (6.0f), angleFor (view.min), angleFor (view.max),
                 juce::Colours::white.withAlpha (0.14f), 5.0f);
            arc (g, dial.expanded (6.0f), angleFor (view.effLo), angleFor (view.effHi),
                 juce::Colour (0xff7fb3d5).withAlpha (0.55f), 5.0f);
        }

        // Course complète, puis la portion parcourue.
        arc (g, dial, kStart, kStart + kSpan, juce::Colours::white.withAlpha (0.16f), 3.0f);
        arc (g, dial, kStart, angleFor (view.raw),
             juce::Colours::white.withAlpha (view.inert ? 0.2f : 0.75f), 3.0f);

        // Trait fin de la BASE : d'où part le pas quand il ne porte pas de valeur.
        {
            const float a = angleFor (view.base);
            const float cx = dial.getCentreX(), cy = dial.getCentreY();
            const float ro = dial.getWidth() * 0.5f + 2.0f, ri = dial.getWidth() * 0.5f - 5.0f;
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawLine (cx + std::sin (a) * ri, cy - std::cos (a) * ri,
                        cx + std::sin (a) * ro, cy - std::cos (a) * ro, 1.0f);
        }

        // Point de la valeur affichée du pas sélectionné (c-6).
        {
            const float a = angleFor (view.raw);
            const float rr = dial.getWidth() * 0.5f - 9.0f;
            const float px = dial.getCentreX() + std::sin (a) * rr;
            const float py = dial.getCentreY() - std::cos (a) * rr;
            g.setColour (juce::Colours::white.withAlpha (view.inert ? 0.25f : 0.95f));
            g.fillEllipse (px - 3.0f, py - 3.0f, 6.0f, 6.0f);
        }

        if (view.locked || view.structural)
            glyph::padlock (g, dial.withSizeKeepingCentre (14.0f, 16.0f),
                            juce::Colours::white.withAlpha (0.55f), true);

        g.setColour (juce::Colours::white.withAlpha (view.inert ? 0.35f : 0.85f));
        g.setFont (13.0f);
        g.drawText (caption, text.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred, true);
        g.setColour (juce::Colours::white.withAlpha (view.inert ? 0.3f : 0.65f));
        g.setFont (12.0f);
        g.drawText (value, text.toNearestInt(), juce::Justification::centred, true);
    }

    void Knob::send (float raw)
    {
        const float v = juce::jlimit (0.0f, 1.0f, raw);
        if (onSteps) presenter.setStepsExplicit (slot1, presenter.firstSelectedStep(), presenter.lastSelectedStep(), view.name, v);
        else         presenter.setParam (gridId, v);
    }

    void Knob::mouseDown (const juce::MouseEvent& e)
    {
        if (view.inert) return;
        if (captionArea().contains (e.getPosition())) { presenter.showParam (slot1, view.name); return; }
        if (view.structural) return;
        gestureStart = view.raw;
        gesturing = true;
        // Un geste = UNE transaction nommée. Sélection partielle : elle porte sur les pas
        // (correction 4, geste A) ; ligne entière : sur la base, comme avant.
        onSteps = presenter.selectionIsPartial();
        if (onSteps) presenter.beginStepsGesture (slot1, presenter.firstSelectedStep(), presenter.lastSelectedStep(), view.name);
        else         presenter.beginGesture (gridId);
    }

    void Knob::mouseDrag (const juce::MouseEvent& e)
    {
        if (! gesturing) return;
        send (gestureStart - (float) e.getDistanceFromDragStartY() / kDragSpan);
    }

    void Knob::mouseUp (const juce::MouseEvent&)
    {
        if (! gesturing) return;
        gesturing = false;
        if (onSteps) presenter.endStepsGesture();
        else         presenter.endGesture (gridId);
    }

    void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (view.inert || view.structural || gesturing) return;
        onSteps = presenter.selectionIsPartial();   // hors geste : une transaction par cran
        send (view.raw + (w.deltaY >= 0.0f ? kFine : -kFine));
    }

    //==========================================================================
    ParamRow::ParamRow (Presenter& p, int slot, int m)
        : presenter (p), slot1 (slot), modulable (m)
    {
    }

    void ParamRow::setView (const ParamView& v, int slot, bool shownInLine)
    {
        slot1 = slot;
        shown = shownInLine;
        view = v;
        gridId = slotGridId (slot, v.name.toRawUTF8());
        caption = v.label;
        value = v.valueText;
        lockTag = Format::lockWord (v.locked, v.lockedByDefault, v.structural);
        // Mesuré ICI, à la notification, jamais dans paint() : le mot du cadenas ne
        // s'écrit que s'il tient à droite du libellé sans le recouvrir. Sinon le cadenas
        // et l'aide au survol portent seuls l'information (§3.3.1 : visible sans chercher).
        showTag = false;
        if (lockTag.isNotEmpty())
        {
            const juce::Font captionFont (juce::FontOptions (12.0f)), tagFont (juce::FontOptions (10.0f));
            showTag = juce::GlyphArrangement::getStringWidthInt (captionFont, caption)
                        + juce::GlyphArrangement::getStringWidthInt (tagFont, lockTag) + 8 <= kCaptionW;
        }
        setTooltip (v.help + (v.lockReason.isNotEmpty() ? "\n" + v.lockReason : String())
                    + (v.inert || v.structural ? String() : selectionRuleHelp (presenter))
                    + (v.inert ? String() : showInLineHelp()));
        repaint();
    }

    juce::Rectangle<int> ParamRow::lockArea() const { return getLocalBounds().removeFromLeft (kLockW); }

    juce::Rectangle<int> ParamRow::captionArea() const
    {
        return getLocalBounds().withTrimmedLeft (kLockW + 2).withWidth (kCaptionW);
    }

    juce::Rectangle<int> ParamRow::valueArea() const { return getLocalBounds().removeFromRight (kValueW); }

    juce::Rectangle<int> ParamRow::sliderArea() const
    {
        const auto r = getLocalBounds().withTrimmedLeft (kLockW + 2 + kCaptionW + kGap)
                                       .withTrimmedRight (kValueW + kGap).reduced (0, 7);
        return r.getWidth() >= kMinSlider ? r : juce::Rectangle<int>();
    }

    void ParamRow::paint (juce::Graphics& g)
    {
        const auto ink = juce::Colours::white.withAlpha (view.inert ? 0.3f : 0.85f);

        if (! view.inert)
            glyph::padlock (g, lockArea().toFloat().reduced (3.0f),
                            juce::Colours::white.withAlpha (view.locked || view.structural ? 0.8f : 0.3f),
                            view.locked || view.structural);

        // La pastille de couleur du paramètre (6a), puis le nom.
        if (! view.inert)
            glyph::swatch (g, captionArea().toFloat().removeFromLeft (10.0f).withSizeKeepingCentre (8.0f, 8.0f),
                           glyph::paramColour (view.index), shown);
        g.setColour (ink);
        g.setFont (12.0f);
        g.drawText (caption, captionArea().withTrimmedLeft (12), juce::Justification::centredLeft, true);

        if (showTag)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (10.0f);
            g.drawText (lockTag, captionArea(), juce::Justification::centredRight, false);
        }

        const auto s = sliderArea();
        if (! s.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (s.toFloat(), 2.0f);
        }

        if (! view.inert && ! s.isEmpty())
        {
            // Halo : la plage de génération, puis la plage effective à la densité courante.
            if (! view.locked && ! view.structural)
            {
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.fillRect (s.getX() + (int) (s.getWidth() * view.min), s.getY(),
                            (int) (s.getWidth() * (view.max - view.min)), s.getHeight());
                g.setColour (juce::Colour (0xff7fb3d5).withAlpha (0.35f));
                g.fillRect (s.getX() + (int) (s.getWidth() * view.effLo), s.getY(),
                            (int) (s.getWidth() * (view.effHi - view.effLo)), s.getHeight());
            }

            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (s.getX(), s.getY(), (int) (s.getWidth() * juce::jlimit (0.0f, 1.0f, view.raw)), s.getHeight());

            // Trait fin de la base.
            const int bx = s.getX() + (int) (s.getWidth() * juce::jlimit (0.0f, 1.0f, view.base));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (bx, s.getY() - 2, 1, s.getHeight() + 4);
        }

        g.setColour (juce::Colours::white.withAlpha (view.inert ? 0.3f : 0.7f));
        g.setFont (12.0f);
        g.drawText (value, valueArea(), juce::Justification::centredLeft, true);
    }

    void ParamRow::sendFromX (int x)
    {
        const auto s = sliderArea();
        if (s.isEmpty()) return;
        const float v = juce::jlimit (0.0f, 1.0f, (float) (x - s.getX()) / (float) s.getWidth());
        if (onSteps) presenter.setStepsExplicit (slot1, presenter.firstSelectedStep(), presenter.lastSelectedStep(), view.name, v);
        else         presenter.setParam (gridId, v);
    }

    void ParamRow::mouseDown (const juce::MouseEvent& e)
    {
        if (view.inert) return;

        // Le cadenas : bascule. Le structurel ne bascule pas — son aide dit pourquoi.
        if (lockArea().contains (e.getPosition()))
        {
            if (! view.structural) presenter.setLocked (slot1, view.name, ! view.locked);
            return;
        }
        // Le nom : la ligne du séquenceur montre ce paramètre (6a).
        if (captionArea().contains (e.getPosition())) { presenter.showParam (slot1, view.name); return; }
        const auto s = sliderArea();
        if (s.isEmpty() || ! s.expanded (0, 6).contains (e.getPosition())) return;

        gesturing = true;
        // Même règle que le knob (correction 4) : sélection partielle → les pas, sinon la base.
        onSteps = presenter.selectionIsPartial();
        if (onSteps) presenter.beginStepsGesture (slot1, presenter.firstSelectedStep(), presenter.lastSelectedStep(), view.name);
        else         presenter.beginGesture (gridId);
        sendFromX (e.x);
    }

    void ParamRow::mouseDrag (const juce::MouseEvent& e) { if (gesturing) sendFromX (e.x); }

    void ParamRow::mouseUp (const juce::MouseEvent&)
    {
        if (! gesturing) return;
        gesturing = false;
        if (onSteps) presenter.endStepsGesture();
        else         presenter.endGesture (gridId);
    }

    //==========================================================================
    PlugControls::PlugControls (Presenter& p)
        : presenter (p), main (p)
    {
        title.setJustificationType (juce::Justification::centredLeft);
        title.setInterceptsMouseClicks (false, false);
        title.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (title);

        emptyHint.setJustificationType (juce::Justification::centred);
        emptyHint.setInterceptsMouseClicks (false, false);
        emptyHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
        addChildComponent (emptyHint);

        chooseButton.setButtonText ("Choisir un effet"_fr);
        chooseButton.setWantsKeyboardFocus (false);
        chooseButton.onClick = [this] { if (onChooseSkill) onChooseSkill (presenter.selectedSlot()); };
        addChildComponent (chooseButton);

        addAndMakeVisible (main);

        for (int m = 1; m <= 6; ++m)                       // paramA..paramF
            rows.push_back (std::make_unique<ParamRow> (p, 1, m));
        rows.push_back (std::make_unique<ParamRow> (p, 1, 9));    // stereo
        for (int m = 10; m <= 12; ++m)                    // res1..res3
            rows.push_back (std::make_unique<ParamRow> (p, 1, m));
        for (auto& r : rows) addAndMakeVisible (*r);

        mixRow  = std::make_unique<ParamRow> (p, 1, 7);
        gainRow = std::make_unique<ParamRow> (p, 1, 8);
        addAndMakeVisible (*mixRow);
        addAndMakeVisible (*gainRow);

        for (auto* s : { &glide, &fade })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
            s->setRange (0.0, 1.0, 0.0);
            s->setWantsKeyboardFocus (false);
            addAndMakeVisible (*s);
        }
        glide.setTooltip (Format::slotSettingHelp ("glide"));
        fade.setTooltip (Format::slotSettingHelp ("fade"));
        glide.onDragStart = [this] { presenter.beginGesture (slotGridId (presenter.selectedSlot(), "glide")); };
        glide.onDragEnd   = [this] { presenter.endGesture (slotGridId (presenter.selectedSlot(), "glide")); };
        fade.onDragStart  = [this] { presenter.beginGesture (slotGridId (presenter.selectedSlot(), "fade")); };
        fade.onDragEnd    = [this] { presenter.endGesture (slotGridId (presenter.selectedSlot(), "fade")); };
        glide.onValueChange = [this] { presenter.setParam (slotGridId (presenter.selectedSlot(), "glide"), (float) glide.getValue()); };
        fade.onValueChange  = [this] { presenter.setParam (slotGridId (presenter.selectedSlot(), "fade"),  (float) fade.getValue()); };

        for (auto* l : { &glideLabel, &fadeLabel })
        {
            l->setJustificationType (juce::Justification::centredLeft);
            l->setInterceptsMouseClicks (false, false);
            l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
            addAndMakeVisible (*l);
        }
        glideLabel.setText (Format::slotSettingLabel ("glide"), juce::dontSendNotification);
        fadeLabel.setText (Format::slotSettingLabel ("fade"), juce::dontSendNotification);

        activeButton.setClickingTogglesState (true);
        activeButton.setButtonText (Format::slotSettingLabel ("active"));
        activeButton.setTooltip (Format::slotSettingHelp ("active"));
        activeButton.onClick = [this] { presenter.setActive (presenter.selectedSlot(), activeButton.getToggleState()); };

        tailCut.setButtonText ("coupée"_fr);
        tailRing.setButtonText ("laissée mourir"_fr);
        tailCut.setTooltip (Format::slotSettingHelp ("tail"));
        tailRing.setTooltip (Format::slotSettingHelp ("tail"));
        tailCut.onClick  = [this] { presenter.setTail (presenter.selectedSlot(), false); };
        tailRing.onClick = [this] { presenter.setTail (presenter.selectedSlot(), true); };

        for (auto* b : { &activeButton, &tailCut, &tailRing })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }

        refresh();
    }

    PlugControls::~PlugControls() = default;

    void PlugControls::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    void PlugControls::resized()
    {
        auto r = getLocalBounds().reduced (10, 8);
        title.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);

        auto bottom = r.removeFromBottom (58);
        auto settings = bottom.removeFromTop (26);
        activeButton.setBounds (settings.removeFromLeft (80));
        settings.removeFromLeft (8);
        tailCut.setBounds (settings.removeFromLeft (90));
        settings.removeFromLeft (4);
        tailRing.setBounds (settings.removeFromLeft (130));

        auto times = bottom;
        auto left = times.removeFromLeft (times.getWidth() / 2);
        glideLabel.setBounds (left.removeFromLeft (90));
        glide.setBounds (left);
        fadeLabel.setBounds (times.removeFromLeft (70));
        fade.setBounds (times);

        auto dial = r.removeFromLeft (150);
        main.setBounds (dial.removeFromTop (juce::jmin (170, dial.getHeight())));
        r.removeFromLeft (10);

        emptyHint.setBounds (r.removeFromTop (40));
        chooseButton.setBounds (emptyHint.getBounds().withSizeKeepingCentre (160, 26));

        const int rowH = 22;
        for (auto& row : rows)
        {
            if (r.getHeight() < rowH) { row->setBounds ({}); continue; }
            row->setBounds (r.removeFromTop (rowH));
        }
        if (r.getHeight() >= rowH) mixRow->setBounds (r.removeFromTop (rowH));
        if (r.getHeight() >= rowH) gainRow->setBounds (r.removeFromTop (rowH));
    }

    void PlugControls::applySlotSettings (const SlotView& v)
    {
        activeButton.setToggleState (v.active, juce::dontSendNotification);
        tailCut.setToggleState (! v.tailRing, juce::dontSendNotification);
        tailRing.setToggleState (v.tailRing, juce::dontSendNotification);
        glide.setValue (v.glide, juce::dontSendNotification);
        fade.setValue (v.fade, juce::dontSendNotification);
    }

    void PlugControls::refresh()
    {
        const int slot1 = presenter.selectedSlot();
        const auto v = presenter.slotView (slot1);

        title.setText (String (slot1) + " · "_fr + (v.present ? v.skillLabel : "—"_fr), juce::dontSendNotification);

        const bool empty = ! v.present;
        emptyHint.setVisible (empty);
        chooseButton.setVisible (empty);
        if (empty) emptyHint.setText (Format::emptySlotText(), juce::dontSendNotification);

        // Ce que la ligne montre (6a) : lu UNE fois, puis chaque contrôle sait si c'est lui.
        const auto lv = presenter.lineView (slot1);
        const auto shownIn = [&lv] (const ParamView& p) { return lv.modeB && lv.shownParam == p.name; };

        main.setView (v.params[0], slot1, shownIn (v.params[0]));   // grid::kModulable[0] = « main »
        for (size_t i = 0; i < rows.size(); ++i)
        {
            static constexpr int kIndices[] = { 1, 2, 3, 4, 5, 6, 9, 10, 11, 12 };
            const auto& p = v.params[(size_t) kIndices[i]];
            rows[i]->setView (p, slot1, shownIn (p));
            rows[i]->setVisible (! empty);
        }
        mixRow->setView (v.params[7], slot1, shownIn (v.params[7]));    // mix et gain : le socle les compose
        gainRow->setView (v.params[8], slot1, shownIn (v.params[8]));   // toujours, quelle que soit la skill

        applySlotSettings (v);
    }
}
