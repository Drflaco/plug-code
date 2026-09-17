#include "PlugBar.h"

namespace plug::ui::v1
{
    using juce::String;

    namespace
    {
        // Identifiants du menu [Preset ▾]. Les entrées de bibliothèque prennent
        // kLibraryBase + index : un seul rappel, pas de lambda par preset.
        constexpr int kLoadFile = 1, kSaveAs = 2, kRescan = 3, kLibraryBase = 100;
    }

    //==========================================================================
    PlugBar::PlugBar (Presenter& p) : presenter (p)
    {
        presetButton.onClick = [this] { showPresetMenu(); };
        undoButton.onClick   = [this] { presenter.undo(); refresh(); };
        redoButton.onClick   = [this] { presenter.redo(); refresh(); };
        aboutButton.onClick  = [this] { if (onShowAbout) onShowAbout(); };
        prefsButton.onClick  = [this] { if (onShowPrefs) onShowPrefs(); };

        // L'identité du binaire est un bouton, pas une décoration : on clique dessus
        // pour avoir la ligne entière, elle reste lisible sans cliquer.
        aboutButton.setButtonText (presenter.aboutView().shortStamp);

        for (auto* b : { &presetButton, &undoButton, &redoButton, &aboutButton, &prefsButton })
        {
            b->setWantsKeyboardFocus (false);   // le clavier reste à l'hôte et à l'éditeur
            addAndMakeVisible (*b);
        }

        status.setJustificationType (juce::Justification::centredLeft);
        status.setInterceptsMouseClicks (false, false);
        status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        addAndMakeVisible (status);

        refresh();
    }

    PlugBar::~PlugBar() = default;

    //==========================================================================
    void PlugBar::paint (juce::Graphics& g)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    }

    void PlugBar::resized()
    {
        auto r = getLocalBounds().reduced (6, 6);
        presetButton.setBounds (r.removeFromLeft (220));
        r.removeFromLeft (8);
        undoButton.setBounds (r.removeFromLeft (90));
        r.removeFromLeft (4);
        redoButton.setBounds (r.removeFromLeft (90));
        r.removeFromLeft (10);

        prefsButton.setBounds (r.removeFromRight (32));
        r.removeFromRight (6);
        aboutButton.setBounds (r.removeFromRight (300));
        r.removeFromRight (10);
        status.setBounds (r);
    }

    //==========================================================================
    void PlugBar::refresh()
    {
        const auto presets = presenter.presetView();
        const auto history = presenter.undoView();

        // Aucun nom connu (Set rouvert : le nom vit dans la session, pas dans l'état) :
        // « — », jamais « sans titre ». On ne fait pas croire à un preset qui n'existe
        // pas (décision pilote du 17/09 ; l'attribut dans PlugState est une question
        // du CdC 0.4, le schéma v2 ne se retouche pas pour un confort d'affichage).
        const String name = presets.currentName.isNotEmpty() ? presets.currentName : String ("—");
        presetButton.setButtonText (name + (presets.modified ? " *" : "") + "  ▾");

        undoButton.setEnabled (history.canUndo);
        redoButton.setEnabled (history.canRedo);

        applyTooltips (presets, history);
    }

    void PlugBar::applyTooltips (const PresetView& presets, const UndoView& history)
    {
        presetButton.setTooltip ("Presets d'état : la bibliothèque, plus charger et enregistrer un fichier.\n"
                                 "Dossier : " + presets.folder
                                 + (presets.currentName.isEmpty()
                                        ? String ("\nNom du preset non conservé par le projet en v1.") : String())
                                 + (presets.modified ? "\nL'état a changé depuis le chargement (*)." : ""));

        // Le nom de la transaction, pas « Annuler » : c'est lui qui montre la granularité.
        undoButton.setTooltip (history.canUndo
                                   ? (history.undoName.isNotEmpty() ? "Annuler : " + history.undoName
                                                                    : String ("Annuler la dernière action"))
                                   : String ("Rien à annuler"));
        redoButton.setTooltip (history.canRedo
                                   ? (history.redoName.isNotEmpty() ? "Refaire : " + history.redoName
                                                                    : String ("Refaire l'action annulée"))
                                   : String ("Rien à refaire"));

        aboutButton.setTooltip (presenter.aboutView().buildStamp);
        prefsButton.setTooltip ("Préférences (§3.11) — l'espace existe, son contenu arrive à l'étape 6.");
    }

    //==========================================================================
    void PlugBar::showPresetMenu()
    {
        presenter.rescanPresets();
        const auto presets = presenter.presetView();

        juce::PopupMenu menu;
        menu.addSectionHeader ("Bibliothèque");
        if (presets.names.isEmpty())
            menu.addItem (juce::PopupMenu::Item ("Aucun preset dans " + presets.folder).setEnabled (false));
        else
            for (int i = 0; i < presets.names.size(); ++i)
                menu.addItem (juce::PopupMenu::Item (presets.names[i])
                                  .setID (kLibraryBase + i)
                                  .setTicked (i == presets.currentIndex));

        menu.addSeparator();
        menu.addItem (kLoadFile, "Charger un fichier...");
        menu.addItem (kSaveAs,   "Enregistrer sous...");
        menu.addItem (kRescan,   "Relire le dossier");

        // showMenuAsync : aucune boucle modale dans un plugin, et l'hôte n'attend pas.
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                            [this] (int result)
                            {
                                if (result == 0) return;
                                if (result == kLoadFile) { chooseToLoad(); return; }
                                if (result == kSaveAs)   { chooseToSave(); return; }
                                if (result == kRescan)   { presenter.rescanPresets(); refresh(); return; }

                                const int index = result - kLibraryBase;
                                if (index < 0) return;
                                const bool ok = presenter.loadPresetIndex (index);
                                status.setText (ok ? "Chargé : " + presenter.presetView().currentName
                                                   : String ("Preset illisible"),
                                                juce::dontSendNotification);
                                refresh();
                            });
    }

    void PlugBar::chooseToLoad()
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
                                   refresh();
                               });
    }

    void PlugBar::chooseToSave()
    {
        const auto presets = presenter.presetView();
        const auto suggested = juce::File (presets.folder)
                                   .getChildFile ((presets.currentName.isNotEmpty() ? presets.currentName
                                                                                    : String ("sans-titre"))
                                                      + ".plugstate");
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer l'état courant", suggested, "*.plugstate");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto f = fc.getResult();
                                   if (f == juce::File()) return;
                                   const bool ok = presenter.savePreset (f);
                                   status.setText (ok ? "Enregistré : " + f.getFileNameWithoutExtension()
                                                      : String ("Échec de l'enregistrement"),
                                                    juce::dontSendNotification);
                                   refresh();
                               });
    }
}
