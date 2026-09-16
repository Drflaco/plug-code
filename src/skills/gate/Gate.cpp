// Gate — porte de bruit à seuil, attaque, maintien, relâchement et profondeur
// (CdC §3.8, ligne « Gate », latence nulle ; contrat §3.9 ; sert le geste 1).
// Invariants et leur raison :
//   · latence nulle — la porte décide sur le présent, sans pré-écoute ; pas de
//     look-ahead, donc rien à déclarer ni à aligner (§4.3) ;
//   · loi -6 dB — le traité est le sec multiplié par un gain, donc en phase (§3.7) ;
//   · les cinq entrées sont libres — seuil et temps sont du terrain de variation
//     par pas, aucune ne touche à la latence ni à la structure (§3.3.1) ;
//   · rampe en S (3c²-2c³) — sa pente est nulle aux deux bouts : ni l'ouverture ni la
//     fermeture ne produisent de marche, même à l'attaque la plus courte. Un gate qui
//     claque annule le geste 1, qui se joue justement sur des découpes rapides ;
//   · détecteur à relâchement fixe de 3 ms, non exposé — sans lui la porte bat à chaque
//     passage par zéro de la forme d'onde ; le relâchement réglé par le pilote est
//     celui du gain, pas celui du détecteur ;
//   · deux canaux au plus, détection liée — une porte qui s'ouvre d'un seul côté
//     déplace l'image stéréo ; le socle ne présente jamais plus de deux canaux (Engine.cpp) ;
//   · rien n'est alloué : l'état tient dans l'objet, prepare() ne fait que le purger (§4.2).
#include "Gate.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_THR = 0, M_ATT = 1, M_HOLD = 2, M_REL = 3, M_RANGE = 4;

    constexpr float kThrMinDb = -60.0f;        // seuil : -60 dB (tout passe) à 0 dB
    constexpr float kAttMinS  = 0.0001f;       // 0,1 ms → 100 ms, exponentiel
    constexpr float kRelMinS  = 0.001f;        // 1 ms → 1000 ms, exponentiel
    constexpr float kTimeSpan = 6.907755279f;  // ln(1000) : trois décades pour les deux temps
    constexpr float kHoldMaxS = 0.5f;          // maintien : 0 à 500 ms, quadratique
    constexpr float kDbToLin  = 0.1151292546f; // ln(10)/20
    constexpr float kDetReleaseS = 0.003f;     // détecteur : fixe, voir l'en-tête

    constexpr size_t kMaxChannels = 2;

    inline float thresholdLin (float v) noexcept { return std::exp (kThrMinDb * (1.0f - juce::jlimit (0.0f, 1.0f, v)) * kDbToLin); }
    inline float attackSec    (float v) noexcept { return kAttMinS * std::exp (juce::jlimit (0.0f, 1.0f, v) * kTimeSpan); }
    inline float releaseSec   (float v) noexcept { return kRelMinS * std::exp (juce::jlimit (0.0f, 1.0f, v) * kTimeSpan); }
    inline float holdSec      (float v) noexcept { const float c = juce::jlimit (0.0f, 1.0f, v); return kHoldMaxS * c * c; }
    // Profondeur : 0 = silence franc (le cas de la découpe), 1 = le gate n'atténue plus.
    inline float floorGain    (float v) noexcept
    {
        const float c = juce::jlimit (0.0f, 1.0f, v);
        return c <= 0.0f ? 0.0f : std::exp (kThrMinDb * (1.0f - c) * kDbToLin);
    }

    //==========================================================================
    class Gate : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.gate", 1, "Gate", MixLaw::Minus6, false,
                {
                    { M_THR,   "Seuil",
                      "Niveau à partir duquel la porte s'ouvre : de -60 dB (tout passe) à 0 dB (rien ne passe), échelle linéaire en dB. Libre : c'est le réglage que le séquenceur fait respirer.",
                      LockClass::Free, true },
                    { M_ATT,   "Attaque",
                      "Temps d'ouverture : 0,1 ms à 100 ms, course exponentielle. La montée est en S, donc sans clic même au plus court. Libre.",
                      LockClass::Free, false },
                    { M_HOLD,  "Maintien",
                      "Durée pendant laquelle la porte reste ouverte après le passage sous le seuil : 0 à 500 ms, course quadratique. C'est ce qui l'empêche de battre sur un signal qui frôle le seuil. Libre.",
                      LockClass::Free, false },
                    { M_REL,   "Relâchement",
                      "Temps de fermeture, une fois le maintien écoulé : 1 ms à 1000 ms, course exponentielle. Long, il laisse la queue mourir ; court, il tranche. Libre.",
                      LockClass::Free, false },
                    { M_RANGE, "Profondeur",
                      "Ce qui reste quand la porte est fermée : 0 = silence franc, 0,5 = -30 dB, 1 = la porte n'atténue plus rien. Libre.",
                      LockClass::Free, true },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            detCoef = (float) std::exp (-1.0 / (kDetReleaseS * sr));
            reset();
        }

        void reset() override
        {
            det = 0.0f; ctrl = 0.0f; holdLeft = 0;
            lastThr = lastAtt = lastHold = lastRel = lastRange = -1.0f;
            thr = 1.0f; attStep = 1.0f; relStep = 1.0f; holdSamples = 0; floorG = 0.0f;
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0) return;
            juce::ScopedNoDenormals noDenormals;

            const float* th = p[M_THR];
            const float* at = p[M_ATT];
            const float* ho = p[M_HOLD];
            const float* re = p[M_REL];
            const float* ra = p[M_RANGE];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            std::array<float*, kMaxChannels> d {};
            for (int ch = 0; ch < chans; ++ch) d[(size_t) ch] = wet.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
            {
                // Conversions seulement quand une entrée bouge : un pas tenu ne paie
                // ni exp() ni division (le cas ordinaire, annexe A.0).
                if (th[i] != lastThr)   { lastThr = th[i];   thr = thresholdLin (th[i]); }
                if (at[i] != lastAtt)   { lastAtt = at[i];   attStep = stepFor (attackSec (at[i])); }
                if (re[i] != lastRel)   { lastRel = re[i];   relStep = stepFor (releaseSec (re[i])); }
                if (ho[i] != lastHold)  { lastHold = ho[i];  holdSamples = (int) (holdSec (ho[i]) * sr); }
                if (ra[i] != lastRange) { lastRange = ra[i]; floorG = floorGain (ra[i]); }

                float level = 0.0f;
                for (int ch = 0; ch < chans; ++ch) level = juce::jmax (level, std::abs (d[(size_t) ch][i]));
                det = level > det ? level : level + (det - level) * detCoef;   // crête, relâchement fixe

                const bool over = det > thr;
                if (over) holdLeft = holdSamples;
                if (over || holdLeft > 0)
                {
                    if (! over) --holdLeft;
                    ctrl = juce::jmin (1.0f, ctrl + attStep);
                }
                else
                {
                    ctrl = juce::jmax (0.0f, ctrl - relStep);
                }

                const float shaped = ctrl * ctrl * (3.0f - 2.0f * ctrl);       // pente nulle aux deux bouts
                const float g = floorG + (1.0f - floorG) * shaped;
                for (int ch = 0; ch < chans; ++ch) d[(size_t) ch][i] *= g;
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        float stepFor (float seconds) const noexcept
        {
            const double samples = (double) seconds * sr;
            return samples > 1.0 ? (float) (1.0 / samples) : 1.0f;   // sous l'échantillon : d'un coup
        }

        double sr = 48000.0;
        float det = 0.0f, ctrl = 0.0f, detCoef = 0.0f;
        float thr = 1.0f, attStep = 1.0f, relStep = 1.0f, floorG = 0.0f;
        float lastThr = -1.0f, lastAtt = -1.0f, lastHold = -1.0f, lastRel = -1.0f, lastRange = -1.0f;
        int holdSamples = 0, holdLeft = 0;
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
        void all (float thrV, float attV, float holdV, float relV, float rangeV)
        {
            set (M_THR, thrV); set (M_ATT, attV); set (M_HOLD, holdV); set (M_REL, relV); set (M_RANGE, rangeV);
        }
    };

    // Inverses des lois, pour écrire les cas en unités lisibles.
    inline float vThr   (double dB) noexcept { return (float) (1.0 + dB / -(double) kThrMinDb); }
    inline float vAtt   (double ms) noexcept { return (float) (std::log (0.001 * ms / kAttMinS) / kTimeSpan); }
    inline float vRel   (double ms) noexcept { return (float) (std::log (0.001 * ms / kRelMinS) / kTimeSpan); }
    inline float vHold  (double ms) noexcept { return (float) std::sqrt (0.001 * ms / kHoldMaxS); }
    inline float vRange (double dB) noexcept { return (float) (1.0 + dB / -(double) kThrMinDb); }

    bool Gate::selfTest (juce::String& log)
    {
        constexpr double sr = 48000.0;
        constexpr float kLoud = 0.5f, kQuiet = 0.004f;   // -6 dB et -48 dB : de part et d'autre d'un seuil à -30 dB
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.gate : " << what << "\n";
            all = all && ok;
        };

        // Un signal fort pendant 20 ms, puis un signal faible pendant tailMs :
        // rend la valeur de sortie à l'instant demandé après la chute.
        auto afterDrop = [&] (float holdV, float relV, float rangeV, double tailMs, int atMs)
        {
            const int tail = (int) (0.001 * tailMs * sr);
            prepare (sr, tail); reset();
            Bench a (1, 960, kLoud);  a.all (vThr (-30.0), vAtt (1.0), holdV, relV, rangeV);
            process (a.buf, a.p, 960);
            Bench b (1, tail, kQuiet); b.all (vThr (-30.0), vAtt (1.0), holdV, relV, rangeV);
            process (b.buf, b.p, tail);
            return b.buf.getSample (0, juce::jmin (tail - 1, (int) (0.001 * atMs * sr)));
        };

        // 1. Seuil — sous le seuil, profondeur à zéro : la sortie est un silence franc.
        {
            Bench b (1, 4800, kQuiet); b.all (vThr (-30.0), vAtt (1.0), 0.0f, vRel (5.0), 0.0f);
            prepare (sr, 4800); reset();
            process (b.buf, b.p, 4800);
            double peak = 0.0;
            for (int i = 0; i < 4800; ++i) peak = juce::jmax (peak, (double) std::abs (b.buf.getSample (0, i)));
            note (peak < 1.0e-9, "signal à -48 dB sous un seuil à -30 dB : sortie nulle (crête " + juce::String (peak, 12) + ")");
        }
        // 2. Seuil — au-dessus, la porte s'ouvre en grand : le signal ressort intact.
        {
            Bench b (1, 4800, kLoud); b.all (vThr (-30.0), vAtt (1.0), 0.0f, vRel (5.0), 0.0f);
            prepare (sr, 4800); reset();
            process (b.buf, b.p, 4800);
            const float out = b.buf.getSample (0, 4799);
            note (std::abs (out - kLoud) < 1.0e-6f, "signal à -6 dB au-dessus du même seuil : sortie " + juce::String (out, 6) + " = entrée");
        }
        // 3. Attaque — 10 ms déclarés : mi-course à 5 ms (la rampe en S y vaut 0,5), pleine ouverture à 10 ms.
        {
            Bench b (1, 960, kLoud); b.all (vThr (-30.0), vAtt (10.0), 0.0f, vRel (100.0), 0.0f);
            prepare (sr, 960); reset();
            process (b.buf, b.p, 960);
            const float half = b.buf.getSample (0, 239) / kLoud, full = b.buf.getSample (0, 479) / kLoud;
            note (std::abs (half - 0.5f) < 0.02f && std::abs (full - 1.0f) < 0.002f,
                  "attaque 10 ms : gain " + juce::String (half, 4) + " à 5 ms (0,50), " + juce::String (full, 4) + " à 10 ms (1,00)");
        }
        // 4. Maintien — 100 ms après la chute, la porte est fermée sans maintien, encore ouverte avec 200 ms.
        {
            const float none = afterDrop (0.0f,            vRel (5.0), 0.0f, 150.0, 100);
            const float held = afterDrop (vHold (200.0),   vRel (5.0), 0.0f, 150.0, 100);
            note (none < 1.0e-9f && held > 0.9f * kQuiet,
                  "maintien : sans lui " + juce::String (none, 9) + " à 100 ms, avec 200 ms " + juce::String (held, 6) + " (entrée " + juce::String (kQuiet, 6) + ")");
        }
        // 5. Relâchement — même chute, maintien nul : 5 ms a fini, 500 ms est encore largement ouvert.
        {
            const float quick = afterDrop (0.0f, vRel (5.0),   0.0f, 100.0, 50);
            const float slow  = afterDrop (0.0f, vRel (500.0), 0.0f, 100.0, 50);
            note (quick < 1.0e-9f && slow > 0.5f * kQuiet,
                  "relâchement : 5 ms → " + juce::String (quick, 9) + " à 50 ms, 500 ms → " + juce::String (slow, 6));
        }
        // 6. Profondeur — porte fermée à -30 dB : ce qui reste est l'entrée atténuée d'autant.
        {
            Bench b (1, 4800, kQuiet); b.all (vThr (-30.0), vAtt (1.0), 0.0f, vRel (5.0), vRange (-30.0));
            prepare (sr, 4800); reset();
            process (b.buf, b.p, 4800);
            const double out = b.buf.getSample (0, 4799);
            const double expected = (double) kQuiet * std::pow (10.0, -30.0 / 20.0);
            note (std::abs (out - expected) < 0.03 * expected,
                  "profondeur -30 dB : sortie " + juce::String (out, 9) + " pour " + juce::String (expected, 9) + " attendus");
        }
        return all;
    }
}

void registerGate()
{
    SkillRegistry::instance().add (Gate().info(), [] { return std::make_unique<Gate>(); });
}
}
