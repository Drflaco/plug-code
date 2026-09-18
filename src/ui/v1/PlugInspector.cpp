#include "PlugInspector.h"

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        constexpr int kRowHeight = 22;
        constexpr int kLockW = 20, kNameW = 104, kValueW = 84, kRangeW = 132, kProbW = 58, kTransW = 62;
        constexpr float kDragSpan = 160.0f;
    }

    //==========================================================================
    PlugInspector::Row::Row (Presenter& p, PlugInspector& o) : presenter (p), owner (o) {}

    juce::Rectangle<int> PlugInspector::Row::lockArea() const { return getLocalBounds().removeFromLeft (kLockW); }
    juce::Rectangle<int> PlugInspector::Row::nameArea() const { return getLocalBounds().withTrimmedLeft (kLockW).withWidth (kNameW); }

    juce::Rectangle<int> PlugInspector::Row::valueArea() const
    {
        return getLocalBounds().withTrimmedLeft (kLockW + kNameW).withWidth (kValueW).reduced (2, 4);
    }

    juce::Rectangle<int> PlugInspector::Row::rangeArea() const
    {
        return getLocalBounds().withTrimmedLeft (kLockW + kNameW + kValueW).withWidth (kRangeW).reduced (2, 5);
    }

    juce::Rectangle<int> PlugInspector::Row::probArea() const
    {
        return getLocalBounds().withTrimmedLeft (kLockW + kNameW + kValueW + kRangeW).withWidth (kProbW).reduced (2, 5);
    }

    juce::Rectangle<int> PlugInspector::Row::transitionArea() const
    {
        return getLocalBounds().withTrimmedLeft (kLockW + kNameW + kValueW + kRangeW + kProbW)
                               .withWidth (kTransW).reduced (2, 3);
    }

    void PlugInspector::Row::setView (const ParamView& v, int slot, int step, bool shownInLine)
    {
        view = v; slot1 = slot; step1 = step; shown = shownInLine;
        caption = v.label;
        valueText = v.valueText;
        rangeText = Format::rawText (v.min) + "–"_fr + Format::rawText (v.max);
        probText = Format::rawText (v.prob);
        transitionText = v.glide ? "glide"_fr : "saut"_fr;
        lockTag = Format::lockWord (v.locked, v.lockedByDefault, v.structural);
        setTooltip (v.help + (v.lockReason.isNotEmpty() ? "\n" + v.lockReason : String())
                        + "\nValeur : glisser · Plage : glisser les deux moitiés · Proba : glisser"_fr
                        + "\nTransition vers le pas suivant : cliquer"_fr
                        + "\nCliquer le nom : la ligne du séquenceur montre ce paramètre en barres (mode B)."_fr);
        repaint();
    }

    void PlugInspector::Row::paint (juce::Graphics& g)
    {
        const auto ink = juce::Colours::white.withAlpha (view.inert ? 0.3f : 0.85f);
        g.setFont (11.0f);

        if (! view.inert)
            glyph::padlock (g, lockArea().toFloat().reduced (4.0f),
                            juce::Colours::white.withAlpha (view.locked || view.structural ? 0.8f : 0.3f),
                            view.locked || view.structural);

        // La pastille de couleur du paramètre (6a), puis le nom.
        if (! view.inert)
            glyph::swatch (g, nameArea().toFloat().removeFromLeft (10.0f).withSizeKeepingCentre (7.0f, 7.0f),
                           glyph::paramColour (view.index), shown);
        g.setColour (ink);
        g.drawText (caption, nameArea().withTrimmedLeft (11).withWidth (kNameW - 37),
                    juce::Justification::centredLeft, true);
        if (lockTag.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (9.0f);
            g.drawText (lockTag, getLocalBounds().withTrimmedLeft (kLockW).withWidth (kNameW - 2),
                        juce::Justification::centredRight, false);
            g.setFont (11.0f);
        }

        // Valeur du pas.
        auto va = valueArea();
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (va);
        g.setColour (ink);
        g.drawText (valueText, va, juce::Justification::centred, true);

        // Plage de génération : la barre, puis le texte par-dessus.
        auto ra = rangeArea();
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (ra);
        if (! view.inert)
        {
            g.setColour (juce::Colour (0xff7fb3d5).withAlpha (view.locked ? 0.15f : 0.4f));
            g.fillRect (ra.getX() + (int) (ra.getWidth() * view.min), ra.getY(),
                        juce::jmax (1, (int) (ra.getWidth() * (view.max - view.min))), ra.getHeight());
        }
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (10.0f);
        g.drawText (rangeText, ra, juce::Justification::centred, false);
        g.setFont (11.0f);

        // Probabilité.
        auto pa = probArea();
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (pa);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRect (pa.getX(), pa.getY(), juce::jmax (1, (int) (pa.getWidth() * view.prob)), pa.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (10.0f);
        g.drawText (probText, pa, juce::Justification::centred, false);
        g.setFont (11.0f);

        // Transition vers le pas suivant, PAR PARAMÈTRE (§3.3).
        auto ta = transitionArea();
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawRect (ta, 1);
        g.setColour (ink);
        g.drawText (transitionText, ta, juce::Justification::centred, false);
    }

    void PlugInspector::Row::mouseDown (const juce::MouseEvent& e)
    {
        if (view.inert) return;

        if (lockArea().contains (e.getPosition()))
        {
            if (! view.structural) { presenter.setLocked (slot1, view.name, ! view.locked); owner.refresh(); }
            return;
        }
        if (transitionArea().contains (e.getPosition()))
        {
            presenter.setTransition (slot1, view.name, ! view.glide);
            owner.refresh();
            return;
        }
        // Le nom : la ligne du séquenceur montre ce paramètre (6a).
        if (nameArea().contains (e.getPosition())) { presenter.showParam (slot1, view.name); return; }
        grabRaw = view.raw; grabMin = view.min; grabMax = view.max; grabProb = view.prob;

        if (valueArea().contains (e.getPosition()))
        {
            // Colonne « Pas » : sélection partielle → tous les pas sélectionnés (correction 4,
            // geste A) ; ligne entière → le pas montré seul. Dans les deux cas UN geste =
            // UNE transaction, de la prise au relâché.
            const bool partial = presenter.selectionIsPartial();
            grabFirst = partial ? presenter.firstSelectedStep() : step1;
            grabLast  = partial ? presenter.lastSelectedStep()  : step1;
            presenter.beginStepsGesture (slot1, grabFirst, grabLast, view.name);
            grab = Grab::Value;
            return;
        }
        if (probArea().contains (e.getPosition()))   { grab = Grab::Prob; return; }
        if (rangeArea().contains (e.getPosition()))
        {
            // Moitié gauche = le minimum, moitié droite = le maximum. Deux poignées,
            // une seule zone : la plage se lit et se prend au même endroit.
            grab = e.x < rangeArea().getCentreX() ? Grab::Min : Grab::Max;
            return;
        }
    }

    void PlugInspector::Row::mouseDrag (const juce::MouseEvent& e)
    {
        if (grab == Grab::None) return;
        const float delta = -(float) e.getDistanceFromDragStartY() / kDragSpan;

        switch (grab)
        {
            case Grab::Value:
                presenter.setStepsExplicit (slot1, grabFirst, grabLast, view.name, juce::jlimit (0.0f, 1.0f, grabRaw + delta));
                break;
            case Grab::Min:
                presenter.setRange (slot1, view.name, juce::jlimit (0.0f, grabMax, grabMin + delta), grabMax);
                break;
            case Grab::Max:
                presenter.setRange (slot1, view.name, grabMin, juce::jlimit (grabMin, 1.0f, grabMax + delta));
                break;
            case Grab::Prob:
                presenter.setProb (slot1, view.name, juce::jlimit (0.0f, 1.0f, grabProb + delta));
                break;
            case Grab::None: break;
        }
        owner.refresh();
    }

    void PlugInspector::Row::mouseUp (const juce::MouseEvent&)
    {
        if (grab == Grab::Value) presenter.endStepsGesture();
        grab = Grab::None;
    }

    //==========================================================================
    PlugInspector::PlugInspector (Presenter& p) : presenter (p)
    {
        for (auto* l : { &title, &warning, &seedLabel, &densityLabel })
        {
            l->setInterceptsMouseClicks (false, false);
            l->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (*l);
        }
        title.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        warning.setColour (juce::Label::textColourId, juce::Colour (0xffe2b34a));
        seedLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
        densityLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
        seedLabel.setText ("Graine"_fr, juce::dontSendNotification);
        densityLabel.setText ("Densité"_fr, juce::dontSendNotification);

        for (int m = 0; m < kSlotParams; ++m)
            rows.push_back (std::make_unique<Row> (p, *this));
        for (auto& r : rows) addAndMakeVisible (*r);

        density.setSliderStyle (juce::Slider::LinearHorizontal);
        density.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
        density.setRange (0.0, 1.0, 0.0);
        density.setWantsKeyboardFocus (false);
        density.setTooltip ("Densité du tirage : elle resserre la plage autour de son centre. "
                            "La forme du motif est gardée, son amplitude rétrécit (contrat J3)."_fr);
        density.onDragEnd = [this] { presenter.setDensity ((float) density.getValue()); };
        addAndMakeVisible (density);

        // Champ d'entier, validé à Entrée (schéma §2 « Graine [4821] »). Une saisie
        // invalide reprend la valeur courante, sans message : le champ se corrige
        // lui-même, il ne fait pas la leçon.
        seed.setInputRestrictions (10, "0123456789");
        seed.setTooltip ("Graine maîtresse : deux générations à graine égale donnent les mêmes pas.\n"
                         "Entrée pour valider ; une saisie invalide revient à la valeur courante."_fr);
        seed.onReturnKey = [this]
        {
            const auto typed = seed.getText().trim();
            const juce::int64 v = typed.getLargeIntValue();
            if (typed.isNotEmpty() && v >= 0 && v <= 0xffffffffLL) presenter.setMasterSeed ((juce::uint32) v);
            refresh();                       // valide ou non, le champ dit ce qui EST
        };
        seed.onFocusLost = [this] { refresh(); };
        addAndMakeVisible (seed);

        newSeed.setButtonText ("Nouvelle graine"_fr);
        generate.setButtonText ("Générer sur la sélection"_fr);
        capture.setButtonText ("Figer la passe"_fr);
        for (auto* b : { &newSeed, &generate, &capture })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }

        newSeed.onClick = [this]
        {
            presenter.setMasterSeed ((juce::uint32) juce::Random::getSystemRandom().nextInt());
            refresh();
        };
        generate.onClick = [this]
        {
            presenter.generate (presenter.selectedSlot(), presenter.firstSelectedStep(),
                                presenter.lastSelectedStep(), presenter.generationView().density);
            refresh();
        };
        capture.onClick = [this]
        {
            presenter.capture (presenter.selectedSlot(), presenter.firstSelectedStep(),
                               presenter.lastSelectedStep());
            refresh();
        };

        refresh();
    }

    PlugInspector::~PlugInspector() = default;

    //==========================================================================
    void PlugInspector::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        // En-tête du tableau : des repères, pas des données.
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (10.0f);
        auto h = getLocalBounds().reduced (8, 0).withY (24).withHeight (12);
        g.drawText ("Paramètre"_fr, h.withTrimmedLeft (kLockW).withWidth (kNameW), juce::Justification::centredLeft, false);
        g.drawText ("Pas"_fr, h.withTrimmedLeft (kLockW + kNameW).withWidth (kValueW), juce::Justification::centred, false);
        g.drawText ("Plage"_fr, h.withTrimmedLeft (kLockW + kNameW + kValueW).withWidth (kRangeW), juce::Justification::centred, false);
        g.drawText ("Proba"_fr, h.withTrimmedLeft (kLockW + kNameW + kValueW + kRangeW).withWidth (kProbW), juce::Justification::centred, false);
        g.drawText ("Transition"_fr, h.withTrimmedLeft (kLockW + kNameW + kValueW + kRangeW + kProbW).withWidth (kTransW), juce::Justification::centred, false);
    }

    void PlugInspector::resized()
    {
        auto r = getLocalBounds().reduced (8, 6);
        title.setBounds (r.removeFromTop (18));
        r.removeFromTop (14);                                 // la ligne d'en-têtes, peinte

        auto bottom = r.removeFromBottom (78);
        warning.setBounds (bottom.removeFromBottom (18));
        auto buttons = bottom.removeFromBottom (24);
        generate.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (6);
        capture.setBounds (buttons.removeFromLeft (110));

        auto seedRow = bottom.removeFromBottom (22);
        seedLabel.setBounds (seedRow.removeFromLeft (50));
        seed.setBounds (seedRow.removeFromLeft (110).reduced (0, 1));
        seedRow.removeFromLeft (6);
        newSeed.setBounds (seedRow.removeFromLeft (130).reduced (0, 1));

        auto densityRow = bottom.removeFromBottom (22);
        densityLabel.setBounds (densityRow.removeFromLeft (50));
        density.setBounds (densityRow);

        for (auto& row : rows)
        {
            if (r.getHeight() < kRowHeight) { row->setBounds ({}); continue; }
            row->setBounds (r.removeFromTop (kRowHeight));
        }
    }

    //==========================================================================
    void PlugInspector::applyWarnings (const StepCountsView& counts, const GenerationView& gen)
    {
        // d-2 : le compte est écrit SOUS les boutons et AVANT le clic. Deux gestes
        // détruisent des nombres figés — Générer retire les V de la sélection, une
        // nouvelle graine re-dérive tous les pas générés.
        String w;
        if (counts.explicitCount > 0)
            w << counts.explicitCount << (counts.explicitCount > 1 ? " pas figés dans la sélection seront remplacés par « Générer »."_fr
                                                                   : " pas figé dans la sélection sera remplacé par « Générer »."_fr);
        if (counts.generated > 0)
        {
            if (w.isNotEmpty()) w << "  ";
            w << "Nouvelle graine : "_fr << counts.generated
              << (counts.generated > 1 ? " pas générés changeront."_fr : " pas généré changera."_fr);
        }
        warning.setText (w, juce::dontSendNotification);

        generate.setTooltip ("Tire une valeur par pas sur la sélection, à la densité courante ("
                                 + Format::rawText (gen.density) + "). Ctrl+Z revient en arrière."_fr);
        capture.setTooltip (counts.generated > 0
                                ? "Matérialise les "_fr + String (counts.generated) + " pas générés de la sélection : "
                                  "leurs nombres cessent de dépendre de la graine."_fr
                                : "Rien à figer : aucun pas généré dans la sélection."_fr);
        capture.setEnabled (counts.generated > 0);
    }

    void PlugInspector::refresh()
    {
        // 6b : la case SURVOLÉE prend l'affichage (emplacement et pas) ; les commandes et
        // leurs avertissements restent sur la sélection. La souris sortie, on revient.
        const bool hovering = presenter.hoverSlot() > 0;
        const int slot1 = hovering ? presenter.hoverSlot() : presenter.selectedSlot();
        const int first = hovering ? presenter.hoverStep() : presenter.firstSelectedStep();
        const int last  = hovering ? presenter.hoverStep() : presenter.lastSelectedStep();
        const auto sv = hovering ? presenter.slotViewAt (slot1, first) : presenter.slotView (slot1);
        const auto counts = presenter.stepCountsView (presenter.selectedSlot(), presenter.firstSelectedStep(), presenter.lastSelectedStep());
        const auto gen = presenter.generationView();

        title.setText (String (slot1) + " · "_fr + (sv.present ? sv.skillLabel : "—"_fr)
                           + (first == last ? "  ·  pas "_fr + String (first)
                                            : "  ·  pas "_fr + String (first) + "–"_fr + String (last))
                           + (hovering ? "  (survol)"_fr : String()),
                       juce::dontSendNotification);

        // Les entrées déclarées, puis mix et gain que le socle compose toujours.
        const auto lv = presenter.lineView (slot1);   // ce que la ligne montre (6a), lu une fois
        int used = 0;
        for (int m = 0; m < kSlotParams; ++m)
        {
            const auto& p = sv.params[(size_t) m];
            const bool keep = p.declared || p.name == "mix" || p.name == "gain";
            if (! keep) continue;
            rows[(size_t) used]->setView (p, slot1, first, lv.modeB && lv.shownParam == p.name);
            rows[(size_t) used]->setVisible (true);
            ++used;
        }
        for (int i = used; i < (int) rows.size(); ++i) rows[(size_t) i]->setVisible (false);

        density.setValue (gen.density, juce::dontSendNotification);
        seed.setText (String (gen.masterSeed), juce::dontSendNotification);
        newSeed.setTooltip ("Tire une nouvelle graine maîtresse. "_fr
                                + (counts.generated > 0
                                       ? String (counts.generated) + " pas générés de la sélection changeront."_fr
                                       : "Aucun pas généré dans la sélection."_fr));

        applyWarnings (counts, gen);
        resized();
    }
}
