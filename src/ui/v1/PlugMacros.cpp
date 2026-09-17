#include "PlugMacros.h"
#include "../Format.h"
#include <cmath>

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        constexpr float kStart = juce::MathConstants<float>::pi * 1.25f;
        constexpr float kSpan  = juce::MathConstants<float>::pi * 1.5f;
        constexpr float kFine  = 0.02f;
        constexpr float kDragSpan = 150.0f;
    }

    //==========================================================================
    PlugMacros::MacroKnob::MacroKnob (Presenter& p, int macro1)
        : presenter (p), gridId ("macro" + String (macro1)), caption ("M" + String (macro1))
    {
    }

    void PlugMacros::MacroKnob::setValue (float v, const String& valueText)
    {
        raw = v;
        value = valueText;                     // composé ici, jamais dans paint()
        repaint();
    }

    void PlugMacros::MacroKnob::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto text = r.removeFromBottom (14.0f);
        auto dial = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()),
                                             juce::jmin (r.getWidth(), r.getHeight())).reduced (5.0f);

        juce::Path track, done;
        track.addCentredArc (dial.getCentreX(), dial.getCentreY(), dial.getWidth() * 0.5f, dial.getHeight() * 0.5f,
                             0.0f, kStart, kStart + kSpan, true);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.strokePath (track, juce::PathStrokeType (3.0f));

        const float a = kStart + kSpan * juce::jlimit (0.0f, 1.0f, raw);
        if (a > kStart)
        {
            done.addCentredArc (dial.getCentreX(), dial.getCentreY(), dial.getWidth() * 0.5f, dial.getHeight() * 0.5f,
                                0.0f, kStart, a, true);
            g.setColour (juce::Colour (0xff7fb3d5).withAlpha (0.9f));
            g.strokePath (done, juce::PathStrokeType (3.0f));
        }

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (11.0f);
        g.drawText (caption, text.toNearestInt(), juce::Justification::centred, false);
    }

    void PlugMacros::MacroKnob::mouseDown (const juce::MouseEvent&)
    {
        gestureStart = raw;
        gesturing = true;
        presenter.beginGesture (gridId);       // un geste = UNE transaction nommée
    }

    void PlugMacros::MacroKnob::mouseDrag (const juce::MouseEvent& e)
    {
        if (! gesturing) return;
        presenter.setParam (gridId, juce::jlimit (0.0f, 1.0f,
                                                  gestureStart - (float) e.getDistanceFromDragStartY() / kDragSpan));
    }

    void PlugMacros::MacroKnob::mouseUp (const juce::MouseEvent&)
    {
        if (! gesturing) return;
        gesturing = false;
        presenter.endGesture (gridId);
    }

    void PlugMacros::MacroKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        presenter.setParam (gridId, juce::jlimit (0.0f, 1.0f, raw + (w.deltaY >= 0.0f ? kFine : -kFine)));
    }

    //==========================================================================
    PlugMacros::PlugMacros (Presenter& p) : presenter (p)
    {
        for (int m = 1; m <= kMacros; ++m)
        {
            auto k = std::make_unique<MacroKnob> (p, m);
            addAndMakeVisible (*k);
            knobs[(size_t) (m - 1)] = std::move (k);
        }
        refresh();
    }

    void PlugMacros::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (11.0f);
        g.drawText ("Macros"_fr, getLocalBounds().reduced (8, 3), juce::Justification::topLeft, false);
    }

    void PlugMacros::resized()
    {
        auto r = getLocalBounds().reduced (8, 4).withTrimmedTop (12);
        const int w = juce::jmax (1, r.getWidth() / kMacros);
        for (auto& k : knobs)
            if (k != nullptr) k->setBounds (r.removeFromLeft (w));
    }

    void PlugMacros::refresh()
    {
        const auto views = presenter.macroViews();
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            if (knobs[i] == nullptr) continue;
            const auto& v = views[i];
            knobs[i]->setValue (v.value, Format::rawText (v.value));

            // Les routes, en toutes lettres et en lecture seule (Q2). Les éditer est
            // annoncé dans AVENIR.md : une absence annoncée n'est pas un défaut.
            knobs[i]->setTooltip ("Macro "_fr + String (v.macro1) + " : "_fr + Format::rawText (v.value)
                                      + "\n" + (v.routes.isNotEmpty() ? v.routes : "aucune route"_fr)
                                      + "\nÉdition des routes : à venir."_fr);
        }
    }
}
