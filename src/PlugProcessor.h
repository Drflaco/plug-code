// PlugProcessor — l'adaptateur hôte VST3 (CdC §5, responsabilité 2) au-dessus
// du moteur J3 : audio, temps, événements, état. Il ne calcule rien lui-même.
// Garantit : la grille de 284 paramètres figée (Rév. 2), l'état v2 sérialisé en
// XML dans le même blob que les paramètres (§3.6), la latence déclarée = celle
// du moteur, le bypass hôte aligné sur cette latence (§4.3), aucune allocation
// dans processBlock (§4.2). L'éditeur générique JUCE reste l'échafaudage J2,
// jusqu'au J4 (décision pilote, ETAT Rév. 4).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterGrid.h"
#include "BlockTimer.h"
#include "Engine.h"
#include <vector>

namespace plug
{
    class PlugProcessor : public juce::AudioProcessor,
                          private juce::ValueTree::Listener,
                          private juce::AsyncUpdater
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
        // Échafaudage J2 conservé jusqu'au J4 (ETAT Rév. 4) : option CMake PLUG_J2_GENERIC_EDITOR.
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override;

        const juce::String getName() const override { return "Plug"; }
        bool acceptsMidi() const override { return true; }     // déclenchement MIDI des enveloppes (§3.4)
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
        // Modèle d'édition (message thread) : l'arbre d'état et son historique (§3.6).
        juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }
        juce::ValueTree& stateTree() noexcept { return apvts.state; }
        juce::UndoManager& undoManager() noexcept { return undo; }
        Engine& engine() noexcept { return plugEngine; }

        const BlockTimer& blockTimer() const noexcept { return timer; }
        void resetBlockTimer() noexcept { timer.reset(); }

    private:
        // Source des valeurs brutes de la grille pour le moteur : les atomiques de l'APVTS.
        class ApvtsParamSource : public ParamSource
        {
        public:
            std::vector<std::atomic<float>*> raw;
            float get (int gridIndex) const override { return raw[(size_t) gridIndex]->load(); }
        };

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
        void valueTreeParentChanged (juce::ValueTree&) override;
        void handleAsyncUpdate() override;

        void publishState();
        void dumpTiming (const char* reason);

        juce::UndoManager undo;
        juce::AudioProcessorValueTreeState apvts;
        ApvtsParamSource paramSource;
        Engine plugEngine;

        // Bypass hôte : le sec retardé de la latence déclarée (§4.3), préalloué.
        std::vector<std::vector<float>> bypassRing;
        size_t bypassPos = 0;
        int bypassLatency = 0;

        double currentSampleRate = 0.0;
        int currentBlockSize = 0;
        BlockTimer timer;
        bool timingDumped = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlugProcessor)
    };
}
