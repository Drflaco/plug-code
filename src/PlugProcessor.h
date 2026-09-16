// plug-code — processeur J2 : passe-tout, latence déclarée, grille complète.
// Aucun effet, aucune interface. Le moteur DSP arrive au J3.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterGrid.h"
#include "BlockTimer.h"
#include <vector>

namespace plug
{
    class PlugProcessor : public juce::AudioProcessor
    {
    public:
        PlugProcessor();
        ~PlugProcessor() override;

        //==============================================================================
        void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        //==============================================================================
        // Échafaudage J2 uniquement : sans fenêtre, Live ne peut « configurer »
        // aucun paramètre, donc n'en automatise aucun. L'éditeur générique de
        // JUCE (liste de curseurs standard) n'est pas l'interface du §3.7 ;
        // option CMake PLUG_J2_GENERIC_EDITOR, retirée au J4.
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override;

        const juce::String getName() const override { return "Plug"; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        //==============================================================================
        // Crochets de mesure J2 (hors thread audio).
        // La latence de test n'est PAS un paramètre de la grille : elle se lit
        // dans %APPDATA%/LascauxLab/Plug/j2_latency.txt à la construction,
        // ou se force ici (banc). Retirée au J3.
        void setTestLatency (int samples);
        int getTestLatency() const noexcept { return testLatency; }

        juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
        const BlockTimer& blockTimer() const noexcept { return timer; }
        void resetBlockTimer() noexcept { timer.reset(); }

        static constexpr int kMaxTestLatency = 1 << 16;
        static constexpr int kSchemaVersion = 1;

    private:
        void applyDelay (juce::AudioBuffer<float>& buffer) noexcept;
        void rebuildDelay();
        void dumpTiming (const char* reason);
        static int readLatencyFile();

        juce::AudioProcessorValueTreeState apvts;

        int testLatency = 0;
        double currentSampleRate = 0.0;
        int currentBlockSize = 0;
        std::vector<std::vector<float>> ring;   // un anneau par canal, taille = latence
        size_t ringPos = 0;

        BlockTimer timer;
        bool timingDumped = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugProcessor)
    };
}
