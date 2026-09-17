// Delay — écho à une ligne par canal, tête de lecture continue dite « bande »
// (CdC §3.8, ligne « Délai / écho », latence nulle ; contrat §3.9 ; sert les gestes 2 et 3).
// Invariants et leur raison :
//   · latence nulle — la sortie ne dépend que de ce qui est déjà écrit dans l'anneau ;
//     rien n'est à aligner ni à déclarer à l'hôte (§4.3) ;
//   · loi -3 dB — le traité est décalé dans le temps, jamais en phase avec le sec (§3.7) ;
//   · anneau alloué UNE fois pour le temps maximal (kTimeMaxS, constante de compilation)
//     et jamais réalloué : changer le temps déplace la tête de lecture dans le même
//     anneau, on lit plus loin ou moins loin, rien ne se réserve en cours de passe (§4.2) ;
//   · tête de lecture CONTINUE, à vitesse bornée (kSlewMax échantillon par échantillon,
//     interpolation linéaire) — c'est le choix de lecture de l'anneau. Un saut de temps
//     sur une ligne qui sonne ne peut alors produire qu'un glissement de hauteur (l'effet
//     de bande), jamais un clic, parce que le signal lu reste continu. Le prix est connu
//     et assumé : la course entière met kTimeMaxS / kSlewMax secondes à se parcourir.
//     C'est pourquoi le temps est déclaré en GLISSEMENT : la transition entre pas et la
//     mécanique interne racontent alors la même chose au pilote ;
//   · réinjection bornée à kFbMax, strictement sous 1 — à 1 la boucle est un oscillateur
//     qui sature. La borne vit dans le code (feedbackOf), pas seulement dans l'aide ;
//   · bloqueur de continu fixe à 20 Hz dans la boucle, non exposé — sans lui une composante
//     continue se multiplierait par 1/(1-kFbMax) et finirait par emporter la ligne ;
//     son gain maximal (2/(1+R)) reste sous 1,002, la boucle reste donc sous 1 ;
//   · après reset() la tête se pose d'emblée sur le temps demandé — il n'y a plus de queue
//     à préserver, donc rien à glisser ; c'est le seul saut de tête autorisé ;
//   · queue : quand l'entrée devient silencieuse (pas désactivé en « laissée mourir »,
//     §3.3.2), la boucle continue de tourner sur son seul contenu et décroît d'un facteur
//     kFbMax au plus à chaque tour ; reset() purge l'anneau et les deux filtres ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp, chans) ;
//   · rien n'est alloué dans process() : tout vient de prepare() (§4.2).
#include "Delay.h"
#include "../../Skill.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_TIME = 0, M_FB = 1, M_DAMP = 2, M_STEREO = 9;

    constexpr float kTimeMinS  = 0.001f;        // 1 ms
    constexpr float kTimeMaxS  = 2.0f;          // 2 s — dimensionne l'anneau, constante de compilation
    constexpr float kTimeSpan  = 7.600902459f;  // ln(2000) : trois décades et demie
    constexpr float kFbMax     = 0.95f;         // borne stricte de la boucle
    constexpr float kDampMax   = 0.9f;          // coefficient du passe-bas de boucle
    constexpr float kSlewMax   = 0.25f;         // échantillon de temps gagné ou perdu par échantillon
    constexpr float kStereoMax = 0.5f;          // écart maximal par canal, de part et d'autre
    constexpr float kDcHz      = 20.0f;         // bloqueur de continu, fixe
    constexpr float kMinDelay  = 2.0f;          // échantillons : la tête ne rattrape jamais l'écriture

    constexpr size_t kMaxChannels = 2;

    // 1 ms → 2 s en exponentielle : une même course de knob vaut le même rapport de temps
    // en bas comme en haut, donc le même intervalle rythmique.
    inline float delaySeconds (float v) noexcept { return kTimeMinS * std::exp (juce::jlimit (0.0f, 1.0f, v) * kTimeSpan); }
    inline float feedbackOf   (float v) noexcept { return kFbMax * juce::jlimit (0.0f, 1.0f, v); }
    // Quadratique : de la finesse près de zéro, où l'oreille entend la moindre perte d'aigu.
    inline float dampOf       (float v) noexcept { const float c = juce::jlimit (0.0f, 1.0f, v); return kDampMax * c * c; }

    // Lisibilité (J4b c-2) : les conversions de process(), rendues lisibles.
    juce::String dispTime   (float v) { return display::sig (1000.0f * delaySeconds (v)); }
    juce::String dispFb     (float v) { return display::sig (100.0f * feedbackOf (v)); }
    juce::String dispDamp   (float v) { return display::sig (100.0f * dampOf (v) / kDampMax); }
    juce::String dispStereo (float v) { return display::sig ((juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0f * kStereoMax * 100.0f); }

    //==========================================================================
    class Delay : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.delay", 1, "Délai", MixLaw::Minus3, false,
                {
                    { M_TIME,   "Temps",
                      "Temps de retard, de 1 ms à 2 s, course exponentielle (le milieu tombe vers 45 ms). La tête de lecture est continue : changer le temps pendant que la ligne sonne fait glisser la hauteur comme une bande, sans clic. Elle se déplace au plus d'un quart d'échantillon par échantillon, donc un très grand écart met quelques secondes à s'installer. Libre : c'est le terrain naturel de la variation par pas (§3.3.1).",
                      LockClass::Free, true, "ms", dispTime },
                    { M_FB,     "Réinjection",
                      "Part de la répétition renvoyée dans la ligne : 0 = une seule répétition, 1 = 0,95, jamais davantage. La boucle est bornée strictement sous 1 pour qu'elle ne puisse pas devenir un oscillateur qui sature ; à 1 la queue perd 5 % par tour et s'éteint toujours. Libre.",
                      LockClass::Free, true, "%", dispFb },
                    { M_DAMP,   "Amortissement",
                      "Passe-bas à un pôle dans la boucle : chaque répétition perd un peu d'aigu. 0 = aucun filtrage (la ligne est transparente), 0,5 = coupure vers 11 kHz, 1 = vers 800 Hz (valeurs à 48 kHz : le filtre est défini par son coefficient, sa coupure suit donc la fréquence d'échantillonnage). Libre.",
                      LockClass::Free, true, "%", dispDamp },
                    { M_STEREO, "Décalage stéréo",
                      "Écarte les temps des deux canaux : 0,5 = même temps des deux côtés, 0 et 1 = ±50 % par canal (rapport de 1 à 3 entre gauche et droite), le sens s'inversant de part et d'autre du centre. Le temps ainsi écarté reste plafonné à 2 s. Libre.",
                      LockClass::Free, true, "%", dispStereo },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            maxDelay = (float) (kTimeMaxS * sr);
            const size_t n = (size_t) maxDelay + 4;      // marge : interpolation + arrondi
            for (auto& l : line)
                if (l.ring.size() != n) l.ring.assign (n, 0.0f);   // seule allocation, hors audio (§4.2)
            dcR = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * kDcHz / sr);
            reset();
        }

        void reset() override
        {
            for (auto& l : line)
            {
                std::fill (l.ring.begin(), l.ring.end(), 0.0f);    // purge : la queue ne survit pas à un reset
                l.write = 0;
                l.delay = -1.0f;                                   // < 0 : tête non posée, elle se posera d'un coup
                l.target = kMinDelay;
                l.lp = l.dcX = l.dcY = 0.0f;
                l.fb = l.damp = 0.0f;
                l.lastTime = l.lastFb = l.lastDamp = l.lastStereo = -1.0f;
            }
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0 || line[0].ring.empty()) return;
            juce::ScopedNoDenormals noDenormals;

            const float* tm = p[M_TIME];
            const float* fb = p[M_FB];
            const float* dm = p[M_DAMP];
            const float* st = p[M_STEREO];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& l = line[(size_t) ch];
                const int N = (int) l.ring.size();
                float* d = wet.getWritePointer (ch);
                const float dir = chans < 2 ? 0.0f : (ch == 0 ? -1.0f : 1.0f);   // mono : pas d'écart à répartir

                for (int i = 0; i < n; ++i)
                {
                    // Conversions seulement quand une entrée bouge : un pas tenu ne paie
                    // ni exp() ni division (le cas ordinaire, annexe A.0).
                    if (tm[i] != l.lastTime || st[i] != l.lastStereo)
                    {
                        l.lastTime = tm[i]; l.lastStereo = st[i];
                        const float factor = 1.0f + dir * (juce::jlimit (0.0f, 1.0f, st[i]) - 0.5f) * 2.0f * kStereoMax;
                        l.target = juce::jlimit (kMinDelay, maxDelay, delaySeconds (tm[i]) * (float) sr * factor);
                    }
                    if (fb[i] != l.lastFb)   { l.lastFb = fb[i];   l.fb = feedbackOf (fb[i]); }
                    if (dm[i] != l.lastDamp) { l.lastDamp = dm[i]; l.damp = dampOf (dm[i]); }

                    // Tête continue : elle rejoint la cible à vitesse bornée. C'est ce qui
                    // transforme un saut de temps en glissement de hauteur au lieu d'un clic.
                    if (l.delay < 0.0f) l.delay = l.target;
                    else                l.delay += juce::jlimit (-kSlewMax, kSlewMax, l.target - l.delay);

                    float r = (float) l.write - l.delay;
                    if (r < 0.0f) r += (float) N;
                    int i0 = (int) r;
                    if (i0 >= N) i0 = N - 1;
                    const float f = r - (float) i0;
                    const int i1 = (i0 + 1 >= N) ? 0 : i0 + 1;
                    const float out = l.ring[(size_t) i0] + f * (l.ring[(size_t) i1] - l.ring[(size_t) i0]);

                    l.lp += (1.0f - l.damp) * (out - l.lp);          // damp = 0 : lp = out, ligne transparente
                    const float hp = l.lp - l.dcX + dcR * l.dcY;     // bloqueur de continu, fixe
                    l.dcX = l.lp; l.dcY = hp;

                    l.ring[(size_t) l.write] = d[i] + l.fb * hp;
                    d[i] = out;
                    if (++l.write >= N) l.write = 0;
                }
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        struct Line
        {
            std::vector<float> ring;
            int write = 0;
            float delay = -1.0f, target = kMinDelay;
            float lp = 0.0f, dcX = 0.0f, dcY = 0.0f;
            float fb = 0.0f, damp = 0.0f;
            float lastTime = -1.0f, lastFb = -1.0f, lastDamp = -1.0f, lastStereo = -1.0f;
        };

        std::array<Line, kMaxChannels> line {};
        double sr = 48000.0;
        float maxDelay = 0.0f, dcR = 0.0f;
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
        void all (float t, float f, float d, float s) { set (M_TIME, t); set (M_FB, f); set (M_DAMP, d); set (M_STEREO, s); }

        void impulse()
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.setSample (ch, 0, 1.0f);
        }
        void burst (double freqHz, double sampleRate, int len, float amp)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < juce::jmin (len, n); ++i)
                    buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate));
        }
        double peak (int ch, int from, int to) const
        {
            double m = 0.0;
            for (int i = juce::jmax (0, from); i < juce::jmin (n, to); ++i) m = juce::jmax (m, (double) std::abs (buf.getSample (ch, i)));
            return m;
        }
        double rms (int ch, int from, int to) const
        {
            double acc = 0.0; int c = 0;
            for (int i = juce::jmax (0, from); i < juce::jmin (n, to); ++i) { const double v = buf.getSample (ch, i); acc += v * v; ++c; }
            return c > 0 ? std::sqrt (acc / (double) c) : 0.0;
        }
        int firstAbove (int ch, double th) const
        {
            for (int i = 0; i < n; ++i) if (std::abs (buf.getSample (ch, i)) > th) return i;
            return -1;
        }
    };

    inline double dBratio (double a, double b) noexcept
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-18, a) / juce::jmax (1.0e-18, b));
    }
    // Valeurs d'entrée qui donnent l'unité voulue (inverses des lois ci-dessus).
    inline float vTime (double seconds) noexcept { return (float) (std::log (seconds / kTimeMinS) / kTimeSpan); }
    inline float vFb   (double f)       noexcept { return (float) (f / kFbMax); }

    bool Delay::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.delay : " << what << "\n";
            all = all && ok;
        };

        // 1. Temps — une impulsion ressort exactement au temps demandé, et rien avant.
        {
            Bench a (1, 20000); a.all (vTime (0.100), 0.0f, 0.0f, 0.5f); a.impulse();
            prepare (kSr, 512); reset(); process (a.buf, a.p, a.n);
            Bench b (1, 20000); b.all (vTime (0.250), 0.0f, 0.0f, 0.5f); b.impulse();
            prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
            const int t1 = a.firstAbove (0, 1.0e-4), t2 = b.firstAbove (0, 1.0e-4);
            note (std::abs (t1 - 4800) <= 2 && std::abs (t2 - 12000) <= 2,
                  "temps : impulsion ressortie à " + juce::String (t1) + " (4800 attendus, 100 ms) et "
                  + juce::String (t2) + " (12000 attendus, 250 ms)");
        }
        // 2. Réinjection — chaque répétition vaut la précédente multipliée par le réglage.
        {
            Bench b (1, 20000); b.all (vTime (0.050), vFb (0.5), 0.0f, 0.5f); b.impulse();
            prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
            const double p1 = b.peak (0, 2390, 2410), p2 = b.peak (0, 4790, 4810), p3 = b.peak (0, 7190, 7210);
            const double r2 = p2 / juce::jmax (1.0e-18, p1), r3 = p3 / juce::jmax (1.0e-18, p2);
            note (std::abs (r2 - 0.5) < 0.02 && std::abs (r3 - 0.5) < 0.02,
                  "réinjection 0,50 : rapports entre répétitions " + juce::String (r2, 4) + " et " + juce::String (r3, 4));
        }
        // 3. Réinjection bornée — à 1 la boucle vaut kFbMax, jamais 1 : rien ne monte.
        {
            Bench b (1, 40000); b.all (vTime (0.050), 1.0f, 0.0f, 0.5f); b.impulse();
            prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
            const double p8 = b.peak (0, 8 * 2400 - 10, 8 * 2400 + 10), p9 = b.peak (0, 9 * 2400 - 10, 9 * 2400 + 10);
            const double r = p9 / juce::jmax (1.0e-18, p8);
            const double top = b.peak (0, 0, 40000);
            note (std::abs (r - (double) kFbMax) < 0.02 && top <= 1.001,
                  "réinjection à 1 : rapport de boucle " + juce::String (r, 4) + " (0,95 attendu), crête totale "
                  + juce::String (top, 4) + " (jamais au-dessus de l'impulsion)");
        }
        // 4. Amortissement — la boucle perd l'aigu et garde le grave.
        {
            auto echo2 = [&] (double freqHz, float dampV)
            {
                Bench b (1, 16000); b.all (vTime (0.100), vFb (0.6), dampV, 0.5f);
                b.burst (freqHz, kSr, 2400, 0.5f);
                prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
                return b.rms (0, 9600, 12000);
            };
            const double hi = dBratio (echo2 (8000.0, 1.0f), echo2 (8000.0, 0.0f));
            const double lo = dBratio (echo2 (200.0,  1.0f), echo2 (200.0,  0.0f));
            note (hi <= -12.0 && std::abs (lo) <= 2.0,
                  "amortissement : deuxième répétition à " + juce::String (hi, 1) + " dB à 8 kHz (≤ -12) et "
                  + juce::String (lo, 2) + " dB à 200 Hz (|x| ≤ 2)");
        }
        // 5. Décalage stéréo — à 1, la gauche répète à la moitié du temps, la droite à une fois et demie.
        {
            Bench b (2, 20000); b.all (vTime (0.100), 0.0f, 0.0f, 1.0f); b.impulse();
            prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
            const int l = b.firstAbove (0, 1.0e-4), r = b.firstAbove (1, 1.0e-4);
            note (std::abs (l - 2400) <= 2 && std::abs (r - 7200) <= 2,
                  "décalage stéréo à 1 : gauche à " + juce::String (l) + " (2400), droite à " + juce::String (r) + " (7200)");
        }
        // 6. Queue (§3.3.2) — impulsion puis silence : la queue décroît sans jamais remonter,
        //    et reset() la purge. C'est le cas du pas désactivé en « laissée mourir » : le socle
        //    cesse d'alimenter la ligne, le reste est à la charge de la skill.
        {
            Bench b (1, 96000); b.all (vTime (0.100), vFb (0.7), 0.3f, 0.5f); b.impulse();
            prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
            bool falls = true;
            double prev = b.peak (0, 4800, 9600), last = prev;
            for (int k = 2; k < 20; ++k)
            {
                const double cur = b.peak (0, k * 4800, (k + 1) * 4800);
                falls = falls && cur < prev;
                prev = cur; last = cur;
            }
            Bench s (1, 512); s.all (vTime (0.100), vFb (0.7), 0.3f, 0.5f);
            reset(); process (s.buf, s.p, s.n);
            const double after = s.peak (0, 0, 512);
            note (falls && last < 1.0e-2 && after == 0.0,
                  "queue : 19 fenêtres de 100 ms strictement décroissantes, dernière crête "
                  + juce::String (last, 6) + ", silence exact après reset (" + juce::String (after, 1) + ")");
        }
        return all;
    }
}

void registerDelay()
{
    SkillRegistry::instance().add (Delay().info(), [] { return std::make_unique<Delay>(); });
}
}
