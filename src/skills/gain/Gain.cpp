// Gain — gain et découpe rythmique (CdC §3.8, ligne « Gain / découpe rythmique »,
// latence nulle ; contrat §3.9 ; sert le geste 1 avec le gate).
// Invariants et leur raison :
//   · latence nulle — un gain ne regarde ni devant ni derrière (§4.3) ;
//   · loi -6 dB — le traité est le sec multiplié, donc en phase avec lui (§3.7) ;
//   · les trois entrées sont libres — gain, montée et descente sont le terrain
//     naturel de la variation par pas (§3.3.1) ;
//   · montée et descente réglables SÉPARÉMENT — c'est ce qui rend la découpe
//     utilisable : un pas qui commande un saut de gain claque, et une découpe se
//     veut nette à l'attaque et douce au relâchement, pas symétrique (§3.3.2) ;
//   · la courbe de gain se calcule dans un tampon prévu par prepare() ; un bloc
//     plus long que prévu est traité en sous-blocs, sans allocation de secours (§4.2).
#include "Gain.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_LEVEL = 0, M_ATT = 1, M_REL = 2;

    constexpr float kAttackMaxMs  = 50.0f;    // course quadratique : de la finesse près de zéro
    constexpr float kReleaseMaxMs = 200.0f;
    constexpr float kLn100 = 4.605170186f;    // ln(100) : le temps déclaré est celui des 99 %

    // Lisibilité (J4b c-2) : exactement les conversions de process(), pas une de plus.
    juce::String dispLevel (float v) { return display::dB (grid::gainLinear (v)); }
    juce::String dispAtt   (float v) { const float c = juce::jlimit (0.0f, 1.0f, v); return display::sig (kAttackMaxMs  * c * c); }
    juce::String dispRel   (float v) { const float c = juce::jlimit (0.0f, 1.0f, v); return display::sig (kReleaseMaxMs * c * c); }

    //==========================================================================
    class Gain : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.gain", 1, "Gain / découpe", MixLaw::Minus6, false,
                {
                    { M_LEVEL, "Gain",
                      "Gain appliqué au signal : 0 = silence, 0,5 = unité, 1 = +6 dB (échelle linéaire, celle de la grille). Libre : c'est lui que le séquenceur hache.",
                      LockClass::Free, true, "dB", dispLevel },
                    { M_ATT,   "Montée",
                      "Temps pour rejoindre un gain plus fort : 0 à 50 ms (99 % de la marche), course quadratique. À 0, la montée est franche ; quelques millisecondes suffisent à retirer le clic. Libre.",
                      LockClass::Free, false, "ms", dispAtt },
                    { M_REL,   "Descente",
                      "Temps pour rejoindre un gain plus faible : 0 à 200 ms (99 % de la marche), course quadratique. Plus long que la montée, il donne une découpe qui respire au lieu de trancher. Libre.",
                      LockClass::Free, false, "ms", dispRel },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int maxBlockSize) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            scratch.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);   // seule allocation de la skill
            reset();
        }

        void reset() override
        {
            cur = 0.0f; primed = false;
            lastAtt = lastRel = -1.0f; attCoef = relCoef = 0.0f;
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0 || scratch.empty()) return;
            juce::ScopedNoDenormals noDenormals;

            const float* lvl = p[M_LEVEL];
            const float* att = p[M_ATT];
            const float* rel = p[M_REL];

            const int cap = (int) scratch.size();
            for (int off = 0; off < n; off += cap)          // bloc plus long que prévu : en sous-blocs
            {
                const int len = juce::jmin (cap, n - off);

                for (int i = 0; i < len; ++i)
                {
                    const int k = off + i;
                    const float a = juce::jlimit (0.0f, 1.0f, att[k]);
                    const float r = juce::jlimit (0.0f, 1.0f, rel[k]);
                    if (a != lastAtt) { lastAtt = a; attCoef = coefFor (kAttackMaxMs  * a * a); }
                    if (r != lastRel) { lastRel = r; relCoef = coefFor (kReleaseMaxMs * r * r); }

                    const float target = grid::gainLinear (lvl[k]);
                    if (! primed) { cur = target; primed = true; }    // première pose : on colle, on ne monte pas
                    else if (cur != target)
                    {
                        cur = target + (cur - target) * (target > cur ? attCoef : relCoef);
                        if (std::abs (cur - target) < 1.0e-7f) cur = target;   // fin de course franche
                    }
                    scratch[(size_t) i] = cur;
                }

                for (int ch = 0; ch < wet.getNumChannels(); ++ch)
                {
                    float* d = wet.getWritePointer (ch) + off;
                    for (int i = 0; i < len; ++i) d[i] *= scratch[(size_t) i];
                }
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        // Coefficient d'un aller-simple exponentiel qui couvre 99 % de la marche en ms.
        float coefFor (float ms) const noexcept
        {
            const double samples = 0.001 * (double) ms * sr;
            return samples >= 1.0 ? (float) std::exp (-(double) kLn100 / samples) : 0.0f;   // sous l'échantillon : saut net
        }

        std::vector<float> scratch;
        double sr = 48000.0;
        float cur = 0.0f, attCoef = 0.0f, relCoef = 0.0f, lastAtt = -1.0f, lastRel = -1.0f;
        bool primed = false;
    };

    //==========================================================================
    // Banc de mesure du selfTest. Hors thread audio : l'allocation y est permise (§4.2).
    struct Bench
    {
        int n;
        juce::AudioBuffer<float> buf;
        std::array<std::vector<float>, (size_t) grid::kModulableCount> store;
        ParamCurves p;

        Bench (int channels, int numSamples, float dc) : n (numSamples), buf (channels, numSamples)
        {
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, dc);
        }
        void set (int m, float v)
        {
            store[(size_t) m].assign ((size_t) n, v);
            p.v[(size_t) m] = store[(size_t) m].data();
        }
    };

    bool Gain::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.gain : " << what << "\n";
            all = all && ok;
        };

        // 1. Gain — la loi de la grille, vérifiée sur trois valeurs connues, transitions à zéro.
        {
            bool ok = true;
            juce::String seen;
            const float in[3]  = { 0.0f, 0.25f, 1.0f };
            const float out[3] = { 0.0f, 0.5f,  2.0f };
            for (int c = 0; c < 3; ++c)
            {
                Bench b (1, 16, 1.0f);
                b.set (M_LEVEL, in[c]); b.set (M_ATT, 0.0f); b.set (M_REL, 0.0f);
                prepare (kSr, 16); reset();
                process (b.buf, b.p, 16);
                ok = ok && std::abs (b.buf.getSample (0, 0)  - out[c]) < 1.0e-6f
                        && std::abs (b.buf.getSample (0, 15) - out[c]) < 1.0e-6f;
                seen << juce::String (b.buf.getSample (0, 15), 3) << (c < 2 ? " / " : "");
            }
            note (ok, "gain 0 / 0,25 / 1 → facteurs 0 / 0,5 / 2 (lus : " + seen + "), saut net quand les temps sont à zéro");
        }

        // 2. Montée — 5 ms déclarés : 90 % à mi-temps, 99 % au temps plein.
        {
            const float attV = std::sqrt (5.0f / kAttackMaxMs);
            prepare (kSr, 512); reset();
            Bench b0 (1, 64, 1.0f);  b0.set (M_LEVEL, 0.0f); b0.set (M_ATT, attV); b0.set (M_REL, 0.0f);
            process (b0.buf, b0.p, 64);                       // pose le gain à zéro
            Bench b1 (1, 480, 1.0f); b1.set (M_LEVEL, 0.5f); b1.set (M_ATT, attV); b1.set (M_REL, 0.0f);
            process (b1.buf, b1.p, 480);
            const float half = b1.buf.getSample (0, 119), full = b1.buf.getSample (0, 239);
            note (std::abs (half - 0.9f) < 0.01f && std::abs (full - 0.99f) < 0.005f,
                  "montée 5 ms : " + juce::String (half, 4) + " à 2,5 ms (0,90), " + juce::String (full, 4) + " à 5 ms (0,99)");
        }

        // 3. Descente — 20 ms déclarés, et la montée reste franche : les deux temps sont distincts.
        float maxStep = 0.0f;
        {
            const float relV = std::sqrt (20.0f / kReleaseMaxMs);
            prepare (kSr, 1200); reset();
            Bench b0 (1, 64, 1.0f);   b0.set (M_LEVEL, 0.5f); b0.set (M_ATT, 0.0f); b0.set (M_REL, relV);
            process (b0.buf, b0.p, 64);                       // montée franche : unité dès le premier échantillon
            const bool instantUp = std::abs (b0.buf.getSample (0, 0) - 1.0f) < 1.0e-6f;
            Bench b1 (1, 1200, 1.0f); b1.set (M_LEVEL, 0.0f); b1.set (M_ATT, 0.0f); b1.set (M_REL, relV);
            process (b1.buf, b1.p, 1200);
            const float half = b1.buf.getSample (0, 479), full = b1.buf.getSample (0, 959);
            float prev = 1.0f;
            for (int i = 0; i < 1200; ++i) { const float v = b1.buf.getSample (0, i); maxStep = juce::jmax (maxStep, std::abs (v - prev)); prev = v; }
            note (instantUp && std::abs (half - 0.1f) < 0.01f && std::abs (full - 0.01f) < 0.005f,
                  "descente 20 ms : " + juce::String (half, 4) + " à 10 ms (0,10), " + juce::String (full, 4) + " à 20 ms (0,01), montée restée franche");
        }

        // 4. Anti-clic — la marche par échantillon reste minuscule pendant la descente réglée.
        note (maxStep < 0.01f, "découpe adoucie : plus grande marche entre deux échantillons " + juce::String (maxStep, 5) + " (< 0,01)");

        return all;
    }
}

void registerGain()
{
    SkillRegistry::instance().add (Gain().info(), [] { return std::make_unique<Gain>(); });
}
}
