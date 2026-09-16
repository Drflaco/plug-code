// Clock — horloge et transport (CdC §3.3.3).
// Garantit : le pas courant est calculé à l'échantillon près depuis la position
// musicale de l'hôte, jamais depuis un compteur interne ; à l'arrêt le pas se
// fige ; au saut de position le pas cible s'applique sans rattrapage ; sans
// position hôte, roue libre à 120 BPM (dégradé, signalé). Longueur 2..32,
// division 1/4..1/64 (binaire, ternaire, pointé), swing dès le socle.
// Aucune allocation : tout est arithmétique.
#pragma once
#include "GridMap.h"
#include <cmath>

namespace plug
{
    struct Transport
    {
        double bpm = 120.0;
        double ppq = 0.0;          // position musicale au premier échantillon du bloc, en noires
        bool playing = true;
        bool hasPosition = true;   // faux = hôte muet : roue libre
    };

    struct ClockSettings
    {
        int length = 16;           // pas par boucle
        double stepBeats = 0.25;   // durée d'un pas en noires (hors swing)
        float swing = 0.0f;        // 0..1, voir grid::swingOffsetBeats
    };

    class Clock
    {
    public:
        void prepare (double sampleRate) noexcept { sr = sampleRate; lastStep = -1; freePpq = 0.0; }
        void reset() noexcept { lastStep = -1; freePpq = 0.0; }

        int currentStep() const noexcept { return lastStep; }
        bool isFreeRunning() const noexcept { return freeRunning; }

        double samplesPerBeat (const Transport& t) const noexcept { return sr * 60.0 / juce::jmax (1.0, t.bpm); }
        double stepDurationSamples (const Transport& t, const ClockSettings& c) const noexcept { return c.stepBeats * samplesPerBeat (t); }

        // Découpe le bloc en tranches à pas constant : fn (offset, longueur, pas, pasCommence).
        // Les bords de pas sont posés à l'échantillon, avec une tolérance de 1e-6 échantillon
        // pour qu'une même position donne le même bord quelle que soit la taille de bloc (test T2).
        template <typename Fn>
        void segments (const Transport& t, const ClockSettings& c, int numSamples, Fn&& fn) noexcept
        {
            const int length = juce::jlimit (2, grid::kSteps, c.length);
            const double spb = samplesPerBeat (t);
            const double stepS = c.stepBeats * spb;
            const double swingS = grid::swingOffsetBeats (c.swing, c.stepBeats) * spb;
            const double periodS = length * stepS;
            constexpr double eps = 1e-6;

            auto boundary = [&] (int k) noexcept -> double
            {
                if (k >= length) return periodS;
                return k * stepS + ((k & 1) ? swingS : 0.0);   // le second pas de chaque paire recule
            };

            if (! t.playing)
            {
                // Transport arrêté : le séquenceur se fige sur son dernier pas ; au tout premier
                // bloc arrêté, le pas de la position courante devient ce dernier pas.
                if (lastStep < 0)
                {
                    const double S = (t.hasPosition ? t.ppq : freePpq) * spb;
                    lastStep = stepAt (S, periodS, length, boundary, eps);
                    fn (0, numSamples, lastStep, true);
                }
                else
                    fn (0, numSamples, lastStep, false);
                return;
            }

            freeRunning = ! t.hasPosition;
            const double S0 = (t.hasPosition ? t.ppq : freePpq) * spb;
            int pos = 0;
            while (pos < numSamples)
            {
                double sm = std::fmod (S0 + pos, periodS);
                if (sm < 0) sm += periodS;
                const int k = stepAt (sm, periodS, length, boundary, eps);
                const double toNext = boundary (k + 1) - sm;
                const int len = juce::jlimit (1, numSamples - pos, (int) std::ceil (toNext - eps));
                fn (pos, len, k, k != lastStep);
                lastStep = k;
                pos += len;
            }
            if (! t.hasPosition)
                freePpq += numSamples / spb;   // roue libre : documentée comme dégradée (§3.3.3)
        }

    private:
        template <typename B>
        static int stepAt (double sm, double periodS, int length, B& boundary, double eps) noexcept
        {
            int k = 0;
            for (int j = 1; j < length; ++j)
                if (sm >= boundary (j) - eps) k = j; else break;
            juce::ignoreUnused (periodS);
            return k;
        }

        double sr = 48000.0;
        int lastStep = -1;
        double freePpq = 0.0;
        bool freeRunning = false;
    };
}
