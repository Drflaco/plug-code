#include "PlugProcessor.h"
#include "StateSchema.h"
#include "skills/Skills.h"

#ifndef PLUG_J2_TIMING
 #define PLUG_J2_TIMING 0
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
        apvts.state.addListener (this);
        publishState();
    }

    PlugProcessor::~PlugProcessor()
    {
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
        apvts.state.removeListener (this);
        apvts.replaceState (tree);
        apvts.state.addListener (this);
        publishState();
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
       #if PLUG_J2_GENERIC_EDITOR
        return true;
       #else
        return false;
       #endif
    }

    juce::AudioProcessorEditor* PlugProcessor::createEditor()
    {
       #if PLUG_J2_GENERIC_EDITOR
        return new juce::GenericAudioProcessorEditor (*this);
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
