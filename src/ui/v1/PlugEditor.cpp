#include "PlugEditor.h"
#include <cmath>

namespace plug::ui::v1
{
    using juce::String;

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
        : AudioProcessorEditor (processor), presenter (p), bar (p)
    {
        const auto prefsView = presenter.prefsView();

        // L'aide au survol vit dans la fenêtre du plugin, jamais sur le bureau : un hôte
        // peut détruire l'éditeur à tout moment. Délai : préférences (§3.11).
        tooltips = std::make_unique<juce::TooltipWindow> (this, prefsView.hoverHelp ? prefsView.helpDelayMs : 1 << 30);

        bar.onShowAbout = [this] { showAbout(); };
        bar.onShowPrefs = [this] { showPrefs(); };

        content.setInterceptsMouseClicks (false, true);   // un clic dans le vide revient à l'éditeur
        content.addAndMakeVisible (bar);
        for (auto* z : { &macros, &tabs, &controls, &edition, &master })
            content.addAndMakeVisible (*z);
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
        macros.setBounds (r.removeFromTop (70));
        r.removeFromTop (6);
        tabs.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        master.setBounds (r.removeFromBottom (86));
        r.removeFromBottom (6);

        // Partage principal : contrôles à gauche, édition à droite (préférence 2/3–1/3).
        const double part = presenter.prefsView().ratioTwoThirds ? 2.0 / 3.0 : 1.0 / 3.0;
        auto left = r.removeFromLeft ((int) std::lround (r.getWidth() * part));
        controls.setBounds (left);
        r.removeFromLeft (6);
        edition.setBounds (r);

        if (about != nullptr) layOutPanel (*about, 620, 130);
        if (prefs != nullptr) layOutPanel (*prefs, 620, 180);
    }

    void PlugEditor::mouseDown (const juce::MouseEvent&)
    {
        closePanels();
    }

    //==========================================================================
    void PlugEditor::viewChanged (const ViewMask&)
    {
        // Étape 1 : seule la barre a de quoi se relire (nom du preset, « * », état de
        // l'historique). Les widgets des étapes 2 à 7 s'abonneront et filtreront le masque.
        bar.refresh();
    }

    void PlugEditor::transportChanged (const TransportView&)
    {
        // Étape 2 : la tête de lecture. Ici, le fil arrive et ne sert encore à rien.
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

        prefs = std::make_unique<PrefsPanel>();
        prefs->onClose = [this] { closePanels(); };
        content.addAndMakeVisible (*prefs);
        layOutPanel (*prefs, 620, 180);
    }

    void PlugEditor::closePanels()
    {
        about.reset();
        prefs.reset();
    }
}
