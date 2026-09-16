// Filter — filtre résonant passe-bas / passe-bande / passe-haut (CdC §3.8, ligne
// « Filtre (HP/LP/BP, résonance) », latence nulle ; contrat §3.9 ; sert le geste 3).
// Invariants et leur raison :
//   · latence nulle — structure récursive à variable d'état (TPT, deux intégrateurs) :
//     la sortie ne dépend que du présent et du passé, rien à aligner (§4.3) ;
//   · loi -6 dB — le traité reste en phase avec le sec, un mix à mi-course ne creuse pas (§3.7) ;
//   · coupure et résonance libres — c'est le terrain naturel de la variation par pas (§3.3.1) ;
//   · le type est verrouillé par défaut — changer de palier déplace la prise de sortie
//     d'un tap à l'autre du même filtre : la marche s'entend même à état identique ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp, chans) ;
//   · rien n'est alloué : l'état tient dans l'objet, prepare() ne fait que le purger (§4.2).
#include "Filter.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_CUT = 0, M_RES = 1, M_TYPE = 2, M_STEREO = 9;

    constexpr float kFcMin = 20.0f, kFcMax = 20000.0f;   // course de coupure, en Hz
    constexpr float kQMin  = 0.5f,  kQMax  = 12.0f;      // Q : de l'amorti à ~+21 dB de pointe
    constexpr float kStereoOctaves = 0.5f;               // écart maximal par canal, de part et d'autre

    constexpr size_t kMaxChannels = 2;

    // Étendues en logarithme, écrites en dur : un static local coûterait un garde
    // d'initialisation dans la boucle audio, et ces deux nombres ne bougent jamais.
    constexpr float kCutSpan = 6.907755279f;   // ln(20000 / 20) = ln(1000)
    constexpr float kQSpan   = 3.178053830f;   // ln(12 / 0,5)  = ln(24)

    // 20 Hz → 20 kHz en exponentielle : une même course de knob vaut le même
    // intervalle musical en bas comme en haut.
    inline float cutoffHz (float v, float detuneOct, double sr) noexcept
    {
        const float fc = kFcMin * std::exp (juce::jlimit (0.0f, 1.0f, v) * kCutSpan) * std::exp2 (detuneOct);
        return juce::jlimit (kFcMin, (float) (0.45 * sr), fc);   // 0,45·sr : tan() reste borné
    }

    inline float resonanceQ (float v) noexcept
    {
        return kQMin * std::exp (juce::jlimit (0.0f, 1.0f, v) * kQSpan);
    }

    // Trois paliers égaux sur l'entrée continue : passe-bas, passe-bande, passe-haut.
    inline int filterType (float v) noexcept { return juce::jlimit (0, 2, (int) (juce::jlimit (0.0f, 1.0f, v) * 3.0f)); }

    //==========================================================================
    class Filter : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.filter", 1, "Filtre", MixLaw::Minus6, false,
                {
                    { M_CUT,    "Coupure",
                      "Fréquence de coupure, de 20 Hz à 20 kHz, course exponentielle (le milieu tombe vers 630 Hz). Libre : c'est le balayage que le séquenceur fait vivre.",
                      LockClass::Free, true },
                    { M_RES,    "Résonance",
                      "Pointe à la coupure : Q de 0,5 (amorti) à 12 (environ +21 dB). Libre : la pointe suit la coupure sans risque de resynchronisation.",
                      LockClass::Free, true },
                    { M_TYPE,   "Type",
                      "Type de filtre en trois paliers : 0 à 0,33 passe-bas, 0,34 à 0,66 passe-bande, 0,67 à 1 passe-haut. Verrouillé par défaut : changer de palier en cours de séquence déplace la sortie d'un tap à l'autre et s'entend comme une marche ; déverrouille-le si c'est l'effet cherché.",
                      LockClass::LockedByDefault, false },
                    { M_STEREO, "Décalage stéréo",
                      "Écarte les deux coupures : 0,5 = aucun écart, 0 et 1 = une demi-octave par canal (une octave entre gauche et droite), le sens s'inversant de part et d'autre du centre. Libre.",
                      LockClass::Free, true },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            reset();
        }

        void reset() override
        {
            for (auto& s : chan) s = State {};   // affectation d'agrégat : aucune allocation
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0) return;
            juce::ScopedNoDenormals noDenormals;

            const float* cut = p[M_CUT];
            const float* res = p[M_RES];
            const float* typ = p[M_TYPE];
            const float* ste = p[M_STEREO];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& s = chan[(size_t) ch];
                float* d = wet.getWritePointer (ch);
                const float dir = chans < 2 ? 0.0f : (ch == 0 ? -1.0f : 1.0f);   // mono : pas de décalage à répartir

                for (int i = 0; i < n; ++i)
                {
                    // Les coefficients ne se recalculent que si une entrée a bougé : le cas
                    // ordinaire est un pas tenu, et tan()/exp() sont le coût du bloc.
                    if (cut[i] != s.lastCut || res[i] != s.lastRes || ste[i] != s.lastStereo)
                        s.updateCoeffs (cut[i], res[i], ste[i], dir, sr);

                    const float x  = d[i];
                    const float v3 = x - s.ic2;
                    const float v1 = s.a1 * s.ic1 + s.a2 * v3;
                    const float v2 = s.ic2 + s.a2 * s.ic1 + s.a3 * v3;
                    s.ic1 = 2.0f * v1 - s.ic1;
                    s.ic2 = 2.0f * v2 - s.ic2;

                    switch (filterType (typ[i]))
                    {
                        case 0:  d[i] = v2; break;                      // passe-bas
                        case 1:  d[i] = s.k * v1; break;                // passe-bande normalisé : gain crête unité
                        default: d[i] = x - s.k * v1 - v2; break;       // passe-haut
                    }
                }
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        struct State
        {
            float ic1 = 0.0f, ic2 = 0.0f;                 // les deux intégrateurs
            float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            float lastCut = -1.0f, lastRes = -1.0f, lastStereo = -1.0f;

            void updateCoeffs (float cutV, float resV, float steV, float dir, double sr) noexcept
            {
                const float detune = dir * (juce::jlimit (0.0f, 1.0f, steV) - 0.5f) * 2.0f * kStereoOctaves;
                const float g = std::tan ((float) (juce::MathConstants<double>::pi * cutoffHz (cutV, detune, sr) / sr));
                k  = 1.0f / resonanceQ (resV);
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                lastCut = cutV; lastRes = resV; lastStereo = steV;
            }
        };

        std::array<State, kMaxChannels> chan {};
        double sr = 48000.0;
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
        void sine (double freqHz, double sr, float amp)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sr));
        }
        double rms (int ch, int from) const
        {
            double acc = 0.0; int c = 0;
            for (int i = from; i < n; ++i) { const double v = buf.getSample (ch, i); acc += v * v; ++c; }
            return c > 0 ? std::sqrt (acc / (double) c) : 0.0;
        }
    };

    inline double dBratio (double a, double b) noexcept
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-15, a) / juce::jmax (1.0e-15, b));
    }
    // Valeur d'entrée qui donne la coupure voulue (inverse de cutoffHz, sans décalage).
    inline float vForFc (double hz) noexcept { return (float) (std::log (hz / kFcMin) / std::log ((double) kFcMax / kFcMin)); }
    inline float vForQ  (double q)  noexcept { return (float) (std::log (q / kQMin) / std::log ((double) kQMax / kQMin)); }

    bool Filter::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 8192;
        const int from = n / 2;                       // on mesure après établissement
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.filter : " << what << "\n";
            all = all && ok;
        };

        // Un passage complet : régle les quatre entrées, filtre, rend le rapport dB sortie/entrée.
        auto run = [&] (int channels, double freqHz, float amp, float cutV, float resV, float typeV, float stereoV, int ch)
        {
            Bench b (channels, n);
            b.sine (freqHz, kSr, amp);
            b.set (M_CUT, cutV); b.set (M_RES, resV); b.set (M_TYPE, typeV); b.set (M_STEREO, stereoV);
            const double in = (double) amp / std::sqrt (2.0);
            prepare (kSr, n); reset();
            process (b.buf, b.p, n);
            return dBratio (b.rms (ch, from), in);
        };

        const float lp = 0.0f, bp = 0.5f, hp = 1.0f;
        const float qFlat = vForQ (0.707), mid = 0.5f;

        // 1. Coupure — passe-bas à 200 Hz : un sinus à 6 kHz (30× la coupure) s'efface.
        {
            const double att = -run (1, 6000.0, 0.5f, vForFc (200.0), qFlat, lp, mid, 0);
            note (att >= 40.0, "passe-bas 200 Hz, sinus 6 kHz atténué de " + juce::String (att, 1) + " dB (≥ 40 attendus)");
        }
        // 2. Coupure — le même passe-bas laisse passer ce qui est sous la coupure.
        {
            const double g = run (1, 50.0, 0.5f, vForFc (200.0), qFlat, lp, mid, 0);
            note (std::abs (g) <= 1.5, "passe-bas 200 Hz, sinus 50 Hz à " + juce::String (g, 2) + " dB (|g| ≤ 1,5)");
        }
        // 3. Type — passe-haut : coupe le grave, laisse l'aigu.
        {
            const double low  = -run (1, 50.0,   0.5f, vForFc (200.0), qFlat, hp, mid, 0);
            const double high =  run (1, 6000.0, 0.5f, vForFc (200.0), qFlat, hp, mid, 0);
            note (low >= 18.0 && std::abs (high) <= 1.5,
                  "passe-haut 200 Hz : 50 Hz à -" + juce::String (low, 1) + " dB, 6 kHz à " + juce::String (high, 2) + " dB");
        }
        // 4. Type — passe-bande normalisé : unité à la coupure, rejet loin d'elle.
        {
            const double at   = run (1, 1000.0, 0.5f, vForFc (1000.0), vForQ (2.0), bp, mid, 0);
            const double away = -run (1, 8000.0, 0.5f, vForFc (1000.0), vForQ (2.0), bp, mid, 0);
            note (std::abs (at) <= 1.5 && away >= 15.0,
                  "passe-bande 1 kHz : " + juce::String (at, 2) + " dB à la coupure, -" + juce::String (away, 1) + " dB à 8 kHz");
        }
        // 5. Résonance — la pointe se lit à la coupure entre Q mini et Q maxi.
        {
            const double flat = run (1, 1000.0, 0.25f, vForFc (1000.0), 0.0f, lp, mid, 0);
            const double peak = run (1, 1000.0, 0.25f, vForFc (1000.0), 1.0f, lp, mid, 0);
            note (peak - flat >= 15.0, "résonance : " + juce::String (peak - flat, 1) + " dB entre Q 0,5 et Q 12 (≥ 15)");
        }
        // 6. Décalage stéréo — à fond, la droite ouvre plus haut que la gauche.
        {
            const double l = run (2, 1414.0, 0.25f, vForFc (1000.0), qFlat, lp, 1.0f, 0);
            const double r = run (2, 1414.0, 0.25f, vForFc (1000.0), qFlat, lp, 1.0f, 1);
            note (r - l >= 5.0, "décalage stéréo à 1 : droite " + juce::String (r - l, 1) + " dB au-dessus de la gauche (≥ 5)");
        }
        return all;
    }
}

void registerFilter()
{
    SkillRegistry::instance().add (Filter().info(), [] { return std::make_unique<Filter>(); });
}
}
