// Fm — modulation de fréquence du signal entrant par un oscillateur interne
// (CdC §3.8, ligne « FM », latence nulle ; contrat §3.9 ; sert le geste 1).
// Invariants et leur raison :
//   · ce n'est pas un synthé — le signal entrant EST la porteuse. On module le point
//     de lecture d'une ligne à retard qui ne contient que l'entrée, ce qui module sa
//     phase : pas d'entrée, pas de son (§2, geste 1) ;
//   · latence nulle — le retard part de zéro et l'interpolation lit deux échantillons
//     déjà écrits ; à profondeur nulle la sortie est l'entrée au bit près, il n'y a
//     rien à aligner ni à déclarer (§4.3) ;
//   · sortie bornée par l'entrée — la ligne ne reçoit jamais la sortie : l'auto-modulation
//     déplace le point de lecture, elle n'ajoute aucun gain. Une réinjection audio, elle,
//     aurait pu diverger, et le balayage des bornes du vérificateur l'aurait trouvée (§4.2) ;
//   · loi -3 dB — le traité est décalé dans le temps, donc hors phase avec le sec (§3.7) ;
//   · index borné à 2 ms d'excursion et rapports accrochés à une série harmonique —
//     le CdC §2 pose le risque : « la FM produit facilement des partiels sans rapport
//     avec la tonalité de la source ». Ce sont les deux garde-fous de ce module ;
//     l'accroche à la hauteur de la source reste hors périmètre (§2, à trancher) ;
//   · rapport verrouillé par défaut — le faire varier d'un pas à l'autre est précisément
//     ce qui casse la cohérence harmonique que le geste 1 cherche (§3.3.1) ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp, chans) ;
//   · la ligne à retard est allouée dans prepare() et nulle part ailleurs (§4.2).
#include "Fm.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_DEPTH = 0, M_FREQ = 1, M_RATIO = 2, M_SELF = 3, M_STEREO = 9;

    constexpr double kDepthMaxS = 0.002;   // excursion crête à crête du retard, en secondes
    constexpr double kSelfMaxS  = 0.002;   // idem pour l'auto-modulation
    constexpr float  kFreqMin   = 0.5f;    // Hz : sous l'audio on entend un vibrato, au-dessus la FM
    constexpr float  kFreqSpan  = 8.294049640f;   // ln(2000 / 0,5) = ln(4000), quatre décades

    // Rapports accrochables : sous-multiples et multiples entiers de la fréquence de base.
    // Les partiels engendrés tombent alors sur une même série harmonique au lieu de se disperser.
    constexpr int kRatioCount = 11;
    constexpr std::array<float, kRatioCount> kRatios
        { 0.25f, 1.0f / 3.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };

    constexpr size_t kMaxChannels = 2;

    inline float quad (float v) noexcept { const float c = juce::jlimit (0.0f, 1.0f, v); return c * c; }
    inline float baseHz (float v) noexcept { return kFreqMin * std::exp (juce::jlimit (0.0f, 1.0f, v) * kFreqSpan); }
    inline float ratioFor (float v) noexcept
    {
        const int k = juce::jlimit (0, kRatioCount - 1,
                                    (int) std::lround ((double) juce::jlimit (0.0f, 1.0f, v) * (kRatioCount - 1)));
        return kRatios[(size_t) k];
    }

    // Lisibilité (J4b c-2) : les mêmes conversions que process(). Le rapport s'affiche
    // comme il s'entend — un rapport accroché, pas un décimal approché.
    juce::String dispDepth  (float v) { return display::sig (1000.0 * kDepthMaxS * (double) quad (v)); }
    juce::String dispFreq   (float v) { return display::sig (baseHz (v)); }
    juce::String dispRatio  (float v)
    {
        const float r = ratioFor (v);
        // display::text : « × » est du multi-octet, juce::String(const char*) le
        // décoderait octet par octet et afficherait « Ã—2 » (mesuré le 17/09).
        if (r < 0.30f) return display::text ("×1/4");
        if (r < 0.40f) return display::text ("×1/3");
        if (r < 0.75f) return display::text ("×1/2");
        return display::text ("×") + juce::String ((int) std::lround (r));
    }
    juce::String dispSelf   (float v) { return display::sig (1000.0 * kSelfMaxS * (double) quad (v)); }
    juce::String dispStereo (float v) { return display::sig ((juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 200.0f); }

    //==========================================================================
    class Fm : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.fm", 1, "FM", MixLaw::Minus3, false,
                {
                    { M_DEPTH,  "Profondeur",
                      "Index de modulation : excursion du retard de 0 à 2 ms, course quadratique (2 ms valent environ 6,3 radians pour une composante à 1 kHz, et l'index croît avec la fréquence de la composante). À 0 la sortie est l'entrée intacte. Libre : c'est le réglage que le séquenceur fait vivre.",
                      LockClass::Free, true, "ms", dispDepth },
                    { M_FREQ,   "Fréquence",
                      "Fréquence de base du modulateur, de 0,5 Hz à 2 kHz, course exponentielle (le milieu tombe vers 32 Hz). Sous 20 Hz on entend un vibrato, au-dessus des partiels latéraux. Libre.",
                      LockClass::Free, true, "Hz", dispFreq },
                    { M_RATIO,  "Rapport",
                      "Multiplie la fréquence de base par un rapport simple, accroché en 11 paliers : 1/4, 1/3, 1/2, 1, 2, 3, 4, 5, 6, 7, 8 (le palier le plus proche de la valeur × 10). Des rapports simples font tomber les partiels sur une même série harmonique. Verrouillé par défaut : le faire sauter d'un pas à l'autre est exactement ce qui disperse les partiels et casse la cohérence cherchée ; déverrouille-le si c'est l'effet voulu.",
                      LockClass::LockedByDefault, false, "", dispRatio },
                    { M_SELF,   "Auto-modulation",
                      "Le signal module son propre retard, en plus de l'oscillateur : 0 à 2 ms d'excursion, course quadratique. Épaissit sans ajouter de hauteur étrangère, puisque le modulateur est la source elle-même. Libre.",
                      LockClass::Free, true, "ms", dispSelf },
                    { M_STEREO, "Décalage stéréo",
                      "Écarte la phase des deux modulateurs : 0,5 = les deux canaux en phase (image mono), 0 et 1 = une demi-période d'écart de part et d'autre, soit une période entière entre gauche et droite. Libre.",
                      LockClass::Free, true, "%", dispStereo },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            maxDelay = (float) ((kDepthMaxS + kSelfMaxS) * sr);
            // Puissance de deux : l'indice de lecture se ramène par masque, sans division
            // ni branchement dans la boucle audio. Seule allocation de la skill (§4.2).
            size = juce::nextPowerOfTwo (juce::jmax (16, (int) std::ceil (maxDelay) + 4));
            mask = size - 1;
            for (auto& l : line) l.assign ((size_t) size, 0.0f);
            reset();
        }

        void reset() override
        {
            for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);   // pas d'allocation : la taille ne change pas
            write = 0;
            phase = 0.0;
            for (auto& v : lastOut) v = 0.0f;
            lastDepth = lastFreq = lastRatio = lastSelf = lastStereo = -1.0f;
            depthSamples = selfSamples = 0.0f;
            inc = 0.0;
            off[0] = off[1] = 0.0;
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0 || size <= 0) return;
            juce::ScopedNoDenormals noDenormals;

            const float* dp = p[M_DEPTH];
            const float* fq = p[M_FREQ];
            const float* ra = p[M_RATIO];
            const float* sf = p[M_SELF];
            const float* st = p[M_STEREO];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            std::array<float*, kMaxChannels> d {};
            for (int ch = 0; ch < chans; ++ch) d[(size_t) ch] = wet.getWritePointer (ch);

            const float dMax = juce::jmin (maxDelay, (float) (size - 2));
            const bool stereoSpread = chans >= 2;   // mono : rien à écarter

            for (int i = 0; i < n; ++i)
            {
                // Conversions seulement quand une entrée bouge : un pas tenu ne paie
                // ni exp() ni lround() (le cas ordinaire, annexe A.0).
                if (dp[i] != lastDepth) { lastDepth = dp[i]; depthSamples = (float) (kDepthMaxS * sr) * quad (dp[i]); }
                if (sf[i] != lastSelf)  { lastSelf  = sf[i]; selfSamples  = (float) (kSelfMaxS  * sr) * quad (sf[i]); }
                if (fq[i] != lastFreq || ra[i] != lastRatio)
                {
                    lastFreq = fq[i]; lastRatio = ra[i];
                    const double f = juce::jmin (0.45 * sr, (double) baseHz (fq[i]) * (double) ratioFor (ra[i]));
                    inc = juce::MathConstants<double>::twoPi * f / sr;
                }
                if (st[i] != lastStereo)
                {
                    lastStereo = st[i];
                    const double spread = ((double) juce::jlimit (0.0f, 1.0f, st[i]) - 0.5) * juce::MathConstants<double>::twoPi;
                    off[0] = stereoSpread ? -0.5 * spread : 0.0;
                    off[1] = stereoSpread ?  0.5 * spread : 0.0;
                }

                for (int ch = 0; ch < chans; ++ch)
                {
                    const size_t c = (size_t) ch;
                    line[c][(size_t) write] = d[c][i];

                    // Deux termes unipolaires : le retard reste dans [0, dMax] par construction,
                    // donc jamais de lecture en avant (ce qui vaudrait une latence non déclarée).
                    const float osc  = 0.5f * (1.0f - (float) std::cos (phase + off[c]));
                    const float self = 0.5f * (1.0f + juce::jlimit (-1.0f, 1.0f, lastOut[c]));
                    const float del  = juce::jlimit (0.0f, dMax, depthSamples * osc + selfSamples * self);

                    const int   i0   = (int) del;
                    const float frac = del - (float) i0;
                    const float a    = line[c][(size_t) ((write - i0)     & mask)];
                    const float b    = line[c][(size_t) ((write - i0 - 1) & mask)];
                    const float out  = a + frac * (b - a);

                    lastOut[c] = out;
                    d[c][i] = out;
                }

                phase += inc;
                if (phase >= juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
                write = (write + 1) & mask;
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        std::array<std::vector<float>, kMaxChannels> line {};
        std::array<float, kMaxChannels> lastOut {};
        std::array<double, kMaxChannels> off {};
        double sr = 48000.0, phase = 0.0, inc = 0.0;
        float maxDelay = 0.0f, depthSamples = 0.0f, selfSamples = 0.0f;
        float lastDepth = -1.0f, lastFreq = -1.0f, lastRatio = -1.0f, lastSelf = -1.0f, lastStereo = -1.0f;
        int size = 0, mask = 0, write = 0;
    };

    //==========================================================================
    // Banc de mesure du selfTest. Hors thread audio : l'allocation y est permise (§4.2).
    struct Bench
    {
        int n;
        juce::AudioBuffer<float> buf;
        std::array<std::vector<float>, (size_t) grid::kModulableCount> store;
        ParamCurves p;

        Bench (int channels, int numSamples) : n (numSamples), buf (channels, numSamples) { buf.clear(); }

        void set (int m, float v)
        {
            store[(size_t) m].assign ((size_t) n, v);
            p.v[(size_t) m] = store[(size_t) m].data();
        }
        void all (float depth, float freq, float ratio, float self, float stereo)
        {
            set (M_DEPTH, depth); set (M_FREQ, freq); set (M_RATIO, ratio); set (M_SELF, self); set (M_STEREO, stereo);
        }
        void sine (double freqHz, double sampleRate, float amp)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate));
        }
        void noise (juce::Random& rng)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, rng.nextFloat() * 1.2f - 0.6f);
        }
    };

    // Amplitude d'une raie, fenêtre de Hann : les bords (ligne à retard encore vide)
    // sont écrasés par la fenêtre, et la fuite entre raies voisines est négligeable.
    double magAt (const juce::AudioBuffer<float>& b, int ch, int n, double freqHz, double sampleRate)
    {
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double w  = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1));
            const double ph = 2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate;
            const double x  = w * (double) b.getSample (ch, i);
            re += x * std::cos (ph);
            im -= x * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / juce::jmax (1.0e-12, wsum);
    }

    // Inverses des lois, pour écrire les cas en unités lisibles.
    inline float vFreq  (double hz) noexcept { return (float) (std::log (hz / (double) kFreqMin) / (double) kFreqSpan); }
    inline float vRatio (int palier) noexcept { return (float) palier / (float) (kRatioCount - 1); }
    inline float vDepth (double seconds) noexcept { return (float) std::sqrt (seconds / kDepthMaxS); }

    bool Fm::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 16384;                 // 341 ms : 2,9 Hz de résolution, de quoi séparer les raies
        constexpr double kCarrier = 1000.0;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.fm : " << what << "\n";
            all = all && ok;
        };

        // Un passage : sinus à kCarrier, les cinq entrées tenues, une seule mesure de raie.
        auto run = [&] (float depth, float freq, float ratio, float self, float stereo, double atHz)
        {
            Bench b (1, n);
            b.sine (kCarrier, kSr, 0.5f);
            b.all (depth, freq, ratio, self, stereo);
            prepare (kSr, n); reset();
            process (b.buf, b.p, n);
            return magAt (b.buf, 0, n, atHz, kSr);
        };

        // 1. Profondeur — à zéro (et sans auto-modulation) le retard vaut zéro : l'entrée ressort intacte.
        {
            Bench b (2, 4096);
            juce::Random rng (1234);
            b.noise (rng);
            juce::AudioBuffer<float> ref (b.buf);
            b.all (0.0f, 0.5f, vRatio (3), 0.0f, 0.5f);
            prepare (kSr, 4096); reset();
            process (b.buf, b.p, 4096);
            double worst = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4096; ++i)
                    worst = juce::jmax (worst, (double) std::abs (b.buf.getSample (ch, i) - ref.getSample (ch, i)));
            note (worst == 0.0, "profondeur 0 : sortie = entrée au bit près (écart maximal " + juce::String (worst, 12) + ")");
        }
        // 2. Profondeur — index 1 radian à 1 kHz : porteuse et premier partiel aux valeurs de Bessel.
        //    J0(1) = 0,76520 et J1(1) = 0,44005 ; le rapport J1/J0 = 0,57508 ne dépend que de l'index.
        {
            const float depth = vDepth (1.0 / (juce::MathConstants<double>::pi * kCarrier));   // index = pi·fc·D = 1
            const float freq  = vFreq (100.0);
            const double c  = run (depth, freq, vRatio (3), 0.0f, 0.5f, kCarrier);
            const double s1 = run (depth, freq, vRatio (3), 0.0f, 0.5f, kCarrier + 100.0);
            const double expectedC = 0.5 * 0.7651976866;
            const double ratio = s1 / juce::jmax (1.0e-12, c);
            note (std::abs (c / expectedC - 1.0) < 0.06 && std::abs (ratio / 0.5750786701 - 1.0) < 0.10,
                  "index 1 rad : porteuse " + juce::String (c, 5) + " (0,38260 attendu), partiel/porteuse "
                      + juce::String (ratio, 5) + " (0,57508 attendu)");
        }
        // 3. Fréquence — les partiels tombent à ± la fréquence du modulateur, et nulle part ailleurs.
        {
            const float depth = vDepth (1.0 / (juce::MathConstants<double>::pi * kCarrier));
            const double a13 = run (depth, vFreq (300.0), vRatio (3), 0.0f, 0.5f, 1300.0);
            const double a17 = run (depth, vFreq (300.0), vRatio (3), 0.0f, 0.5f, 1700.0);
            const double b13 = run (depth, vFreq (700.0), vRatio (3), 0.0f, 0.5f, 1300.0);
            const double b17 = run (depth, vFreq (700.0), vRatio (3), 0.0f, 0.5f, 1700.0);
            note (a13 > 10.0 * a17 && b17 > 10.0 * b13,
                  "modulateur 300 Hz : raie à 1300 (" + juce::String (a13, 5) + ") contre 1700 (" + juce::String (a17, 5)
                      + ") ; à 700 Hz : 1700 (" + juce::String (b17, 5) + ") contre 1300 (" + juce::String (b13, 5) + ")");
        }
        // 4. Rapport — base 350 Hz, palier 3 (×1) contre palier 4 (×2). Les raies de rang impair
        //    (1350) n'existent que pour ×1 ; celle de rang 2 de ×2 (2400) est de rang 4 pour ×1,
        //    donc bien plus faible. Comparer un même bac entre les deux paliers évite de confondre
        //    une raie absente avec une raie de rang supérieur, qui existe toujours.
        {
            const float depth = vDepth (1.0 / (juce::MathConstants<double>::pi * kCarrier));
            const float base  = vFreq (350.0);
            const double u1350 = run (depth, base, vRatio (3), 0.0f, 0.5f, 1350.0);
            const double d1350 = run (depth, base, vRatio (4), 0.0f, 0.5f, 1350.0);
            const double u2400 = run (depth, base, vRatio (3), 0.0f, 0.5f, 2400.0);
            const double d2400 = run (depth, base, vRatio (4), 0.0f, 0.5f, 2400.0);
            note (u1350 > 10.0 * d1350 && d2400 > 10.0 * u2400,
                  "base 350 Hz : raie à 1350 = " + juce::String (u1350, 5) + " en ×1 contre " + juce::String (d1350, 5)
                      + " en ×2 ; raie à 2400 = " + juce::String (d2400, 5) + " en ×2 contre " + juce::String (u2400, 5) + " en ×1");
        }
        // 5. Auto-modulation — seule source de modulation (profondeur nulle) : sans elle la sortie
        //    est le sinus pur, avec elle le signal engendre ses propres partiels.
        {
            const double f1 = run (0.0f, vFreq (300.0), vRatio (3), 0.0f, 0.5f, kCarrier);
            const double h3 = run (0.0f, vFreq (300.0), vRatio (3), 0.0f, 0.5f, 3.0 * kCarrier);
            const double g1 = run (0.0f, vFreq (300.0), vRatio (3), 0.6f, 0.5f, kCarrier);
            const double g3 = run (0.0f, vFreq (300.0), vRatio (3), 0.6f, 0.5f, 3.0 * kCarrier);
            const double sans = h3 / juce::jmax (1.0e-12, f1), avec = g3 / juce::jmax (1.0e-12, g1);
            note (sans < 1.0e-5 && avec > 0.02,
                  "harmonique 3 relative : " + juce::String (sans, 9) + " à 0, " + juce::String (avec, 5) + " à 0,6");
        }
        // 6. Décalage stéréo — à 0,5 les deux modulateurs sont en phase, ailleurs ils s'écartent.
        {
            auto spread = [&] (float stereo)
            {
                Bench b (2, 8192);
                b.sine (kCarrier, kSr, 0.5f);
                b.all (0.4f, vFreq (300.0), vRatio (3), 0.0f, stereo);
                prepare (kSr, 8192); reset();
                process (b.buf, b.p, 8192);
                double worst = 0.0;
                for (int i = 1024; i < 8192; ++i)
                    worst = juce::jmax (worst, (double) std::abs (b.buf.getSample (0, i) - b.buf.getSample (1, i)));
                return worst;
            };
            const double centred = spread (0.5f), apart = spread (0.0f);
            note (centred == 0.0 && apart > 0.05,
                  "écart gauche/droite : " + juce::String (centred, 12) + " à 0,5 ; " + juce::String (apart, 5) + " à 0");
        }
        return all;
    }
}

void registerFm()
{
    SkillRegistry::instance().add (Fm().info(), [] { return std::make_unique<Fm>(); });
}
}
