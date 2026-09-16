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
#include "PresetLibrary.h"
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

        // Presets d'état (§3.6, décision pilote J4a) : chaque .plugstate porte l'état
        // COMPLET, identités de skill comprises.
        //
        // LE PLUGIN N'EXPOSE JAMAIS DE PROGRAMMES À L'HÔTE (décision pilote, 16/09).
        // Mesuré dans JUCE 8.0.15 (juce_audio_plugin_client_VST3.cpp, « if (numPrograms > 1) ») :
        // au-delà d'un programme, l'enveloppe VST3 ajoute un paramètre caché « Program ».
        // Même à identifiant fixe, c'est une entrée de plus dans ce que Live voit, et la
        // règle est qu'il n'y en a pas : la grille reste 284 + le Bypass imposé par la
        // norme, tel que gravé au J2 (ETAT Rév. 2). Les presets se chargent par fichier —
        // .vstpreset côté hôte, et le menu [Preset ▾] de l'interface au J4b (§3.7).
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
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
        PresetLibrary presets;
        int currentPreset = 0;
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
