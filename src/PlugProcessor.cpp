#include "PlugProcessor.h"

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
          apvts (*this, nullptr, "PlugState", grid::createLayout())
    {
        jassert (getParameters().size() == grid::kTotalCount);

        testLatency = readLatencyFile();
        setLatencySamples (testLatency);
    }

    PlugProcessor::~PlugProcessor()
    {
        dumpTiming ("destructor");
    }

    //==============================================================================
    int PlugProcessor::readLatencyFile()
    {
        auto f = measureDir().getChildFile ("j2_latency.txt");
        if (! f.existsAsFile())
            return 0;

        return juce::jlimit (0, kMaxTestLatency, f.loadFileAsString().trim().getIntValue());
    }

    void PlugProcessor::setTestLatency (int samples)
    {
        testLatency = juce::jlimit (0, kMaxTestLatency, samples);
        setLatencySamples (testLatency);
        rebuildDelay();
    }

    void PlugProcessor::rebuildDelay()
    {
        const int channels = juce::jmax (2, getTotalNumOutputChannels());
        ring.assign ((size_t) channels, std::vector<float> ((size_t) juce::jmax (1, testLatency), 0.0f));
        ringPos = 0;
    }

    //==============================================================================
    void PlugProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        currentSampleRate = sampleRate;
        currentBlockSize = maximumExpectedSamplesPerBlock;
        rebuildDelay();
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

        if (in != out)
            return false;

        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

    //==============================================================================
    void PlugProcessor::applyDelay (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (testLatency <= 0)
            return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), (int) ring.size());
        const size_t len = (size_t) testLatency;

        size_t pos = ringPos;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* r = ring[(size_t) ch].data();
            pos = ringPos;

            for (int i = 0; i < numSamples; ++i)
            {
                const float delayed = r[pos];
                r[pos] = data[i];
                data[i] = delayed;
                if (++pos == len) pos = 0;
            }
        }

        ringPos = pos;
    }

    void PlugProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;
        timer.begin();

        // Canaux de sortie sans entrée correspondante : silence.
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        // Passe-tout : le seul traitement est le retard de la latence déclarée.
        applyDelay (buffer);

        timer.end ((uint32_t) buffer.getNumSamples());
    }

    void PlugProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        // Bypass aligné : même retard que le chemin actif, même anneau, donc pas
        // de saut quand l'hôte bascule. La latence déclarée ne bouge pas.
        processBlock (buffer, midi);
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
        auto tree = apvts.copyState();
        tree.setProperty ("schemaVersion", kSchemaVersion, nullptr);

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
                // schemaVersion : migration prévue dès la version 1 (§3.6). Rien à migrer au J2.
                apvts.replaceState (tree);
            }
        }
    }

    //==============================================================================
    void PlugProcessor::dumpTiming (const char* reason)
    {
       #if PLUG_J2_TIMING
        if (timingDumped)
            return;

        timingDumped = true;
        const auto s = timer.compute();
        if (s.total == 0)
            return;

        auto dir = measureDir().getChildFile ("measure");
        dir.createDirectory();

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
        auto f = dir.getChildFile ("j2_timing_" + stamp + "_" + juce::String (juce::Random::getSystemRandom().nextInt (10000)) + ".txt");

        const double blockMs = currentBlockSize > 0 && currentSampleRate > 0 ? 1000.0 * currentBlockSize / currentSampleRate : 0.0;

        juce::String out;
        out << "plug J2 timing — " << reason << "\n"
            << "host: " << juce::PluginHostType().getHostDescription() << "\n"
            << "sampleRate: " << currentSampleRate << "\n"
            << "preparedBlockSize: " << currentBlockSize << " (" << juce::String (blockMs, 3) << " ms)\n"
            << "blockSizesSeen: " << (int) s.minBlock << ".." << (int) s.maxBlock << "\n"
            << "latencySamples: " << testLatency << "\n"
            << "blocksTotal: " << (juce::int64) s.total << "  blocksKept: " << (juce::int64) s.count << "\n"
            << "meanUs: " << juce::String (s.meanUs, 3) << "\n"
            << "p50Us: " << juce::String (s.p50Us, 3) << "\n"
            << "p99Us: " << juce::String (s.p99Us, 3) << "\n"
            << "p999Us: " << juce::String (s.p999Us, 3) << "\n"
            << "maxUs: " << juce::String (s.maxUs, 3) << "\n";

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
