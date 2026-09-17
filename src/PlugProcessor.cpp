#include "PlugProcessor.h"
#include "StateSchema.h"
#include "skills/Skills.h"
#include "BuildStamp.h"
#include "ui/Presenter.h"

#ifndef PLUG_J2_TIMING
 #define PLUG_J2_TIMING 0
#endif

#ifndef PLUG_UI_V1
 #define PLUG_UI_V1 0
#endif

#if PLUG_UI_V1
 #include "ui/v1/PlugEditor.h"
#endif

namespace plug
{
    namespace
    {
        juce::File measureDir()
        {
            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("LascauxLab").getChildFile ("Plug");
        }
    }

    //==============================================================================
    PlugProcessor::PlugProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, &undo, "PlugState", grid::createLayout())
    {
        jassert (getParameters().size() == grid::kTotalCount);

        registerAllSkills();     // le catalogue existe avant que le moindre état soit lu (§3.8)
        presets.rescan();

        for (const auto& id : grid::allIds())
            paramSource.raw.push_back (apvts.getRawParameterValue (id));
        plugEngine.setParamSource (&paramSource);

        state::ensureSchema (apvts.state);

        // La couche de présentation naît ici : après le catalogue (elle lit les
        // déclarations des skills) et avant l'écouteur d'arbre du processeur, pour
        // qu'aucune notification ne parte vers une vue qui n'existe pas encore.
        view = std::make_unique<ui::Presenter> (*this);

        apvts.state.addListener (this);
        publishState();
    }

    PlugProcessor::~PlugProcessor()
    {
        view.reset();                       // elle écoute l'arbre et les paramètres : elle part la première
        apvts.state.removeListener (this);
        cancelPendingUpdate();
        dumpTiming ("destructor");
    }

    //==============================================================================
    const juce::String PlugProcessor::getProgramName (int index)
    {
        const auto n = presets.name (index);
        return n.isNotEmpty() ? n : juce::String ("Par défaut");
    }

    void PlugProcessor::setCurrentProgram (int index)
    {
        if (! juce::isPositiveAndBelow (index, presets.size())) return;
        auto tree = presets.load (index);
        if (! tree.isValid()) return;           // preset illisible : on ne casse pas l'état en place

        currentPreset = index;
        // Les 284 valeurs qui arrivent d'un coup ne sont pas de l'automation (d-1, Q4).
        ui::Presenter::ScopedStateReplacement guard (view.get());
        apvts.state.removeListener (this);
        apvts.replaceState (tree);
        apvts.state.addListener (this);
        publishState();
    }

    //==============================================================================
    bool PlugProcessor::loadPresetFile (const juce::File& f)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) return false;

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.hasType (state::id::PlugState)) return false;

        state::ensureSchema (tree);          // un preset d'une version antérieure se migre comme un projet (§3.6)

        ui::Presenter::ScopedStateReplacement guard (view.get());   // un preset ne marque jamais hostDriven (Q4)
        apvts.state.removeListener (this);
        apvts.replaceState (tree);           // chemin d'état normal : le moteur ne voit rien passer
        apvts.state.addListener (this);
        publishState();                      // modèle reconstruit et latence redéclarée, hors audio
        return true;
    }

    bool PlugProcessor::savePresetFile (const juce::File& f)
    {
        auto tree = apvts.copyState();
        tree.setProperty (state::id::schemaVersion, state::kSchemaVersion, nullptr);
        return PresetLibrary::write (f.withFileExtension (".plugstate"), tree);
    }

    //==============================================================================
    void PlugProcessor::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&)
    {
        if (t.hasType (state::id::PARAM)) return;   // l'automation passe par les atomiques, pas par le modèle
        triggerAsyncUpdate();
    }
    void PlugProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)       { if (! c.hasType (state::id::PARAM)) triggerAsyncUpdate(); }
    void PlugProcessor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int){ if (! c.hasType (state::id::PARAM)) triggerAsyncUpdate(); }
    void PlugProcessor::valueTreeChildOrderChanged (juce::ValueTree&, int, int)          { triggerAsyncUpdate(); }
    void PlugProcessor::valueTreeParentChanged (juce::ValueTree&)                        { triggerAsyncUpdate(); }
    void PlugProcessor::handleAsyncUpdate()                                              { publishState(); }

    // Message thread : reconstruit le modèle du moteur et redéclare la latence si elle change.
    void PlugProcessor::publishState()
    {
        plugEngine.setState (apvts.state);
        const int lat = plugEngine.latencySamples();
        if (lat != getLatencySamples())
            setLatencySamples (lat);
    }

    //==============================================================================
    void PlugProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        currentSampleRate = sampleRate;
        currentBlockSize = maximumExpectedSamplesPerBlock;
        plugEngine.prepare (sampleRate, maximumExpectedSamplesPerBlock);
        bypassRing.assign (2, std::vector<float> ((size_t) Engine::kMaxLatency, 0.0f));
        bypassPos = 0;
        timer.reset();
        timingDumped = false;
    }

    void PlugProcessor::releaseResources()
    {
        dumpTiming ("releaseResources");
    }

    bool PlugProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& in  = layouts.getMainInputChannelSet();
        const auto& out = layouts.getMainOutputChannelSet();
        if (in != out) return false;
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

    //==============================================================================
    void PlugProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        timer.begin();

        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        // Temps de l'hôte (§3.3.3) : position musicale, tempo, transport. Sans position : roue libre.
        Transport tr;
        tr.hasPosition = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto bpm = pos->getBpm()) tr.bpm = *bpm;
                if (auto ppq = pos->getPpqPosition()) { tr.ppq = *ppq; tr.hasPosition = true; }
                tr.playing = pos->getIsPlaying();
            }

        plugEngine.process (buffer, midi, tr);
        midi.clear();

        timer.end ((uint32_t) buffer.getNumSamples());
    }

    void PlugProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        // Bypass hôte aligné : le sec retardé de la latence déclarée (§4.3). Le moteur
        // ne tourne pas ; sa latence reste déclarée, donc la piste ne bouge pas.
        midi.clear();
        const int lat = juce::jlimit (0, Engine::kMaxLatency, getLatencySamples());
        if (lat != bypassLatency) { bypassLatency = lat; bypassPos = 0; for (auto& r : bypassRing) std::fill (r.begin(), r.end(), 0.0f); }
        if (lat == 0 || bypassRing.empty()) return;

        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), (int) bypassRing.size());
        size_t p = bypassPos;
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                auto& r = bypassRing[(size_t) ch];
                const float delayed = r[p];
                r[p] = d[i];
                d[i] = delayed;
            }
            if (++p == (size_t) lat) p = 0;
        }
        bypassPos = p;
    }

    //==============================================================================
    bool PlugProcessor::hasEditor() const
    {
       #if PLUG_UI_V1 || PLUG_J2_GENERIC_EDITOR
        return true;
       #else
        return false;
       #endif
    }

   #if PLUG_J2_GENERIC_EDITOR
    namespace
    {
        // Échafaudage J4a : la liste de curseurs de JUCE, coiffée de l'identité du
        // binaire et de deux boutons de preset. C'est le [Preset ▾] du §3.7 sous sa
        // forme brute — il rend le pilote autonome avant l'interface du J4b, qui le
        // remplacera. Rien ici n'est un paramètre : ni l'hôte ni l'état n'en savent rien.
        class PresetBar : public juce::Component
        {
        public:
            static constexpr int kHeight = 30;

            explicit PresetBar (PlugProcessor& p) : proc (p)
            {
                load.setButtonText ("Charger un preset...");
                save.setButtonText ("Enregistrer sous...");
                for (auto* b : { &load, &save }) addAndMakeVisible (*b);

                status.setJustificationType (juce::Justification::centredLeft);
                status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
                status.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (status);
                status.setText ("Presets : " + PlugProcessor::presetsDirectory().getFullPathName(), juce::dontSendNotification);

                load.onClick = [this] { chooseToLoad(); };
                save.onClick = [this] { chooseToSave(); };
            }

            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.35f)); }

            void resized() override
            {
                auto r = getLocalBounds().reduced (6, 3);
                load.setBounds (r.removeFromLeft (150));
                r.removeFromLeft (6);
                save.setBounds (r.removeFromLeft (150));
                r.removeFromLeft (10);
                status.setBounds (r);
            }

        private:
            void chooseToLoad()
            {
                // launchAsync : les boucles modales sont interdites dans un plugin
                // (JUCE_MODAL_LOOPS_PERMITTED=0), et l'hôte n'attend pas.
                chooser = std::make_unique<juce::FileChooser> ("Charger un preset Plug",
                                                                PlugProcessor::presetsDirectory(), "*.plugstate");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [this] (const juce::FileChooser& fc)
                                       {
                                           const auto f = fc.getResult();
                                           if (f == juce::File()) return;
                                           const bool ok = proc.loadPresetFile (f);
                                           status.setText (ok ? "Chargé : " + f.getFileNameWithoutExtension()
                                                              : "Illisible : " + f.getFileName(),
                                                            juce::dontSendNotification);
                                       });
            }

            void chooseToSave()
            {
                chooser = std::make_unique<juce::FileChooser> ("Enregistrer l'état courant",
                                                                PlugProcessor::presetsDirectory().getChildFile ("sans-titre.plugstate"),
                                                                "*.plugstate");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::warnAboutOverwriting,
                                       [this] (const juce::FileChooser& fc)
                                       {
                                           const auto f = fc.getResult();
                                           if (f == juce::File()) return;
                                           const bool ok = proc.savePresetFile (f);
                                           status.setText (ok ? "Enregistré : " + f.getFileNameWithoutExtension()
                                                              : "Échec de l'enregistrement",
                                                            juce::dontSendNotification);
                                       });
            }

            PlugProcessor& proc;
            juce::TextButton load, save;
            juce::Label status;
            std::unique_ptr<juce::FileChooser> chooser;
        };

        // Conteneur : les deux barres en haut, la liste de paramètres en dessous.
        // La liste est un éditeur enfant plutôt qu'une classe de base, pour qu'elle
        // occupe la place qui lui reste au lieu d'être recouverte.
        class ScaffoldEditor : public juce::AudioProcessorEditor
        {
        public:
            explicit ScaffoldEditor (PlugProcessor& p)
                : AudioProcessorEditor (p), presets (p), generic (p)
            {
                addAndMakeVisible (stamp);
                addAndMakeVisible (presets);
                addAndMakeVisible (generic);
                setSize (juce::jmax (620, generic.getWidth()),
                         juce::jmin (760, generic.getHeight() + BuildStampBar::kHeight + PresetBar::kHeight));
                setResizable (true, true);
            }

            void resized() override
            {
                auto r = getLocalBounds();
                stamp.setBounds (r.removeFromTop (BuildStampBar::kHeight));
                presets.setBounds (r.removeFromTop (PresetBar::kHeight));
                generic.setBounds (r);
            }

        private:
            BuildStampBar stamp;
            PresetBar presets;
            juce::GenericAudioProcessorEditor generic;
        };
    }
   #endif

    juce::AudioProcessorEditor* PlugProcessor::createEditor()
    {
        // L'interface v1 prend la place de l'échafaudage dès qu'elle existe ; l'ancien
        // éditeur générique reste atteignable par -DPLUG_UI_V1=OFF tant que la v1 se
        // construit, et disparaît à l'étape 7 (REGIME §8 : un échafaudage porte sa date).
       #if PLUG_UI_V1
        return new ui::v1::PlugEditor (*this, presenter());
       #elif PLUG_J2_GENERIC_EDITOR
        return new ScaffoldEditor (*this);
       #else
        return nullptr;
       #endif
    }

    //==============================================================================
    void PlugProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = apvts.copyState();          // PARAM + Generation + Slots + Macros, un seul arbre (§3.6)
        tree.setProperty (state::id::schemaVersion, state::kSchemaVersion, nullptr);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void PlugProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
        {
            if (xml->hasTagName (apvts.state.getType()))
            {
                auto tree = juce::ValueTree::fromXml (*xml);
                state::ensureSchema (tree);     // migration 1→2 : complète, ne retire rien
                // Ouvrir un projet pose 284 valeurs d'un coup : ce n'est pas l'hôte qui
                // automatise, et la vue ne doit pas s'en persuader (d-1, Q4).
                ui::Presenter::ScopedStateReplacement guard (view.get());
                apvts.replaceState (tree);
                apvts.state.addListener (this);
                publishState();
            }
        }
    }

    //==============================================================================
    void PlugProcessor::dumpTiming (const char* reason)
    {
       #if PLUG_J2_TIMING
        if (timingDumped) return;
        timingDumped = true;
        const auto s = timer.compute();
        if (s.total == 0) return;

        auto dir = measureDir().getChildFile ("measure");
        dir.createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
        auto f = dir.getChildFile ("j3_timing_" + stamp + "_" + juce::String (juce::Random::getSystemRandom().nextInt (10000)) + ".txt");
        const double blockMs = currentBlockSize > 0 && currentSampleRate > 0 ? 1000.0 * currentBlockSize / currentSampleRate : 0.0;

        juce::String out;
        out << "plug J3 timing — " << reason << "\n"
            << "host: " << juce::PluginHostType().getHostDescription() << "\n"
            << "sampleRate: " << currentSampleRate << "\n"
            << "preparedBlockSize: " << currentBlockSize << " (" << juce::String (blockMs, 3) << " ms)\n"
            << "blockSizesSeen: " << (int) s.minBlock << ".." << (int) s.maxBlock << "\n"
            << "latencySamples: " << getLatencySamples() << "\n"
            << "blocksTotal: " << (juce::int64) s.total << "  blocksKept: " << (juce::int64) s.count << "\n"
            << "meanUs: " << juce::String (s.meanUs, 3) << "\np50Us: " << juce::String (s.p50Us, 3)
            << "\np99Us: " << juce::String (s.p99Us, 3) << "\np999Us: " << juce::String (s.p999Us, 3)
            << "\nmaxUs: " << juce::String (s.maxUs, 3) << "\n";
        if (blockMs > 0)
            out << "p999PercentOfBlock: " << juce::String (100.0 * s.p999Us / (blockMs * 1000.0), 4) << "\n"
                << "maxPercentOfBlock: "  << juce::String (100.0 * s.maxUs  / (blockMs * 1000.0), 4) << "\n";
        f.replaceWithText (out);
       #else
        juce::ignoreUnused (reason);
       #endif
    }
}

//==============================================================================
#if ! defined (PLUG_NO_PLUGIN_FACTORY)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new plug::PlugProcessor();
}
#endif
