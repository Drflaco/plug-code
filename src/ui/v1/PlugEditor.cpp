#include "PlugEditor.h"
#include <cmath>

namespace plug::ui::v1
{
    using juce::String;

    namespace
    {
        constexpr int kStampHeight = 22;
        constexpr int kPresetHeight = 30;
    }

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
        : AudioProcessorEditor (processor), presenter (p)
    {
        const auto prefs = presenter.prefsView();

        // L'aide au survol vit dans la fenêtre du plugin, jamais sur le bureau :
        // un hôte peut détruire l'éditeur à tout moment (JUCE_MODAL_LOOPS_PERMITTED=0).
        tooltips = std::make_unique<juce::TooltipWindow> (this, prefs.hoverHelp ? prefs.helpDelayMs : 1 << 30);

        // L'identité du binaire en tête : on ne peut plus écouter un binaire sans
        // savoir lequel (incident 17/09, §3.11 « à-propos »).
        stamp.setText (presenter.aboutView().buildStamp, juce::dontSendNotification);
        stamp.setJustificationType (juce::Justification::centredLeft);
        stamp.setInterceptsMouseClicks (false, false);
        stamp.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));

        status.setJustificationType (juce::Justification::centredLeft);
        status.setInterceptsMouseClicks (false, false);
        status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        status.setText ("Presets : " + presenter.presetView().folder, juce::dontSendNotification);

        load.onClick = [this] { chooseToLoad(); };
        save.onClick = [this] { chooseToSave(); };

        content.addAndMakeVisible (stamp);
        content.addAndMakeVisible (status);
        content.addAndMakeVisible (load);
        content.addAndMakeVisible (save);
        for (auto* z : { &bar, &macros, &tabs, &controls, &edition, &master })
            content.addAndMakeVisible (*z);
        addAndMakeVisible (content);

        // Ratio verrouillé : la mise en page dense du §3.7 n'a qu'une proportion juste.
        setResizable (true, true);
        if (auto* c = getConstrainer())
        {
            c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
            c->setSizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 2, kBaseHeight * 2);
        }
        setSize ((int) std::lround (kBaseWidth * prefs.zoom),
                 (int) std::lround (kBaseHeight * prefs.zoom));

        presenter.addListener (this);
    }

    PlugEditor::~PlugEditor()
    {
        presenter.removeListener (this);
    }

    //==========================================================================
    void PlugEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff1b1b1e));
    }

    void PlugEditor::resized()
    {
        // Zoom : le contenu est dessiné en 1280×800 logiques et mis à l'échelle. La
        // mise en page ne connaît donc qu'une seule taille, quelle que soit la fenêtre.
        const double scale = juce::jmax (0.1, (double) getWidth() / (double) kBaseWidth);
        content.setTransform (juce::AffineTransform::scale ((float) scale));
        content.setBounds (0, 0, kBaseWidth, (int) std::lround (getHeight() / scale));

        auto r = content.getLocalBounds();
        stamp.setBounds (r.removeFromTop (kStampHeight).reduced (8, 0));

        auto presets = r.removeFromTop (kPresetHeight).reduced (6, 3);
        load.setBounds (presets.removeFromLeft (150));
        presets.removeFromLeft (6);
        save.setBounds (presets.removeFromLeft (150));
        presets.removeFromLeft (10);
        status.setBounds (presets);

        r.reduce (6, 6);
        bar.setBounds (r.removeFromTop (44));
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
    }

    //==========================================================================
    void PlugEditor::viewChanged (const ViewMask& mask)
    {
        // Étape 0 : rien à redessiner encore. Le chemin de notification existe et
        // fonctionne — les widgets des étapes 1 à 7 s'y branchent un par un.
        if (mask.has (ViewMask::Presets) || mask.has (ViewMask::Prefs))
        {
            status.setText ("Presets : " + presenter.presetView().folder, juce::dontSendNotification);
            resized();
        }
    }

    void PlugEditor::transportChanged (const TransportView&)
    {
        // Étape 2 : la tête de lecture. Ici, on prouve seulement que le fil arrive.
    }

    //==========================================================================
    void PlugEditor::chooseToLoad()
    {
        chooser = std::make_unique<juce::FileChooser> ("Charger un preset Plug",
                                                        juce::File (presenter.presetView().folder), "*.plugstate");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto f = fc.getResult();
                                   if (f == juce::File()) return;
                                   const bool ok = presenter.loadPreset (f);
                                   status.setText (ok ? "Chargé : " + f.getFileNameWithoutExtension()
                                                      : "Illisible : " + f.getFileName(),
                                                    juce::dontSendNotification);
                               });
    }

    void PlugEditor::chooseToSave()
    {
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer l'état courant",
                                                        juce::File (presenter.presetView().folder)
                                                            .getChildFile ("sans-titre.plugstate"),
                                                        "*.plugstate");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto f = fc.getResult();
                                   if (f == juce::File()) return;
                                   const bool ok = presenter.savePreset (f);
                                   status.setText (ok ? "Enregistré : " + f.getFileNameWithoutExtension()
                                                      : "Échec de l'enregistrement",
                                                    juce::dontSendNotification);
                               });
    }
}
