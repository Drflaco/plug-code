#include "PlugGlyphs.h"
#include <cmath>

namespace plug::ui::v1::glyph
{
    void chevronDown (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
    {
        const auto r = area.withSizeKeepingCentre (juce::jmin (9.0f, area.getWidth()),
                                                    juce::jmin (5.0f, area.getHeight()));
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY());
        p.lineTo (r.getCentreX(), r.getBottom());
        p.lineTo (r.getRight(), r.getY());
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    void gear (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
    {
        const auto r = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()),
                                                   juce::jmin (area.getWidth(), area.getHeight()))
                           .reduced (1.0f);
        const float cx = r.getCentreX(), cy = r.getCentreY();
        const float outer = r.getWidth() * 0.5f;
        const float inner = outer * 0.58f;

        g.setColour (c);

        // Huit dents : des segments radiaux, moins coûteux et plus nets qu'un Path denté.
        for (int i = 0; i < 8; ++i)
        {
            const float a = juce::MathConstants<float>::pi * 0.25f * (float) i;
            const float s = std::sin (a), co = std::cos (a);
            g.drawLine (cx + co * inner, cy + s * inner, cx + co * outer, cy + s * outer, 1.6f);
        }

        g.drawEllipse (cx - inner * 0.85f, cy - inner * 0.85f, inner * 1.7f, inner * 1.7f, 1.4f);
        g.drawEllipse (cx - inner * 0.34f, cy - inner * 0.34f, inner * 0.68f, inner * 0.68f, 1.2f);
    }

    void warning (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
    {
        const auto r = area.withSizeKeepingCentre (juce::jmin (11.0f, area.getWidth()),
                                                   juce::jmin (10.0f, area.getHeight()));
        juce::Path p;
        p.startNewSubPath (r.getCentreX(), r.getY());
        p.lineTo (r.getRight(), r.getBottom());
        p.lineTo (r.getX(), r.getBottom());
        p.closeSubPath();
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.3f));
        g.fillRect (r.getCentreX() - 0.6f, r.getY() + r.getHeight() * 0.35f, 1.2f, r.getHeight() * 0.35f);
        g.fillRect (r.getCentreX() - 0.6f, r.getBottom() - r.getHeight() * 0.18f, 1.2f, 1.2f);
    }

    void padlock (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c, bool closed)
    {
        const auto r = area.withSizeKeepingCentre (juce::jmin (10.0f, area.getWidth()),
                                                   juce::jmin (12.0f, area.getHeight()));
        const float bodyH = r.getHeight() * 0.55f;
        const auto body = juce::Rectangle<float> (r.getX(), r.getBottom() - bodyH, r.getWidth(), bodyH);

        g.setColour (c);
        g.drawRoundedRectangle (body, 1.5f, 1.2f);

        // L'anse : verticale quand c'est fermé, penchée et décrochée quand c'est ouvert.
        const float archW = r.getWidth() * 0.62f;
        const float archX = closed ? body.getCentreX() - archW * 0.5f : body.getCentreX() - archW * 0.1f;
        juce::Path arch;
        arch.addArc (archX, r.getY(), archW, bodyH * 1.1f,
                     juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::pi * 2.5f, true);
        g.strokePath (arch, juce::PathStrokeType (1.2f));
    }
}

namespace plug::ui::v1
{
    IconButton::IconButton (const juce::String& componentName, Icon iconToDraw)
        : juce::Button (componentName), icon (iconToDraw)
    {
        setWantsKeyboardFocus (false);   // le clavier reste à l'hôte et à l'éditeur
    }

    // `newText` et non `text` : juce::Button a déjà un membre `text`, et le masquer
    // rend le code ambigu à la relecture (C4458).
    void IconButton::setLabelText (const juce::String& newText)
    {
        if (labelText == newText) return;
        labelText = newText;
        repaint();
    }

    void IconButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float alpha = isDown ? 0.22f : (isOver ? 0.16f : 0.09f);

        g.setColour (juce::Colours::white.withAlpha (isEnabled() ? alpha : 0.05f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (isEnabled() ? 0.28f : 0.12f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        const auto ink = juce::Colours::white.withAlpha (isEnabled() ? 0.9f : 0.35f);
        auto content = r.reduced (8.0f, 0.0f);

        if (icon == Icon::ChevronDown)
        {
            glyph::chevronDown (g, content.removeFromRight (14.0f), ink);
            content.removeFromRight (4.0f);
        }
        else if (icon == Icon::Gear)
        {
            glyph::gear (g, r.reduced (5.0f), ink);
            return;                                  // un engrenage seul, sans texte
        }

        if (labelText.isNotEmpty())
        {
            g.setColour (ink);
            g.setFont (13.0f);
            g.drawText (labelText, content.toNearestInt(), juce::Justification::centredLeft, true);
        }
    }
}

namespace plug::ui::v1::glyph
{
    juce::Colour paramColour (int index)
    {
        static const juce::uint32 palette[13] =
        {
            0xff7fb3d5,   // main   — bleu, la teinte du halo
            0xffe2b34a,   // paramA — ambre
            0xff8fd18f,   // paramB — vert
            0xffd98fd1,   // paramC — mauve
            0xfff28c73,   // paramD — corail
            0xff79d3c9,   // paramE — turquoise
            0xffc9c26f,   // paramF — olive
            0xffc8c8c8,   // mix    — gris clair
            0xffd0a884,   // gain   — sable
            0xffa3a8e6,   // stereo — lavande
            0xff8a8a8a, 0xff8a8a8a, 0xff8a8a8a   // res1..3 — réserve, gris
        };
        return juce::Colour (palette[(size_t) juce::jlimit (0, 12, index)]);
    }

    void swatch (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool lit)
    {
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        const auto c = area.withSizeKeepingCentre (d, d);
        if (lit)
        {
            g.setColour (colour);
            g.fillEllipse (c);
        }
        else
        {
            g.setColour (colour.withAlpha (0.55f));
            g.drawEllipse (c.reduced (0.5f), 1.0f);
        }
    }
}
