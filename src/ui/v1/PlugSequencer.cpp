#include "PlugSequencer.h"

#ifndef PLUG_UI_TIMING
 #define PLUG_UI_TIMING 0
#endif

#if PLUG_UI_TIMING
 #include <algorithm>
#endif

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        constexpr int kRowGap = 2;
        constexpr int kCellGap = 2;
        constexpr int kMinRowHeight = 12;

        juce::Colour modeInk (StepModeView m)
        {
            switch (m)
            {
                case StepModeView::Generated: return juce::Colour (0xff7fb3d5);
                case StepModeView::Explicit:  return juce::Colour (0xffe2b34a);
                case StepModeView::Base:      break;
            }
            return juce::Colours::white;
        }
    }

    //==========================================================================
    PlugSequencer::PlugSequencer (Presenter& p) : presenter (p)
    {
        setTooltip ("Séquenceur : une ligne par emplacement, 32 pas.\n"
                    "Clic = sélectionner un pas · Maj+clic ou glisser = une plage\n"
                    "Clic droit = activer ou éteindre le pas\n"
                    "A/B au bout de la ligne = motif ou valeurs ; le chevron choisit le paramètre montré."_fr);
        refresh();
    }

   #if PLUG_UI_TIMING
    PlugSequencer::~PlugSequencer()
    {
        if (paintUs.size() < 10) return;
        auto v = paintUs;
        std::sort (v.begin(), v.end());
        const auto pick = [&v] (double q) { return v[(size_t) juce::jlimit (0, (int) v.size() - 1,
                                                                            (int) (q * (double) v.size())) ]; };
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("LascauxLab").getChildFile ("Plug").getChildFile ("measure");
        dir.createDirectory();
        String out;
        out << "plug J4b — rendu du séquenceur\n"
            << "frames : " << (int) v.size() << "\n"
            << "p50 : " << String (pick (0.50), 3) << " us\n"
            << "p99 : " << String (pick (0.99), 3) << " us\n"
            << "max : " << String (v.back(), 3) << " us\n";
        dir.getChildFile ("j4b_ui_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".txt")
           .replaceWithText (out);
    }
   #endif

    //==========================================================================
    int PlugSequencer::rowAt (int y) const
    {
        for (size_t r = 0; r + 1 < rowY.size(); ++r)
            if (y >= rowY[r] && y < rowY[r + 1]) return (int) r;
        return -1;
    }

    int PlugSequencer::stepAt (int x) const
    {
        for (size_t s = 0; s + 1 < columnX.size(); ++s)
            if (x >= columnX[s] && x < columnX[s + 1]) return (int) s;
        return -1;
    }

    juce::Rectangle<int> PlugSequencer::cellBounds (int row, int step) const
    {
        if (row < 0 || row + 1 >= (int) rowY.size() || step < 0 || step + 1 >= (int) columnX.size()) return {};
        return { columnX[(size_t) step], rowY[(size_t) row],
                 columnX[(size_t) step + 1] - columnX[(size_t) step] - kCellGap,
                 rowY[(size_t) row + 1] - rowY[(size_t) row] - kRowGap };
    }

    juce::Rectangle<int> PlugSequencer::columnBounds (int step) const
    {
        if (step < 0 || step + 1 >= (int) columnX.size() || rowY.size() < 2) return {};
        return { columnX[(size_t) step] - kCellGap, rowY.front(),
                 columnX[(size_t) step + 1] - columnX[(size_t) step] + kCellGap, rowY.back() - rowY.front() };
    }

    juce::Rectangle<int> PlugSequencer::modeButtonBounds (int row) const
    {
        if (row < 0 || row + 1 >= (int) rowY.size()) return {};
        return { getWidth() - tailWidth() + 2, rowY[(size_t) row], compact ? 16 : 24,
                 rowY[(size_t) row + 1] - rowY[(size_t) row] - kRowGap };
    }

    juce::Rectangle<int> PlugSequencer::paramButtonBounds (int row) const
    {
        if (row < 0 || row + 1 >= (int) rowY.size()) return {};
        return { getWidth() - tailWidth() + (compact ? 20 : 28), rowY[(size_t) row], compact ? 12 : 20,
                 rowY[(size_t) row + 1] - rowY[(size_t) row] - kRowGap };
    }

    void PlugSequencer::composeLabels()
    {
        // En large : « 3  FM ». En compact : « 3 ». Recomposé quand le mode change,
        // jamais à chaque frame.
        for (size_t r = 0; r < rows.size(); ++r)
            rows[r].label = compact ? String ((int) r + 1) : String ((int) r + 1) + "  " + rows[r].name;
    }

    //==========================================================================
    void PlugSequencer::resized()
    {
        // Toute la géométrie est calculée ICI, une fois. paint() ne fait que la lire :
        // c'est ce qui rend la règle « aucune allocation par frame » tenable.
        const int n = (int) rows.size();
        rowY.assign ((size_t) juce::jmax (0, n) + 1, 0);
        columnX.assign (kSteps + 1, 0);

        auto area = getLocalBounds().reduced (6, 6);

        // Le mode compact du schéma §2 : sous cette largeur, une case ne peut plus
        // porter que sa présence. On enlève alors le nom de l'effet, pas des pas.
        const bool wantCompact = area.getWidth() < 640;
        if (wantCompact != compact) { compact = wantCompact; composeLabels(); }

        const int top = area.getY();
        const int h = n > 0 ? juce::jmax (kMinRowHeight, area.getHeight() / n) : 0;
        for (int r = 0; r <= n; ++r) rowY[(size_t) r] = top + r * h;

        const int x0 = area.getX() + labelWidth();
        const int width = juce::jmax (kSteps, area.getRight() - tailWidth() - x0);
        for (int s = 0; s <= kSteps; ++s)
            columnX[(size_t) s] = x0 + (int) ((juce::int64) width * s / kSteps);
    }

    //==========================================================================
    void PlugSequencer::refresh()
    {
        const int shown = presenter.displayedSlots();
        selectedSlot = presenter.selectedSlot();
        selFirst = presenter.firstSelectedStep();
        selLast = presenter.lastSelectedStep();

        const bool countChanged = (int) rows.size() != shown;
        rows.assign ((size_t) shown, {});
        cells.assign ((size_t) shown * kSteps, {});

        for (int r = 0; r < shown; ++r)
        {
            const int slot1 = r + 1;
            const auto sv = presenter.slotView (slot1);
            const auto lv = presenter.lineView (slot1);
            auto& row = rows[(size_t) r];

            row.name = sv.present ? sv.skillLabel : "—"_fr;
            row.modeB = lv.modeB;
            row.present = sv.present;
            row.selected = (slot1 == selectedSlot);

            const int shownIndex = juce::jmax (0, [&sv, &lv]
            {
                for (int m = 0; m < kSlotParams; ++m)
                    if (sv.params[(size_t) m].name == lv.shownParam) return m;
                return 0;
            }());
            row.paramLabel = sv.params[(size_t) shownIndex].label;

            for (int s = 0; s < kSteps; ++s)
            {
                auto& c = cells[(size_t) r * kSteps + (size_t) s];
                const auto& st = lv.steps[(size_t) s];
                c.on = st.on;
                c.mode = st.mode;
                c.value = st.value;
                c.hasValue = st.hasValue;
                c.beyondLength = (s >= lv.length);
            }
        }

        composeLabels();                 // composé ICI, jamais dans paint()
        if (countChanged) resized();
        repaint();
    }

    void PlugSequencer::refreshSelection()
    {
        const int slot = presenter.selectedSlot();
        const int first = presenter.firstSelectedStep();
        const int last = presenter.lastSelectedStep();
        if (slot == selectedSlot && first == selFirst && last == selLast) return;

        for (auto& row : rows) row.selected = false;
        selectedSlot = slot; selFirst = first; selLast = last;
        if (slot >= 1 && slot <= (int) rows.size()) rows[(size_t) slot - 1].selected = true;
        repaint();
    }

    void PlugSequencer::setTransport (const TransportView& t)
    {
        if (t.step == playhead) return;
        const int old = playhead;
        playhead = t.step;
        // Seules les deux colonnes concernées se repeignent : à 30 Hz, repeindre la
        // grille entière pour une tête qui avance d'un pas serait du gaspillage pur.
        if (old >= 0) repaint (columnBounds (old));
        if (playhead >= 0) repaint (columnBounds (playhead));
    }

    //==========================================================================
    void PlugSequencer::paint (juce::Graphics& g)
    {
       #if PLUG_UI_TIMING
        t0 = juce::Time::getHighResolutionTicks();
       #endif

        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        const int n = (int) rows.size();
        if (n == 0 || columnX.size() < 2) return;

        // Tête de lecture : une colonne, sous les cases.
        if (playhead >= 0 && playhead < kSteps)
        {
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.fillRect (columnBounds (playhead));
        }

        g.setFont (11.0f);
        for (int row = 0; row < n; ++row)
        {
            const auto& info = rows[(size_t) row];
            const int y = rowY[(size_t) row];
            const int h = rowY[(size_t) row + 1] - y - kRowGap;

            g.setColour (juce::Colours::white.withAlpha (info.selected ? 0.9f : (info.present ? 0.6f : 0.35f)));
            g.drawText (info.label, getLocalBounds().getX() + 8, y, labelWidth() - 10, h,
                        juce::Justification::centredLeft, true);

            for (int s = 0; s < kSteps; ++s)
            {
                const auto& c = cells[(size_t) row * kSteps + (size_t) s];
                const auto b = cellBounds (row, s);
                if (b.isEmpty()) continue;

                if (c.beyondLength)
                {
                    // Au-delà de seq.length : la boucle n'y passe jamais.
                    g.setColour (juce::Colours::white.withAlpha (0.05f));
                    g.fillRect (b);
                    continue;
                }

                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.fillRect (b);

                if (! c.on)
                {
                    // Mode A comme B : un pas éteint est un point, pas une barre.
                    g.setColour (juce::Colours::white.withAlpha (0.28f));
                    g.fillRect (b.getCentreX() - 1, b.getCentreY() - 1, 2, 2);
                }
                else if (info.modeB)
                {
                    // Mode B : la hauteur DIT la valeur affichée du pas (fonction pure).
                    const int fill = juce::jmax (1, (int) ((float) b.getHeight()
                                                            * juce::jlimit (0.0f, 1.0f, c.value)));
                    g.setColour (modeInk (c.mode).withAlpha (0.75f));
                    g.fillRect (b.getX(), b.getBottom() - fill, b.getWidth(), fill);
                }
                else
                {
                    g.setColour (juce::Colours::white.withAlpha (info.present ? 0.7f : 0.4f));
                    g.fillRect (b.reduced (1));
                }

                // Badge de mode : deux pixels dans le coin, discrets mais lisibles.
                if (c.mode != StepModeView::Base)
                {
                    g.setColour (modeInk (c.mode));
                    g.fillRect (b.getRight() - 3, b.getY() + 1, 2, 2);
                }

                // Sélection : un contour, sur la ligne de l'emplacement sélectionné.
                if (info.selected && s + 1 >= juce::jmin (selFirst, selLast) && s + 1 <= juce::jmax (selFirst, selLast))
                {
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.drawRect (b, 1);
                }
            }

            // Bouton A/B et chevron du paramètre montré : tracés, jamais des glyphes.
            const auto mb = modeButtonBounds (row);
            g.setColour (juce::Colours::white.withAlpha (info.modeB ? 0.8f : 0.35f));
            g.drawRect (mb, 1);
            g.drawText (info.modeB ? "B" : "A", mb, juce::Justification::centred, false);
            glyph::chevronDown (g, paramButtonBounds (row).toFloat(), juce::Colours::white.withAlpha (0.45f));
        }

       #if PLUG_UI_TIMING
        paintUs.push_back (1.0e6 * (double) (juce::Time::getHighResolutionTicks() - t0)
                               / (double) juce::Time::getHighResolutionTicksPerSecond());
       #endif
    }

    //==========================================================================
    void PlugSequencer::mouseDown (const juce::MouseEvent& e)
    {
        const int row = rowAt (e.y);
        if (row < 0) return;
        const int slot1 = row + 1;

        if (modeButtonBounds (row).contains (e.getPosition()))
        {
            presenter.setLineModeB (slot1, ! rows[(size_t) row].modeB);
            refresh();
            return;
        }
        if (paramButtonBounds (row).contains (e.getPosition())) { showParamMenu (row); return; }

        const int step = stepAt (e.x);
        if (step < 0) return;

        if (e.mods.isPopupMenu())
        {
            // Clic droit : le pas bascule. Transaction nommée, donc relisible.
            presenter.setStepOn (slot1, step + 1, ! cells[(size_t) row * kSteps + (size_t) step].on);
            refresh();
            return;
        }

        if (slot1 != selectedSlot) { presenter.selectSlot (slot1); selectedSlot = slot1; }

        if (e.mods.isShiftDown())
            presenter.selectSteps (selFirst, step + 1);     // Maj : on étend la plage
        else
        {
            dragAnchor = step + 1;
            presenter.selectSteps (dragAnchor, dragAnchor);
        }
        dragging = ! e.mods.isShiftDown();
        refreshSelection();
    }

    void PlugSequencer::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging) return;
        const int step = stepAt (e.x);
        if (step < 0) return;
        if (step + 1 == selLast) return;                    // rien n'a changé : pas de notification
        presenter.selectSteps (dragAnchor, step + 1);
        refreshSelection();
    }

    void PlugSequencer::mouseUp (const juce::MouseEvent&) { dragging = false; }

    //==========================================================================
    void PlugSequencer::showParamMenu (int row)
    {
        const int slot1 = row + 1;
        const auto sv = presenter.slotView (slot1);

        juce::PopupMenu menu;
        menu.addSectionHeader ("Paramètre montré en mode B"_fr);
        for (int m = 0; m < kSlotParams; ++m)
        {
            const auto& p = sv.params[(size_t) m];
            if (! p.declared && p.inert) continue;          // une réserve n'a rien à montrer
            menu.addItem (juce::PopupMenu::Item (p.label).setID (m + 1)
                              .setTicked (p.name == presenter.lineView (slot1).shownParam));
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                .withTargetScreenArea (localAreaToGlobal (paramButtonBounds (row))),
                            [this, slot1, sv] (int result)
                            {
                                if (result <= 0 || result > kSlotParams) return;
                                presenter.setShownParam (slot1, sv.params[(size_t) (result - 1)].name);
                                refresh();
                            });
    }
}
