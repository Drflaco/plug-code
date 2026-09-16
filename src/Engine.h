// Engine — le socle J3 : horloge, séquenceur paramétrique, composition des
// valeurs, verrous, fondus et queues, modulation, macros, chaîne série et
// mélange (CdC §3.3 à §3.7, contrat J3 a/b et amendements 1 à 5).
// Garantit : testable sans hôte (PlugRender), aucune allocation ni verrou dans
// process() (§4.2), rendu fonction pure de (audio, état, position) (§3.6),
// latence déclarée constante pendant une passe (§3.3.2), publication bornée
// de l'état vers l'audio (triple tampon, acquittement par indice).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Clock.h"
#include <memory>

namespace plug
{
    // Source des 284 valeurs brutes de la grille (APVTS dans le plugin, PARAM de l'état hors hôte).
    class ParamSource
    {
    public:
        virtual ~ParamSource() = default;
        virtual float get (int gridIndex) const = 0;
    };

    class Engine
    {
    public:
        static constexpr int kMaxLatency = 8192;   // borne des lignes à retard du sec (préallouées)

        Engine();
        ~Engine();

        void prepare (double sampleRate, int maxBlockSize);    // toute allocation ici
        void reset();                                           // purge audio, sans allocation
        void setParamSource (const ParamSource* source) noexcept;

        // Hors audio : construit un modèle depuis l'arbre PlugState et le publie.
        void setState (const juce::ValueTree& plugState);

        // Thread audio.
        void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Transport& transport);

        int latencySamples() const noexcept;                    // somme déclarée du modèle publié
        int currentStep() const noexcept;
        bool isFreeRunning() const noexcept;
        // Valeurs composées du dernier bloc traité (tests, interface J4) : une par échantillon.
        const float* valueCurve (int slot0, int modulable) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
