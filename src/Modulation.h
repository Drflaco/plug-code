// Modulation — enveloppe éditable et suiveur de niveau (CdC §3.4).
// Garantit : une source est une fonction déterministe de l'état, de la position
// hôte et du signal (§3.6) ; déclenchement transport = phase dérivée de la
// position (resynchronisée au saut), audio = front montant du suiveur, MIDI =
// note-on à l'échantillon. Aucune allocation : points en tableau fixe.
#pragma once
#include "Clock.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace plug
{
    constexpr int kMaxEnvPoints = 16;

    struct EnvPoint { float t = 0.0f, v = 0.0f; };

    struct EnvSpec
    {
        std::array<EnvPoint, kMaxEnvPoints> pts {};
        int count = 0;
        float rate = 0.5f;        // sync : indice de division (comme seq.division) ; libre : 0,05 s → 10 s
        bool sync = true;
        bool loop = true;
        int trigger = 0;          // 0 transport, 1 audio, 2 midi
    };

    struct FollowerSpec { float attack = 0.01f, release = 0.1f; };

    struct Source2Spec
    {
        bool follower = false;    // faux = seconde enveloppe
        EnvSpec env;
        FollowerSpec fol;
    };

    // Valeur de l'enveloppe à la phase φ ∈ [0,1] : segments linéaires entre points triés,
    // tenue avant le premier et après le dernier. Sans point : 0.
    inline float envelopeValue (const EnvSpec& e, float phase) noexcept
    {
        if (e.count <= 0) return 0.0f;
        if (phase <= e.pts[0].t) return e.pts[0].v;
        for (int i = 1; i < e.count; ++i)
        {
            const auto& a = e.pts[(size_t) i - 1];
            const auto& b = e.pts[(size_t) i];
            if (phase <= b.t)
            {
                const float span = b.t - a.t;
                return span <= 0.0f ? b.v : a.v + (b.v - a.v) * ((phase - a.t) / span);
            }
        }
        return e.pts[(size_t) e.count - 1].v;
    }

    // Durée d'un cycle en échantillons. Sync : 4 × division (1/4 → une mesure, 1/16 → un temps).
    inline double envCycleSamples (const EnvSpec& e, double samplesPerBeat, double sampleRate) noexcept
    {
        if (e.sync)
            return 4.0 * grid::divisionBeats (grid::divisionIndex (e.rate)) * samplesPerBeat;
        return 0.05 * std::pow (200.0, (double) juce::jlimit (0.0f, 1.0f, e.rate)) * sampleRate;
    }

    class Follower
    {
    public:
        void prepare (double sampleRate) noexcept { sr = sampleRate; env = 0.0f; }
        void reset() noexcept { env = 0.0f; }
        void set (const FollowerSpec& s) noexcept
        {
            ca = (float) std::exp (-1.0 / (juce::jmax (1e-4, (double) s.attack) * sr));
            cr = (float) std::exp (-1.0 / (juce::jmax (1e-4, (double) s.release) * sr));
        }
        float process (float x) noexcept
        {
            const float a = std::abs (x);
            env = a > env ? ca * env + (1.0f - ca) * a : cr * env + (1.0f - cr) * a;
            return env;
        }
    private:
        double sr = 48000.0;
        float env = 0.0f, ca = 0.0f, cr = 0.0f;
    };

    class EnvRunner
    {
    public:
        void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
        void reset() noexcept { phase = 0.0; lastTrig = 0.0f; }

        // Remplit out[0..n) avec la valeur de l'enveloppe. trig : signal du suiveur (déclenchement
        // audio) ; midi : note-on (déclenchement MIDI). S0 : position en échantillons au premier
        // échantillon du bloc (hôte ou roue libre).
        void render (const EnvSpec& e, double S0, bool playing, double samplesPerBeat,
                     const float* trig, const juce::MidiBuffer* midi, int n, float* out) noexcept
        {
            const double cycle = juce::jmax (1.0, envCycleSamples (e, samplesPerBeat, sr));

            if (e.trigger == 0)
            {
                // Transport : la phase est la position, jamais un compteur (§3.3.3, §3.6).
                for (int i = 0; i < n; ++i)
                {
                    const double S = playing ? S0 + i : S0;
                    double ph = S / cycle;
                    ph = e.loop ? ph - std::floor (ph) : juce::jlimit (0.0, 1.0, ph);
                    phase = ph;
                    out[i] = envelopeValue (e, (float) ph);
                }
                return;
            }

            const double inc = 1.0 / cycle;

            // Note-on du bloc, relevés d'avance dans un tableau fixe (pas d'itérateur dans la boucle).
            std::array<int, 64> noteOns {};
            int noteOnCount = 0;
            if (e.trigger == 2 && midi != nullptr)
                for (const auto meta : *midi)
                    if (meta.getMessage().isNoteOn() && noteOnCount < 64)
                        noteOns[(size_t) noteOnCount++] = meta.samplePosition;
            int nextNote = 0;

            for (int i = 0; i < n; ++i)
            {
                bool restart = false;
                if (e.trigger == 1 && trig != nullptr)
                {
                    restart = trig[i] >= 0.5f && lastTrig < 0.5f;   // front montant du suiveur
                    lastTrig = trig[i];
                }
                else if (e.trigger == 2)
                {
                    while (nextNote < noteOnCount && noteOns[(size_t) nextNote] <= i) { restart = true; ++nextNote; }
                }
                if (restart) phase = 0.0;
                out[i] = envelopeValue (e, (float) phase);
                if (playing)
                {
                    phase += inc;
                    if (phase >= 1.0) phase = e.loop ? phase - 1.0 : 1.0;
                }
            }
        }

    private:
        double sr = 48000.0;
        double phase = 0.0;
        float lastTrig = 0.0f;
    };
}
