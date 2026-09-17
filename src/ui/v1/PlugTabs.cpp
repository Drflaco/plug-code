#include "PlugTabs.h"

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    namespace
    {
        constexpr int kGap = 4;            // espace entre deux onglets : la place du trait d'insertion
        constexpr int kMinTabWidth = 64;
        constexpr int kMaxTabWidth = 150;
        constexpr int kDragThreshold = 6;  // pixels avant qu'un clic devienne un glisser
        constexpr int kEdgeZone = 12;      // bord d'onglet : au-delà c'est un échange, en deçà une insertion
        constexpr int kBannerMs = 3000;    // durée du rappel « Annuler (Ctrl+Z) » après un dépôt

        constexpr int kClearSlot = 1, kSkillBase = 100, kCopyToBase = 200;
    }

    //==========================================================================
    PlugTabs::Tab::Tab (PlugTabs& owner, int slotNumber) : slot1 (slotNumber), tabs (owner) {}

    juce::Rectangle<int> PlugTabs::Tab::lamp() const
    {
        return juce::Rectangle<int> (6, getHeight() / 2 - 5, 10, 10);
    }

    juce::Rectangle<int> PlugTabs::Tab::arrow() const
    {
        return juce::Rectangle<int> (getWidth() - 18, 0, 18, getHeight());
    }

    void PlugTabs::Tab::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (selected ? juce::Colours::white.withAlpha (0.16f)
                              : juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (highlighted ? juce::Colours::white.withAlpha (0.85f)
                                 : juce::Colours::white.withAlpha (selected ? 0.55f : 0.20f));
        g.drawRoundedRectangle (r, 3.0f, highlighted ? 2.0f : 1.0f);

        // Indicateur d'activité : plein = l'emplacement traite, creux = contourné (§3.3.2).
        const auto l = lamp().toFloat();
        g.setColour (active ? juce::Colours::white.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.25f));
        if (active) g.fillEllipse (l); else g.drawEllipse (l, 1.0f);

        auto textArea = getLocalBounds().withTrimmedLeft (20).withTrimmedRight (18);

        // Skill absente du registre : un triangle DESSINÉ, pas un « ⚠ » — U+26A0 manque
        // dans la police par défaut de Windows (défaut vu dans Live le 17/09).
        if (unknown)
        {
            glyph::warning (g, textArea.removeFromLeft (14).toFloat(), juce::Colour (0xffe2b34a));
            textArea.removeFromLeft (3);
        }

        g.setColour (unknown ? juce::Colour (0xffe2b34a)
                             : juce::Colours::white.withAlpha (sourceOfDrag ? 0.35f : (present ? 0.9f : 0.45f)));
        // 12 points et non 13 : « 1 · Pitch granulaire » tenait mal dans un onglet, et le
        // nom complet reste dans l'aide au survol (remarque du pilote, 17/09).
        g.setFont (12.0f);
        g.drawText (title, textArea, juce::Justification::centredLeft, true);

        glyph::chevronDown (g, arrow().toFloat(), juce::Colours::white.withAlpha (0.45f));
    }

    void PlugTabs::Tab::mouseDown (const juce::MouseEvent& e) { tabs.tabMouseDown (*this, e); }
    void PlugTabs::Tab::mouseDrag (const juce::MouseEvent& e) { tabs.tabMouseDrag (*this, e); }
    void PlugTabs::Tab::mouseUp   (const juce::MouseEvent& e) { tabs.tabMouseUp   (*this, e); }

    //==========================================================================
    PlugTabs::PlugTabs (Presenter& p) : presenter (p)
    {
        banner.setJustificationType (juce::Justification::centredLeft);
        banner.setInterceptsMouseClicks (false, false);
        banner.setColour (juce::Label::textColourId, juce::Colour (0xffe2b34a));
        banner.setVisible (false);
        addChildComponent (banner);

        bannerUndo.setButtonText ("Annuler (Ctrl+Z)"_fr);
        bannerUndo.setWantsKeyboardFocus (false);
        bannerUndo.setTooltip ("Défait le déplacement qui vient d'avoir lieu."_fr);
        bannerUndo.onClick = [this] { presenter.undo(); setBanner ({}, false); };
        bannerUndo.setVisible (false);
        addChildComponent (bannerUndo);

        refresh();
    }

    PlugTabs::~PlugTabs() = default;

    //==========================================================================
    juce::Rectangle<int> PlugTabs::rowArea() const
    {
        return getLocalBounds().removeFromTop (kRowHeight);
    }

    void PlugTabs::refresh()
    {
        const int wanted = presenter.displayedSlots();

        // On ne recrée les onglets que si leur NOMBRE change : un refresh est
        // fréquent, une reconstruction de composants ne doit pas l'être.
        if ((int) tabs.size() != wanted)
        {
            tabs.clear();
            for (int s = 1; s <= wanted; ++s)
            {
                auto tab = std::make_unique<Tab> (*this, s);
                addAndMakeVisible (*tab);
                tabs.push_back (std::move (tab));
            }
            resized();
        }

        const int selected = presenter.selectedSlot();
        for (auto& t : tabs)
        {
            const auto v = presenter.slotView (t->slot1);

            // Le titre est composé ICI, une fois par notification. paint() ne fabrique
            // aucune chaîne : c'est la règle de rendu du pilote (aucune allocation par frame).
            // Le signe d'alerte d'une skill inconnue est DESSINÉ par Tab::paint, pas écrit.
            const String what = v.unknown ? v.skillId : (v.present ? v.skillLabel : "—"_fr);
            t->title = String (t->slot1) + " · "_fr + what;
            t->selected = (t->slot1 == selected);
            t->active = v.active;
            t->unknown = v.unknown;
            t->present = v.present;

            String help;
            if (v.unknown)
                help << "Effet inconnu : "_fr << v.skillId << " v" << v.skillVersion
                     << "\nL'audio traverse, les données sont conservées (§3.9)."_fr;
            else if (! v.present)
                help << "Emplacement vide — clic droit ou chevron pour choisir un effet."_fr;
            else
                help << v.skillLabel << "  (" << v.skillId << " v" << v.skillVersion << ")"
                     << "\nLoi de mélange naturelle : "_fr << v.mixLaw
                     << "\nLatence : non affichée en v1."_fr;
            help << "\nClic = sélectionner · clic droit ou chevron = changer d'effet."_fr
                 << "\nGlisser = réordonner : sur un onglet pour échanger, entre deux pour insérer, "
                    "Alt pour copier. Au-delà du dernier onglet, l'effet va à la fin des "
                    "emplacements affichés."_fr;
            if (v.hostDriven)
                help << "\nDes valeurs sont arrivées de l'hôte sur cet emplacement."_fr;
            t->setTooltip (help);
            t->repaint();
        }
    }

    void PlugTabs::resized()
    {
        auto row = rowArea();
        const int n = (int) tabs.size();
        if (n > 0)
        {
            const int available = row.getWidth() - (n - 1) * kGap;
            const int w = juce::jlimit (kMinTabWidth, kMaxTabWidth, available / juce::jmax (1, n));
            int x = row.getX();
            for (auto& t : tabs)
            {
                t->setBounds (x, row.getY(), w, row.getHeight());
                x += w + kGap;
            }
        }

        auto strip = getLocalBounds().removeFromBottom (kBannerHeight);
        bannerUndo.setBounds (strip.removeFromRight (140).reduced (2, 1));
        strip.removeFromRight (8);
        banner.setBounds (strip);
    }

    //==========================================================================
    void PlugTabs::paint (juce::Graphics&) {}

    void PlugTabs::paintOverChildren (juce::Graphics& g)
    {
        if (! dragging) return;

        if (insertLineX >= 0)
        {
            // Trait d'insertion : le geste du §3.2 se voit avant de se produire.
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (insertLineX - 1, rowArea().getY(), 2, kRowHeight);
        }

        if (dropKind == DropKind::Copy)
        {
            g.setColour (juce::Colour (0xffe2b34a));
            g.setFont (12.0f);
            g.drawText (copyTag, rowArea().removeFromRight (60), juce::Justification::centredRight, false);
        }
    }

    //==========================================================================
    void PlugTabs::tabMouseDown (Tab& tab, const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu()) { showSkillMenu (tab.slot1); return; }

        if (tab.lamp().expanded (4, 4).contains (e.getPosition()))
            return;                                   // traité au relâchement : un clic, pas un glisser
        if (tab.arrow().contains (e.getPosition()))
            return;

        if (tab.slot1 != presenter.selectedSlot())
        {
            presenter.selectSlot (tab.slot1);
            refresh();
        }
    }

    void PlugTabs::tabMouseDrag (Tab& tab, const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu()) return;

        if (! dragging)
        {
            if (e.getDistanceFromDragStart() < kDragThreshold) return;

            // Seuil franchi : c'est ici, AVANT le dépôt, qu'on dit ce qui ne suivra pas
            // l'effet (ETAT d-1). Le texte est composé une fois, pas à chaque frame.
            dragging = true;
            dragFrom = tab.slot1;
            tab.sourceOfDrag = true;
            setBanner (presenter.moveWarning (dragFrom), false);
        }

        updateDrag (e);

        // Le curseur appartient au composant qui a capturé la souris, donc à l'onglet
        // source : Alt s'y voit tout de suite, sans étiquette flottante à allouer.
        tab.setMouseCursor (dropKind == DropKind::Copy ? juce::MouseCursor::CopyingCursor
                                                       : juce::MouseCursor::DraggingHandCursor);
    }

    void PlugTabs::tabMouseUp (Tab& tab, const juce::MouseEvent& e)
    {
        if (dragging) { applyDrop(); return; }
        if (e.mods.isPopupMenu()) return;

        if (tab.lamp().expanded (4, 4).contains (e.getPosition()))
        {
            presenter.setActive (tab.slot1, ! tab.active);
            refresh();
            return;
        }
        if (tab.arrow().contains (e.getPosition()))
            showSkillMenu (tab.slot1);
    }

    //==========================================================================
    void PlugTabs::updateDrag (const juce::MouseEvent& e)
    {
        const auto p = e.getEventRelativeTo (this).getPosition();
        clearDropMarks();

        const int n = (int) tabs.size();
        if (n == 0) return;

        const bool copy = e.mods.isAltDown();

        // Quel onglet est sous le curseur ? Au-delà du dernier, on vise la fin.
        int over = -1;
        for (int i = 0; i < n; ++i)
            if (p.x >= tabs[(size_t) i]->getX() && p.x < tabs[(size_t) i]->getRight()) { over = i; break; }

        if (over < 0)
        {
            const bool past = p.x >= tabs[(size_t) (n - 1)]->getRight();
            const int boundary = past ? n : 0;
            insertLineX = past ? tabs[(size_t) (n - 1)]->getRight() + kGap / 2
                               : tabs[0]->getX() - kGap / 2;
            const int from0 = dragFrom - 1;
            dropTo = (boundary <= from0) ? boundary + 1 : boundary;
            dropKind = DropKind::Insert;
            repaint();
            return;
        }

        auto& t = *tabs[(size_t) over];
        const int fromLeft = p.x - t.getX();
        const int fromRight = t.getRight() - p.x;

        if (copy)
        {
            dropKind = DropKind::Copy;
            dropTo = t.slot1;
            t.highlighted = true;
            t.repaint();
        }
        else if (fromLeft < kEdgeZone || fromRight < kEdgeZone)
        {
            // Près d'un bord : insertion entre deux onglets, les autres se décalent.
            const int boundary = (fromLeft < fromRight) ? over : over + 1;
            insertLineX = (fromLeft < fromRight) ? t.getX() - kGap / 2 : t.getRight() + kGap / 2;
            const int from0 = dragFrom - 1;
            dropTo = (boundary <= from0) ? boundary + 1 : boundary;
            dropKind = DropKind::Insert;
        }
        else
        {
            dropKind = DropKind::Swap;
            dropTo = t.slot1;
            t.highlighted = true;
            t.repaint();
        }
        repaint();
    }

    void PlugTabs::clearDropMarks()
    {
        insertLineX = -1;
        dropKind = DropKind::None;
        dropTo = 0;
        for (auto& t : tabs)
            if (t->highlighted) { t->highlighted = false; t->repaint(); }
    }

    void PlugTabs::applyDrop()
    {
        const int from = dragFrom;
        const int to = dropTo;
        const auto kind = dropKind;

        dragging = false;
        dragFrom = 0;
        clearDropMarks();
        for (auto& t : tabs)
        {
            t->setMouseCursor (juce::MouseCursor::NormalCursor);
            if (t->sourceOfDrag) { t->sourceOfDrag = false; t->repaint(); }
        }

        if (kind == DropKind::None || to <= 0 || to == from) { setBanner ({}, false); refresh(); return; }

        presenter.moveSlot (from, to,
                            kind == DropKind::Swap ? Presenter::MoveMode::Swap
                          : kind == DropKind::Copy ? Presenter::MoveMode::Copy
                                                   : Presenter::MoveMode::Insert);

        // Le bandeau reste trois secondes avec Ctrl+Z à portée de clic : pas de
        // confirmation avant, une porte de sortie après (ETAT d-1).
        auto warning = presenter.moveWarning (from);
        if (warning.isEmpty()) warning = "Déplacement appliqué : seul l'effet a bougé (§3.2)."_fr;
        setBanner (warning, true);
        startTimer (kBannerMs);
        refresh();
    }

    //==========================================================================
    void PlugTabs::setBanner (const String& text, bool offerUndo)
    {
        stopTimer();
        banner.setText (text, juce::dontSendNotification);
        banner.setVisible (text.isNotEmpty());
        bannerUndo.setVisible (offerUndo && text.isNotEmpty());
    }

    void PlugTabs::timerCallback()
    {
        stopTimer();
        setBanner ({}, false);
    }

    //==========================================================================
    void PlugTabs::openSkillMenu (int slot1) { showSkillMenu (slot1); }

    void PlugTabs::showSkillMenu (int slot1)
    {
        const auto v = presenter.slotView (slot1);
        const auto ids = presenter.skillIds();

        juce::PopupMenu menu;
        menu.addSectionHeader ("Emplacement "_fr + String (slot1));
        for (int i = 0; i < ids.size(); ++i)
        {
            // JUCE ne donne pas d'aide au survol à un élément de menu : l'identifiant est
            // donc écrit dans l'élément lui-même, ce qui le rend plus visible qu'une aide
            // — et c'est lui qui est gravé à jamais (§3.9).
            menu.addItem (juce::PopupMenu::Item (presenter.skillLabel (ids[i]) + "   ·   "_fr + ids[i])
                              .setID (kSkillBase + i)
                              .setTicked (ids[i] == v.skillId));
        }
        menu.addSeparator();

        // Repli au cas où Live mange la touche Alt du glisser-copier (décision pilote
        // du 17/09) : la MÊME commande, la même transaction, par le menu.
        juce::PopupMenu copyTo;
        const int shown = presenter.displayedSlots();
        for (int s = 1; s <= shown; ++s)
            if (s != slot1)
                copyTo.addItem (juce::PopupMenu::Item (String (s)).setID (kCopyToBase + s).setEnabled (v.present));
        menu.addSubMenu ("Copier vers..."_fr, copyTo, v.present);

        menu.addItem (juce::PopupMenu::Item ("Vider l'emplacement"_fr).setID (kClearSlot).setEnabled (v.present));

        Component* target = nullptr;
        for (auto& t : tabs) if (t->slot1 == slot1) target = t.get();

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                            [this, slot1, ids] (int result)
                            {
                                if (result == 0) return;
                                if (result == kClearSlot) { presenter.clearSlot (slot1); refresh(); return; }

                                if (result >= kCopyToBase)
                                {
                                    presenter.moveSlot (slot1, result - kCopyToBase, Presenter::MoveMode::Copy);
                                    refresh();
                                    return;
                                }

                                const int index = result - kSkillBase;
                                if (index < 0 || index >= ids.size()) return;
                                presenter.setSkill (slot1, ids[index]);
                                presenter.selectSlot (slot1);
                                refresh();
                            });
    }
}
