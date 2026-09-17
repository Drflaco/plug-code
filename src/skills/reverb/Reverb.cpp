// Reverb — réverbe à huit peignes amortis et quatre passe-tout par canal
// (CdC §3.8, ligne « Réverbe », latence nulle ; contrat §3.9 ; sert le geste 1 avec le gate).
// Invariants et leur raison :
//   · latence nulle — aucune pré-écoute, aucun bloc à accumuler : la sortie d'un
//     échantillon ne dépend que du passé des lignes (§4.3) ;
//   · loi -3 dB — le traité est décorrélé du sec, jamais en phase avec lui (§3.7) ;
//   · lignes allouées UNE fois à la taille maximale (kCombMs et kApMs, constantes de
//     compilation, au facteur de taille 1) et jamais réallouées : changer la taille ne
//     fait que raccourcir la longueur utile à l'intérieur de la même ligne (§4.2) ;
//   · réinjection de peigne bornée à kGMax, strictement sous 1 — la boucle d'un peigne
//     est une récursion pure : à 1 elle ne s'éteint jamais. La borne plafonne la
//     décroissation réelle aux très petites tailles, et c'est assumé : une réverbe qui
//     ne s'éteint pas n'est pas une réverbe ;
//   · passe-haut de boucle NORMALISÉ ((1+R)/2) — sa forme brute gagne 2/(1+R) dans
//     l'aigu, ce qui ferait passer la boucle au-dessus de 1 quand le grave est coupé
//     haut et la décroissance longue. Normalisé, le gain de boucle reste kGMax ;
//   · passe-tout vrais ((z^-L - g)/(1 - g z^-L), |H| = 1) — ils diffusent sans colorer
//     ni changer la décroissance, qui reste celle des peignes seuls. C'est ce qui rend
//     la décroissance mesurable et donc testable ;
//   · la taille est VERROUILLÉE PAR DÉFAUT (§3.3.1) et en saut : changer la taille
//     déplace les têtes des peignes d'un coup, la queue en cours se replie et
//     s'entend comme un décrochage. Elle ne touche ni à la latence ni à l'allocation,
//     donc elle n'est pas structurelle — juste coûteuse à moduler ;
//   · queue : quand l'entrée devient silencieuse (pas désactivé en « laissée mourir »,
//     §3.3.2), chaque peigne continue sur son seul contenu et perd son facteur g à
//     chaque tour ; reset() purge les lignes et tous les états de filtre ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp, chans) ;
//   · rien n'est alloué dans process() : tout vient de prepare() (§4.2).
#include "Reverb.h"
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
    constexpr int M_DECAY = 0, M_DAMP = 1, M_SIZE = 2, M_DIFF = 3, M_BASS = 4, M_STEREO = 9;

    constexpr int kCombs = 8, kAllp = 4;
    // Longueurs de base en millisecondes, au facteur de taille 1. Elles dimensionnent
    // l'allocation et ne bougent jamais ; leurs écarts sont irréguliers pour que les
    // périodes des peignes ne se superposent pas en un timbre de peigne unique.
    constexpr float kCombMs[kCombs] = { 29.7f, 31.9f, 34.1f, 36.7f, 39.1f, 41.3f, 43.7f, 46.1f };
    constexpr float kApMs[kAllp]    = { 9.7f, 7.3f, 5.9f, 4.3f };
    constexpr float kSpreadMs  = 0.5f;          // décalage fixe du canal droit : décorrélation
    constexpr float kSizeMin   = 0.35f;         // facteur de taille minimal
    constexpr float kRt60Min   = 0.2f;          // décroissance : 0,2 s …
    constexpr float kRt60Span  = 3.401197382f;  // … à 6 s (ln 30)
    constexpr float kGMax      = 0.985f;        // borne stricte de la boucle d'un peigne
    constexpr float kDampMax   = 0.9f;          // coefficient du passe-bas de boucle
    constexpr float kDiffMax   = 0.7f;          // coefficient des passe-tout
    constexpr float kBassMinHz = 20.0f;         // passe-haut de boucle : 20 Hz …
    constexpr float kBassSpan  = 3.218875825f;  // … à 500 Hz (ln 25)
    constexpr float kCombNorm  = 1.0f / (float) kCombs;   // la somme des huit reste au niveau de l'entrée
    constexpr float kMinus3Ln10 = -6.907755279f;          // -3 · ln(10) : RT60 → facteur de peigne

    constexpr size_t kMaxChannels = 2;

    inline float rt60Sec (float v) noexcept { return kRt60Min * std::exp (juce::jlimit (0.0f, 1.0f, v) * kRt60Span); }
    inline float bassHz  (float v) noexcept { return kBassMinHz * std::exp (juce::jlimit (0.0f, 1.0f, v) * kBassSpan); }
    // Quadratique : de la finesse près de zéro, où l'oreille entend la moindre perte d'aigu.
    inline float dampOf  (float v) noexcept { const float c = juce::jlimit (0.0f, 1.0f, v); return kDampMax * c * c; }
    inline float diffOf  (float v) noexcept { return kDiffMax * juce::jlimit (0.0f, 1.0f, v); }
    inline float sizeOf  (float v) noexcept { return kSizeMin + (1.0f - kSizeMin) * juce::jlimit (0.0f, 1.0f, v); }

    // Lisibilité (J4b c-2) : les conversions de process(), rendues lisibles.
    juce::String dispDecay  (float v) { return display::sig (rt60Sec (v)); }
    juce::String dispDamp   (float v) { return display::sig (100.0f * dampOf (v) / kDampMax); }
    juce::String dispSize   (float v) { return display::sig (100.0f * sizeOf (v)); }
    juce::String dispDiff   (float v) { return display::sig (100.0f * diffOf (v) / kDiffMax); }
    juce::String dispBass   (float v) { return display::sig (bassHz (v)); }
    juce::String dispWidth  (float v) { return display::sig (100.0f * juce::jlimit (0.0f, 1.0f, v)); }

    //==========================================================================
    class Reverb : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.reverb", 1, "Réverbe", MixLaw::Minus3, false,
                {
                    { M_DECAY,  "Décroissance",
                      "Temps de décroissance à -60 dB, de 0,2 s à 6 s, course exponentielle. La réinjection de chaque peigne est bornée à 0,985 : aux très petites tailles la décroissance plafonne avant 6 s, c'est le prix d'une queue qui s'éteint toujours. Libre : c'est le réglage que le séquenceur fait respirer.",
                      LockClass::Free, true, "s", dispDecay },
                    { M_DAMP,   "Amortissement",
                      "Passe-bas à un pôle dans chaque peigne : la queue perd son aigu avant son grave, comme une pièce meublée. 0 = aucun filtrage, 0,5 = coupure vers 11 kHz, 1 = vers 800 Hz (valeurs à 48 kHz : le filtre est défini par son coefficient, sa coupure suit donc la fréquence d'échantillonnage). Libre.",
                      LockClass::Free, true, "%", dispDamp },
                    { M_SIZE,   "Taille",
                      "Longueur des lignes, de 35 % à 100 % (première réflexion de 10 ms à 30 ms). Verrouillée par défaut : la changer déplace d'un coup les têtes des huit peignes, et la queue en cours se replie — ça s'entend comme un décrochage, pas comme une pièce qui grandit. Déverrouille-la si c'est le décrochage qui est cherché ; rien ne l'interdit, ni la latence ni l'allocation.",
                      LockClass::LockedByDefault, false, "%", dispSize },
                    { M_DIFF,   "Diffusion",
                      "Coefficient des quatre passe-tout de sortie, de 0 à 0,7. À 0 les réflexions restent des échos distincts ; à fond elles s'étalent en nappe. Les passe-tout sont de gain unité : la diffusion ne change ni le niveau ni la durée de la queue. Libre.",
                      LockClass::Free, true, "%", dispDiff },
                    { M_BASS,   "Grave",
                      "Passe-haut à un pôle dans chaque peigne, de 20 Hz (le grave dure autant que le reste) à 500 Hz (le grave s'éteint bien plus vite). C'est le réglage qui empêche la queue d'empâter un mixage. Libre.",
                      LockClass::Free, true, "Hz", dispBass },
                    { M_STEREO, "Largeur",
                      "Ouverture de la queue : 0 = les deux canaux sont identiques (queue mono), 0,5 = demi-ouverture, 1 = chaque canal garde sa propre queue (les lignes du canal droit sont décalées d'une demi-milliseconde). Libre.",
                      LockClass::Free, true, "%", dispWidth },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            const int spread = juce::jmax (1, (int) (kSpreadMs * 0.001f * (float) sr));
            for (size_t ch = 0; ch < kMaxChannels; ++ch)
            {
                auto& b = bank[ch];
                b.spread = (int) ch * spread;
                // Dimensionnées au pire cas : facteur de taille 1 plus le décalage du canal.
                for (int c = 0; c < kCombs; ++c)
                {
                    const size_t n = (size_t) (kCombMs[c] * 0.001f * (float) sr) + (size_t) spread + 2;
                    if (b.comb[(size_t) c].size() != n) b.comb[(size_t) c].assign (n, 0.0f);   // hors audio (§4.2)
                }
                for (int a = 0; a < kAllp; ++a)
                {
                    const size_t n = (size_t) (kApMs[a] * 0.001f * (float) sr) + (size_t) spread + 2;
                    if (b.ap[(size_t) a].size() != n) b.ap[(size_t) a].assign (n, 0.0f);
                }
            }
            reset();
        }

        void reset() override
        {
            for (auto& b : bank)
            {
                for (auto& v : b.comb) std::fill (v.begin(), v.end(), 0.0f);   // purge de la queue
                for (auto& v : b.ap)   std::fill (v.begin(), v.end(), 0.0f);
                b.cidx = {}; b.aidx = {}; b.lp = {}; b.hx = {}; b.hy = {}; b.g = {};
                b.clen.fill (2); b.alen.fill (2);
                b.damp = b.diff = 0.0f; b.hpR = 0.0f; b.hpA = 0.0f;
                b.lastDecay = b.lastDamp = b.lastSize = b.lastDiff = b.lastBass = -1.0f;
            }
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0 || bank[0].comb[0].empty()) return;
            juce::ScopedNoDenormals noDenormals;

            const float* de = p[M_DECAY];
            const float* dm = p[M_DAMP];
            const float* sz = p[M_SIZE];
            const float* di = p[M_DIFF];
            const float* ba = p[M_BASS];
            const float* st = p[M_STEREO];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            // Boucle par échantillon : la largeur mélange les deux queues au même instant,
            // et un tampon de travail pour la garder serait une allocation de plus.
            std::array<float*, kMaxChannels> d {};
            for (int ch = 0; ch < chans; ++ch) d[(size_t) ch] = wet.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
            {
                std::array<float, kMaxChannels> y {};
                for (int ch = 0; ch < chans; ++ch)
                {
                    auto& b = bank[(size_t) ch];
                    update (b, de[i], dm[i], sz[i], di[i], ba[i]);
                    y[(size_t) ch] = tick (b, d[(size_t) ch][i]);
                }

                if (chans < 2) { d[0][i] = y[0]; continue; }

                const float w = juce::jlimit (0.0f, 1.0f, st[i]);
                const float keep = 0.5f + 0.5f * w, cross = 0.5f - 0.5f * w;
                d[0][i] = keep * y[0] + cross * y[1];
                d[1][i] = keep * y[1] + cross * y[0];
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        struct Bank
        {
            std::array<std::vector<float>, (size_t) kCombs> comb;
            std::array<std::vector<float>, (size_t) kAllp>  ap;
            std::array<int, (size_t) kCombs> cidx {}, clen {};
            std::array<float, (size_t) kCombs> g {}, lp {}, hx {}, hy {};
            std::array<int, (size_t) kAllp> aidx {}, alen {};
            int spread = 0;
            float damp = 0.0f, diff = 0.0f, hpR = 0.0f, hpA = 0.0f;
            float lastDecay = -1.0f, lastDamp = -1.0f, lastSize = -1.0f, lastDiff = -1.0f, lastBass = -1.0f;
        };

        // Conversions seulement quand une entrée bouge : un pas tenu ne paie ni exp()
        // ni division (le cas ordinaire, annexe A.0).
        void update (Bank& b, float dec, float dmp, float siz, float dif, float bas) noexcept
        {
            bool resized = false;
            if (siz != b.lastSize)
            {
                b.lastSize = siz;
                const float s = sizeOf (siz);
                for (int c = 0; c < kCombs; ++c)
                {
                    const int len = (int) (kCombMs[c] * 0.001f * s * (float) sr) + b.spread;
                    b.clen[(size_t) c] = juce::jlimit (2, (int) b.comb[(size_t) c].size(), len);
                    if (b.cidx[(size_t) c] >= b.clen[(size_t) c]) b.cidx[(size_t) c] = 0;
                }
                for (int a = 0; a < kAllp; ++a)
                {
                    const int len = (int) (kApMs[a] * 0.001f * s * (float) sr) + b.spread;
                    b.alen[(size_t) a] = juce::jlimit (2, (int) b.ap[(size_t) a].size(), len);
                    if (b.aidx[(size_t) a] >= b.alen[(size_t) a]) b.aidx[(size_t) a] = 0;
                }
                resized = true;
            }
            if (resized || dec != b.lastDecay)
            {
                b.lastDecay = dec;
                // g = 10^(-3 L / (RT60 · sr)) : chaque peigne perd 60 dB en RT60, quelle que
                // soit sa longueur — c'est ce qui fait une décroissance unique et mesurable.
                const float c = (float) (kMinus3Ln10 / (rt60Sec (dec) * sr));
                for (int k = 0; k < kCombs; ++k)
                    b.g[(size_t) k] = juce::jmin (kGMax, std::exp (c * (float) b.clen[(size_t) k]));
            }
            if (dmp != b.lastDamp) { b.lastDamp = dmp; b.damp = dampOf (dmp); }
            if (dif != b.lastDiff) { b.lastDiff = dif; b.diff = diffOf (dif); }
            if (bas != b.lastBass)
            {
                b.lastBass = bas;
                b.hpR = juce::jlimit (0.0f, 0.9999f, 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * bassHz (bas) / sr));
                b.hpA = 0.5f * (1.0f + b.hpR);     // normalisation : gain unité dans l'aigu, voir l'en-tête
            }
        }

        float tick (Bank& b, float x) noexcept
        {
            float sum = 0.0f;
            for (int c = 0; c < kCombs; ++c)
            {
                const size_t k = (size_t) c;
                const size_t idx = (size_t) b.cidx[k];
                const float out = b.comb[k][idx];
                sum += out;

                b.lp[k] += (1.0f - b.damp) * (out - b.lp[k]);            // damp = 0 : transparent
                const float h = b.hpA * (b.lp[k] - b.hx[k]) + b.hpR * b.hy[k];
                b.hx[k] = b.lp[k]; b.hy[k] = h;

                b.comb[k][idx] = x + b.g[k] * h;
                if (++b.cidx[k] >= b.clen[k]) b.cidx[k] = 0;
            }

            float y = sum * kCombNorm;
            for (int a = 0; a < kAllp; ++a)
            {
                const size_t k = (size_t) a;
                const size_t idx = (size_t) b.aidx[k];
                const float dly = b.ap[k][idx];
                const float v = y + b.diff * dly;
                y = dly - b.diff * v;                                    // passe-tout vrai : |H| = 1
                b.ap[k][idx] = v;
                if (++b.aidx[k] >= b.alen[k]) b.aidx[k] = 0;
            }
            return y;
        }

        std::array<Bank, kMaxChannels> bank {};
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
        void all (float dec, float dmp, float siz, float dif, float bas, float ste)
        {
            set (M_DECAY, dec); set (M_DAMP, dmp); set (M_SIZE, siz);
            set (M_DIFF, dif);  set (M_BASS, bas); set (M_STEREO, ste);
        }
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
    inline float vRt60 (double sec) noexcept { return (float) (std::log (sec / kRt60Min) / kRt60Span); }
    inline float vBass (double hz)  noexcept { return (float) (std::log (hz / kBassMinHz) / kBassSpan); }
    inline float vDiff (double g)   noexcept { return (float) (g / kDiffMax); }

    bool Reverb::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.reverb : " << what << "\n";
            all = all && ok;
        };

        // Une impulsion, puis le silence : rend le banc rempli de la seule queue.
        auto tail = [&] (int channels, int numSamples, float dec, float dmp, float siz, float dif, float bas, float ste)
        {
            auto b = std::make_unique<Bench> (channels, numSamples);
            b->all (dec, dmp, siz, dif, bas, ste); b->impulse();
            prepare (kSr, 512); reset(); process (b->buf, b->p, b->n);
            return b;
        };

        // 1. Décroissance — l'énergie perd 60 dB en RT60, quel que soit le RT60 demandé.
        {
            auto a = tail (1, 144000, vRt60 (1.0), 0.0f, 1.0f, vDiff (0.35), 0.0f, 1.0f);
            auto c = tail (1, 144000, vRt60 (3.0), 0.0f, 1.0f, vDiff (0.35), 0.0f, 1.0f);
            const double d1 = -dBratio (a->rms (0, 62400, 67200), a->rms (0, 14400, 19200));   // 1 s plus tard
            const double d3 = -dBratio (c->rms (0, 62400, 67200), c->rms (0, 14400, 19200));
            note (std::abs (d1 - 60.0) <= 12.0 && std::abs (d3 - 20.0) <= 8.0,
                  "décroissance : -" + juce::String (d1, 1) + " dB en 1 s pour 1 s demandé (60 attendus), -"
                  + juce::String (d3, 1) + " dB pour 3 s demandé (20 attendus)");
        }
        // 2. Amortissement — la queue perd son aigu et garde son grave.
        {
            auto late = [&] (double freqHz, float dampV)
            {
                Bench b (1, 24000);
                b.all (vRt60 (2.0), dampV, 1.0f, vDiff (0.35), 0.0f, 1.0f);
                b.burst (freqHz, kSr, 2400, 0.5f);
                prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
                return b.rms (0, 7200, 12000);      // 150 ms → 250 ms
            };
            const double hi = dBratio (late (10000.0, 1.0f), late (10000.0, 0.0f));
            const double lo = dBratio (late (200.0,   1.0f), late (200.0,   0.0f));
            note (hi <= -20.0 && std::abs (lo) <= 6.0,
                  "amortissement : queue à 150-250 ms à " + juce::String (hi, 1) + " dB à 10 kHz (≤ -20) et "
                  + juce::String (lo, 2) + " dB à 200 Hz (|x| ≤ 6)");
        }
        // 3. Taille — la première réflexion arrive à la longueur du plus court peigne.
        {
            auto big   = tail (1, 24000, vRt60 (2.0), 0.0f, 1.0f, vDiff (0.35), 0.0f, 1.0f);
            auto small = tail (1, 24000, vRt60 (2.0), 0.0f, 0.0f, vDiff (0.35), 0.0f, 1.0f);
            const int eBig   = (int) (kCombMs[0] * 0.001f * sizeOf (1.0f) * (float) kSr);
            const int eSmall = (int) (kCombMs[0] * 0.001f * sizeOf (0.0f) * (float) kSr);
            const int aBig = big->firstAbove (0, 1.0e-4), aSmall = small->firstAbove (0, 1.0e-4);
            note (aBig == eBig && aSmall == eSmall,
                  "taille : première réflexion à " + juce::String (aBig) + " (" + juce::String (eBig)
                  + " attendus, 100 %) et " + juce::String (aSmall) + " (" + juce::String (eSmall) + " attendus, 35 %)");
        }
        // 4. Diffusion — à 0 la queue est faite d'échos isolés (facteur de crête élevé),
        //    à fond elle est dense (facteur de crête bas). Le niveau, lui, ne bouge pas.
        {
            auto crest = [&] (float diffV)
            {
                auto b = tail (1, 24000, vRt60 (2.0), 0.0f, 1.0f, diffV, 0.0f, 1.0f);
                return b->peak (0, 0, 7200) / juce::jmax (1.0e-18, b->rms (0, 0, 7200));
            };
            const double sparse = crest (0.0f), dense = crest (1.0f);
            note (sparse > dense * 1.3,
                  "diffusion : facteur de crête sur 150 ms " + juce::String (sparse, 2) + " à 0 contre "
                  + juce::String (dense, 2) + " à fond (rapport ≥ 1,3)");
        }
        // 5. Grave — à fond, le grave s'éteint bien plus vite ; l'aigu n'y touche pas.
        {
            auto late = [&] (double freqHz, float bassV)
            {
                Bench b (1, 24000);
                b.all (vRt60 (2.0), 0.0f, 1.0f, vDiff (0.35), bassV, 1.0f);
                b.burst (freqHz, kSr, 2400, 0.5f);
                prepare (kSr, 512); reset(); process (b.buf, b.p, b.n);
                return b.rms (0, 14400, 19200);     // 300 ms → 400 ms
            };
            const double low  = dBratio (late (60.0,   1.0f), late (60.0,   0.0f));
            const double high = dBratio (late (4000.0, 1.0f), late (4000.0, 0.0f));
            note (low <= -15.0 && std::abs (high) <= 3.0,
                  "grave : queue à 300-400 ms à " + juce::String (low, 1) + " dB à 60 Hz (≤ -15) et "
                  + juce::String (high, 2) + " dB à 4 kHz (|x| ≤ 3)");
        }
        // 6. Largeur — à 0 les deux canaux sont le même signal ; à 1 chacun garde sa queue.
        {
            auto mono = tail (2, 48000, vRt60 (2.0), 0.0f, 1.0f, vDiff (0.35), 0.0f, 0.0f);
            auto wide = tail (2, 48000, vRt60 (2.0), 0.0f, 1.0f, vDiff (0.35), 0.0f, 1.0f);
            double dm = 0.0;
            for (int i = 0; i < 48000; ++i) dm = juce::jmax (dm, (double) std::abs (mono->buf.getSample (0, i) - mono->buf.getSample (1, i)));
            double acc = 0.0;
            for (int i = 0; i < 48000; ++i) { const double v = wide->buf.getSample (0, i) - wide->buf.getSample (1, i); acc += v * v; }
            const double diffRms = std::sqrt (acc / 48000.0), ref = wide->rms (0, 0, 48000);
            note (dm < 1.0e-9 && diffRms > 0.1 * ref,
                  "largeur : à 0 les canaux diffèrent de " + juce::String (dm, 12) + " ; à 1 l'écart vaut "
                  + juce::String (100.0 * diffRms / juce::jmax (1.0e-18, ref), 1) + " % de la queue (> 10 %)");
        }
        // 7. Queue (§3.3.2) — impulsion puis silence : la queue décroît sans jamais remonter,
        //    et reset() la purge. C'est le cas du pas désactivé en « laissée mourir » : le socle
        //    cesse d'alimenter la réverbe, le reste est à la charge de la skill.
        {
            auto b = tail (1, 96000, vRt60 (1.0), 0.3f, 1.0f, vDiff (0.35), 0.0f, 1.0f);
            bool falls = true;
            double prev = b->rms (0, 9600, 14400), last = prev;
            for (int k = 3; k < 15; ++k)
            {
                const double cur = b->rms (0, k * 4800, (k + 1) * 4800);
                falls = falls && cur < prev;
                prev = cur; last = cur;
            }
            Bench s (1, 512); s.all (vRt60 (1.0), 0.3f, 1.0f, vDiff (0.35), 0.0f, 1.0f);
            reset(); process (s.buf, s.p, s.n);
            const double after = s.peak (0, 0, 512);
            note (falls && last < 0.2 * b->rms (0, 9600, 14400) && after == 0.0,
                  "queue : 12 fenêtres de 100 ms strictement décroissantes, dernière à "
                  + juce::String (dBratio (last, b->rms (0, 9600, 14400)), 1)
                  + " dB de la première, silence exact après reset (" + juce::String (after, 1) + ")");
        }
        return all;
    }
}

void registerReverb()
{
    SkillRegistry::instance().add (Reverb().info(), [] { return std::make_unique<Reverb>(); });
}
}
