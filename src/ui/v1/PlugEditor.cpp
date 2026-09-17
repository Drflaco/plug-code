#include "PlugEditor.h"
#include <cmath>

namespace plug::ui::v1
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    //==========================================================================
    PlugEditor::Zone::Zone (const String& name) : title (name)
    {
        setInterceptsMouseClicks (false, false);
    }

    void PlugEditor::Zone::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (13.0f);
        g.drawText (title, getLocalBounds().reduced (8, 4), juce::Justification::topLeft, false);
    }

    //==========================================================================
    PlugEditor::PlugEditor (juce::AudioProcessor& processor, Presenter& p)
        : AudioProcessorEditor (processor), presenter (p), bar (p), tabs (p), macros (p), controls (p), edition (p), master (p)
    {
        const auto prefsView = presenter.prefsView();

        // L'aide au survol vit dans la fenêtre du plugin, jamais sur le bureau : un hôte
        // peut détruire l'éditeur à tout moment. Délai : préférences (§3.11).
        tooltips = std::make_unique<juce::TooltipWindow> (this, prefsView.hoverHelp ? prefsView.helpDelayMs : 1 << 30);

        bar.onShowAbout = [this] { showAbout(); };
        bar.onShowPrefs = [this] { showPrefs(); };

        content.setInterceptsMouseClicks (false, true);   // un clic dans le vide revient à l'éditeur
        content.addAndMakeVisible (bar);
        content.addAndMakeVisible (tabs);
        content.addAndMakeVisible (macros);
        content.addAndMakeVisible (controls);
        // Le menu d'effet vit dans les onglets : les contrôles le lui demandent.
        controls.onChooseSkill = [this] (int slot1) { tabs.openSkillMenu (slot1); };
        content.addAndMakeVisible (edition);
        content.addAndMakeVisible (master);
        addAndMakeVisible (content);

        // Ratio verrouillé : la mise en page dense du §3.7 n'a qu'une proportion juste.
        setResizable (true, true);
        if (auto* c = getConstrainer())
        {
            c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
            c->setSizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 2, kBaseHeight * 2);
        }
        setSize ((int) std::lround (kBaseWidth * prefsView.zoom),
                 (int) std::lround (kBaseHeight * prefsView.zoom));

        // Meilleur effort pour Ctrl+Z / Ctrl+Y : on ÉCOUTE, on ne réclame rien. Le
        // drapeau EDITOR_WANTS_KEYBOARD_FOCUS du plugin reste FALSE (décision figée) ;
        // les boutons de la barre refusent le focus pour ne pas capter les touches.
        setWantsKeyboardFocus (true);
        addKeyListener (this);

        presenter.addListener (this);

        // Premier affichage APRÈS construction, jamais pendant. Les widgets se lisent
        // dans LEUR constructeur — donc pendant l'initialisation des membres de
        // l'éditeur, avant que celui-ci existe et avant qu'il écoute le Presenter. Sur
        // une instance neuve rien ne changeait ensuite, et ce premier état restait à
        // l'écran : c'est ce que le pilote a vu le 17/09 (indicateurs d'activité faux,
        // corrigés d'eux-mêmes dès qu'un preset était chargé, c'est-à-dire dès la
        // première notification). Le modèle, lui, est juste — PlugBench §6 le vérifie :
        // seize emplacements actifs sur l'état par défaut.
        bar.refresh();
        tabs.refresh();
        macros.refresh();
        controls.refresh();
        edition.refresh();
        master.refresh();
    }

    PlugEditor::~PlugEditor()
    {
        removeKeyListener (this);
        presenter.removeListener (this);
    }

    //==========================================================================
    void PlugEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff1b1b1e));
    }

    void PlugEditor::resized()
    {
        // Zoom : le contenu est dessiné en 1280×800 logiques et mis à l'échelle. La mise
        // en page ne connaît donc qu'une seule taille, quelle que soit la fenêtre.
        const double scale = juce::jmax (0.1, (double) getWidth() / (double) kBaseWidth);
        content.setTransform (juce::AffineTransform::scale ((float) scale));
        content.setBounds (0, 0, kBaseWidth, (int) std::lround (getHeight() / scale));

        auto r = content.getLocalBounds().reduced (6, 6);
        bar.setBounds (r.removeFromTop (PlugBar::kHeight));
        r.removeFromTop (6);
        macros.setBounds (r.removeFromTop (PlugMacros::kHeight));
        r.removeFromTop (6);
        tabs.setBounds (r.removeFromTop (PlugTabs::kHeight));
        r.removeFromTop (6);
        master.setBounds (r.removeFromBottom (PlugMaster::kHeight));
        r.removeFromBottom (6);

        // Contrôles à gauche, édition à droite. Ce partage-ci est FIXE : la préférence
        // 2/3–1/3 gouverne le partage séquenceur / inspecteur DANS la zone d'édition
        // (étape 4 §C), pour que le séquenceur garde toute la largeur de ses 32 pas.
        controls.setBounds (r.removeFromLeft ((int) std::lround (r.getWidth() * 0.34)));
        r.removeFromLeft (6);
        edition.setBounds (r);

        if (about != nullptr) layOutPanel (*about, 620, 130);
        if (prefs != nullptr) layOutPanel (*prefs, PlugPrefsPanel::kWidth, PlugPrefsPanel::kHeight);
    }

    void PlugEditor::mouseDown (const juce::MouseEvent&)
    {
        closePanels();
    }

    //==========================================================================
    void PlugEditor::viewChanged (const ViewMask& mask)
    {
        // Deux chemins, parce qu'ils ne coûtent pas la même chose. Déplacer la sélection
        // ne change pas l'état : relire les dix lignes à chaque mouvement de souris
        // coûtait 2,57 ms par événement (measure/MESURES_J4b.md). Le chemin léger ne
        // relit que ce que la sélection déplace.
        constexpr juce::uint32 kStateBits = ViewMask::Slots | ViewMask::Params | ViewMask::Line
                                          | ViewMask::Master | ViewMask::Macros | ViewMask::Generation
                                          | ViewMask::Sequencer | ViewMask::Presets | ViewMask::Prefs;
        if ((mask.bits & kStateBits) == 0 && mask.has (ViewMask::Session))
        {
            controls.refresh();
            edition.refreshSelection();
            return;
        }

        // Étape 2 : la barre (nom du preset, « * », historique) et les onglets (effets,
        // activité, sélection). Les widgets des étapes suivantes filtreront le masque ;
        // ici les deux se relisent entièrement, ce qui coûte quelques chaînes par
        // notification et rien du tout par frame.
        bar.refresh();
        tabs.refresh();
        macros.refresh();
        controls.refresh();
        edition.refresh();
        master.refresh();
    }

    void PlugEditor::transportChanged (const TransportView& t)
    {
        // 30 Hz : la zone d'édition ne repeint que les deux colonnes qui changent.
        edition.setTransport (t);
    }

    bool PlugEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
    {
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        {
            presenter.undo();
            bar.refresh();
            return true;
        }
        if (key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)
            || key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                               | juce::ModifierKeys::shiftModifier, 0))
        {
            presenter.redo();
            bar.refresh();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && (about != nullptr || prefs != nullptr))
        {
            closePanels();
            return true;
        }
        return false;   // tout le reste retourne à l'hôte : le clavier ne nous appartient pas
    }

    //==========================================================================
    void PlugEditor::layOutPanel (juce::Component& panel, int w, int h)
    {
        const auto area = content.getLocalBounds();
        panel.setBounds (area.getCentreX() - w / 2, area.getY() + 110, w, h);
        panel.toFront (false);
    }

    void PlugEditor::showAbout()
    {
        const bool wasOpen = about != nullptr;
        closePanels();
        if (wasOpen) return;                 // le même bouton ouvre et referme

        about = std::make_unique<AboutPanel> (presenter);
        about->onClose = [this] { closePanels(); };
        content.addAndMakeVisible (*about);
        layOutPanel (*about, 620, 130);
    }

    void PlugEditor::showPrefs()
    {
        const bool wasOpen = prefs != nullptr;
        closePanels();
        if (wasOpen) return;

        prefs = std::make_unique<PlugPrefsPanel> (presenter);
        prefs->onClose = [this] { closePanels(); };
        prefs->onPrefsChanged = [this] { applyPrefs(); };
        content.addAndMakeVisible (*prefs);
        layOutPanel (*prefs, PlugPrefsPanel::kWidth, PlugPrefsPanel::kHeight);
    }

    void PlugEditor::applyPrefs()
    {
        // Le pilote qui coupe l'aide ne la perd pas : un re-clic la rend. Et le zoom
        // change la taille de la fenêtre, pas la mise en page — elle reste en 1280×800
        // logiques, mise à l'échelle par setTransform dans resized().
        const auto p = presenter.prefsView();
        if (tooltips != nullptr)
            tooltips->setMillisecondsBeforeTipAppears (p.hoverHelp ? p.helpDelayMs : 1 << 30);

        const int w = (int) std::lround (kBaseWidth * p.zoom);
        if (w != getWidth()) setSize (w, (int) std::lround (kBaseHeight * p.zoom));
        else                 resized();
    }

    void PlugEditor::closePanels()
    {
        about.reset();
        prefs.reset();
    }
}
