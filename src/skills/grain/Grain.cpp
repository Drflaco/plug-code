// Grain — pitch shift granulaire à durée préservée (CdC §3.8, ligne « Pitch shift
// granulaire », le seul effet LATENT du catalogue ; gestes 1 et 2 ; contrat §3.9).
// Invariants et leur raison :
//   · deux têtes de lecture en opposition de phase dans une fenêtre de grain, fondues
//     par une fenêtre de Hann complémentaire (wA + wB = 1) : l'écriture avance à 1×
//     pendant que les têtes tournent, donc la durée ne bouge pas quand la hauteur
//     change. C'est ce qui sépare cette skill de core.repitch (§3.8, deux lignes) ;
//   · latence déclarée = moitié du PLUS GRAND grain + 2 échantillons de garde
//     d'interpolation, calculée dans prepare(), constante entre deux prepare() (§4.3).
//     La fenêtre est centrée sur elle : les têtes s'écartent de ±taille/2 autour de la
//     latence. C'est ce qui rend la taille de grain LIBRE — elle module par pas sans
//     jamais déplacer la latence déclarée, donc sans décaler la piste ;
//   · à hauteur neutre la phase reste à zéro : la tête B pèse exactement 1 et lit
//     exactement la latence déclarée, la tête A pèse 0. La sortie est alors le sec
//     retardé, sans peigne — un pitch à l'unité qui colore serait un défaut ;
//   · aucune resynchronisation sur un saut de hauteur : un saut ne change que la
//     VITESSE des têtes, pas leur position. Le coût par bloc ne dépend donc pas de ce
//     que fait le séquenceur (annexe A.0, point 3) — c'est la raison du pitch fait
//     main plutôt que spectral (J4a, lot D) ;
//   · loi -3 dB — le traité n'est pas en phase avec le sec (§3.7, « granulaire ») ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp) ;
//   · rien n'est alloué dans process() : les deux lignes à retard sont dimensionnées au
//     pire cas dans prepare() (§4.2), et ce qui entre dans la boucle de réinjection est
//     borné en dur pour qu'un emplacement ne puisse pas diverger.
#include "Grain.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_PITCH = 0, M_SIZE = 1, M_FB = 2, M_STEREO = 9;

    constexpr float kPitchSemis  = 24.0f;          // ±2 octaves de part et d'autre du centre
    constexpr double kGrainMinS  = 0.005;          // 5 ms : le grain chante, métallique
    constexpr double kGrainMaxS  = 0.100;          // 100 ms : le grain s'étale, il flange
    constexpr float kGrainSpan   = 2.995732274f;   // ln(100 / 5) = ln(20)
    constexpr float kFbMax       = 0.8f;           // au-delà la boucle s'emballe : borne de conception
    constexpr float kDetuneCents = 50.0f;          // écart stéréo maximal, par canal
    constexpr int   kGuard       = 2;              // retard minimal : l'interpolation lit un point avant
    constexpr float kLoopCeiling = 32.0f;          // borne dure de la boucle (§4.2 : le bloc suivant reste fini)
    constexpr double kSmoothS    = 0.005;          // anti-zipper interne, raison dans process()
    constexpr size_t kMaxChannels = 2;

    inline float rateFor (float v) noexcept
    {
        const float semis = (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0f * kPitchSemis;
        return std::exp2 (semis / 12.0f);          // v = 0,5 rend exactement 1,0 : la transparence en dépend
    }

    inline float grainFor (float v, double sr, int maxGrain) noexcept
    {
        const float g = (float) (kGrainMinS * sr) * std::exp (juce::jlimit (0.0f, 1.0f, v) * kGrainSpan);
        return juce::jlimit (4.0f, (float) maxGrain, g);
    }

    // Lisibilité (J4b c-2) : les conversions de process(), rendues lisibles.
    juce::String dispPitch  (float v) { return display::sig ((double) (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0 * kPitchSemis); }
    juce::String dispSize   (float v) { return display::sig (1000.0 * kGrainMinS * std::exp ((double) juce::jlimit (0.0f, 1.0f, v) * kGrainSpan)); }
    juce::String dispFb     (float v) { return display::sig (100.0f * kFbMax * juce::jlimit (0.0f, 1.0f, v)); }
    juce::String dispStereo (float v) { return display::sig ((double) (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0 * kDetuneCents); }

    inline float detuneFor (float v) noexcept
    {
        return std::exp2 ((juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0f * kDetuneCents / 1200.0f);
    }

    // Lecture à retard fractionnaire, Catmull-Rom 4 points. À fraction nulle elle rend
    // l'échantillon tel quel : c'est de là que vient l'exactitude de la latence déclarée.
    inline float readCubic (const float* b, int size, int mask, int w, float delay) noexcept
    {
        const int i0 = (int) delay;
        const float t = delay - (float) i0;
        const int q = (w - i0 + size) & mask;
        const float p0 = b[(q + 1) & mask];                // retard i0 - 1
        const float p1 = b[q];                             // retard i0
        const float p2 = b[(q + size - 1) & mask];         // retard i0 + 1
        const float p3 = b[(q + size - 2) & mask];         // retard i0 + 2
        return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3
                                               + t * (3.0f * (p1 - p2) + p3 - p0)));
    }

    //==========================================================================
    class Grain : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.grain", 1, "Pitch granulaire", MixLaw::Minus3, false,
                {
                    { M_PITCH,  "Hauteur",
                      "Décale la hauteur de -24 à +24 demi-tons (0,5 = hauteur d'origine) sans changer la durée : ce qui entre en deux secondes ressort en deux secondes. Verrouillé par défaut (§3.3.1) : c'est le réglage qui éloigne le plus vite le traité de la tonalité de la source, et une hauteur tirée à chaque pas casse la cohérence harmonique cherchée. Ici le saut ne coûte aucune resynchronisation — il ne change que la vitesse des têtes de lecture — donc le déverrouiller est une décision musicale, pas un risque technique.",
                      LockClass::LockedByDefault, false, "demi-tons", dispPitch },
                    { M_SIZE,   "Taille de grain",
                      "Longueur de la fenêtre de lecture, de 5 ms à 100 ms, course exponentielle (le milieu tombe vers 22 ms). Court, le grain chante et devient métallique ; long, le son s'étale et flange. Libre : la taille module par pas sans toucher à la latence déclarée.",
                      LockClass::Free, true, "ms", dispSize },
                    { M_FB,     "Réinjection",
                      "Renvoie le traité dans la ligne à retard, de 0 à 0,8. Chaque tour repasse par le décalage : avec une hauteur montante, la queue monte indéfiniment en spirale. Libre.",
                      LockClass::Free, true, "%", dispFb },
                    { M_STEREO, "Écart stéréo",
                      "Désaccorde les deux canaux, jusqu'à 50 centièmes de demi-ton par canal (0,5 = aucun écart, gauche et droite rigoureusement identiques ; 0 et 1 = un demi-ton entre les deux, le sens s'inversant de part et d'autre du centre). Libre.",
                      LockClass::Free, true, "cents", dispStereo },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            maxGrain = (int) std::lround (kGrainMaxS * sr);
            latency  = kGuard + maxGrain / 2;
            maxDelay = latency + maxGrain / 2;

            int size = 8;
            while (size < maxDelay + kGuard + 4) size <<= 1;    // puissance de deux : l'indexation est un masque
            mask = size - 1;
            for (auto& line : lines) line.assign ((size_t) size, 0.0f);

            smoothCoef = (float) (1.0 - std::exp (-1.0 / (kSmoothS * sr)));
            reset();
        }

        void reset() override
        {
            for (auto& line : lines) std::fill (line.begin(), line.end(), 0.0f);   // ne réalloue pas
            phase = { 0.0f, 0.0f };
            writePos = -1;
            primed = false;
            lastPitch = lastSize = lastFb = lastStereo = -1.0f;
            baseRate = 1.0f; detune = 1.0f;
            sizeTarget = sizeSm = (float) (kGrainMinS * sr);
            fbTarget = fbSm = 0.0f;
        }

        int latencySamples() const override { return latency; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0) return;
            juce::ScopedNoDenormals noDenormals;

            const float* pi = p[M_PITCH];
            const float* si = p[M_SIZE];
            const float* fi = p[M_FB];
            const float* ti = p[M_STEREO];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            const bool stereo = chans >= 2;
            std::array<float*, kMaxChannels> d {};
            for (int ch = 0; ch < chans; ++ch) d[(size_t) ch] = wet.getWritePointer (ch);
            const int size = mask + 1;

            for (int i = 0; i < n; ++i)
            {
                // Conversions seulement quand une entrée bouge : un pas tenu ne paie ni
                // exp() ni exp2() (le cas ordinaire, annexe A.0).
                if (pi[i] != lastPitch)  { lastPitch  = pi[i]; baseRate   = rateFor (pi[i]); }
                if (si[i] != lastSize)   { lastSize   = si[i]; sizeTarget = grainFor (si[i], sr, maxGrain); }
                if (fi[i] != lastFb)     { lastFb     = fi[i]; fbTarget   = kFbMax * juce::jlimit (0.0f, 1.0f, fi[i]); }
                if (ti[i] != lastStereo) { lastStereo = ti[i]; detune     = detuneFor (ti[i]); }

                // Taille et réinjection déplacent une position de lecture et un gain de
                // boucle : sans lissage, un saut de pas s'entend comme un clic. La vitesse,
                // elle, n'est PAS lissée — un saut de hauteur ne déplace aucune tête, il ne
                // change que la pente de la phase. Ce lissage ne remplace pas le mode de
                // transition du pas (§3.3) : il borne la marche, il ne la décide pas.
                if (! primed) { sizeSm = sizeTarget; fbSm = fbTarget; primed = true; }
                sizeSm += (sizeTarget - sizeSm) * smoothCoef;
                fbSm   += (fbTarget   - fbSm)   * smoothCoef;

                writePos = (writePos + 1) & mask;
                const float w = sizeSm;

                for (int ch = 0; ch < chans; ++ch)
                {
                    const float rate = ! stereo ? baseRate : (ch == 0 ? baseRate / detune : baseRate * detune);
                    float& ph = phase[(size_t) ch];

                    // Têtes en opposition : A balaie [latence - w/2, latence + w/2[, B la
                    // même course décalée d'une demi-fenêtre. Les deux poids somment à 1.
                    const float dA = (float) latency + (ph - 0.5f) * w;
                    const float dB = (float) latency + (ph < 0.5f ? ph : ph - 1.0f) * w;
                    const float wA = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * ph);

                    float* line = lines[(size_t) ch].data();
                    const float out = wA * readCubic (line, size, mask, writePos, dA)
                                    + (1.0f - wA) * readCubic (line, size, mask, writePos, dB);

                    line[writePos] = juce::jlimit (-kLoopCeiling, kLoopCeiling, d[(size_t) ch][i] + fbSm * out);
                    d[(size_t) ch][i] = out;

                    ph += (1.0f - rate) / w;
                    ph -= std::floor (ph);
                }
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        std::array<std::vector<float>, kMaxChannels> lines {};
        std::array<float, kMaxChannels> phase { { 0.0f, 0.0f } };
        double sr = 48000.0;
        int maxGrain = 4800, latency = 2402, maxDelay = 4802, mask = 8191, writePos = -1;
        bool primed = false;
        float smoothCoef = 0.0f, baseRate = 1.0f, detune = 1.0f;
        float sizeTarget = 240.0f, sizeSm = 240.0f, fbTarget = 0.0f, fbSm = 0.0f;
        float lastPitch = -1.0f, lastSize = -1.0f, lastFb = -1.0f, lastStereo = -1.0f;
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
        void all (float pitchV, float sizeV, float fbV, float stereoV)
        {
            set (M_PITCH, pitchV); set (M_SIZE, sizeV); set (M_FB, fbV); set (M_STEREO, stereoV);
        }
        float* curve (int m) { return store[(size_t) m].data(); }

        void sine (double freqHz, double sampleRate, float amp, int from = 0, int to = -1)
        {
            const int end = to < 0 ? n : to;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = from; i < end; ++i)
                    buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate));
        }
        void noise (juce::Random& rng, float amp)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, amp * (rng.nextFloat() * 2.0f - 1.0f));
        }
        void impulse (float amp) { for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.setSample (ch, 0, amp); }
    };

    // Valeurs d'entrée qui donnent l'unité demandée (inverses des lois ci-dessus).
    inline float vPitch (double semis) noexcept { return (float) (0.5 + semis / (2.0 * kPitchSemis)); }
    inline float vSize  (double ms)    noexcept { return (float) (std::log (0.001 * ms / kGrainMinS) / kGrainSpan); }
    inline float vFb    (double g)     noexcept { return (float) (g / kFbMax); }

    // Fréquence lue au compteur de passages par zéro montants : la modulation d'amplitude
    // des deux têtes ne déplace pas les zéros, donc la mesure y est insensible.
    inline double zeroCrossHz (const juce::AudioBuffer<float>& b, int ch, int from, int to, double sampleRate)
    {
        int c = 0;
        for (int i = from + 1; i < to; ++i)
            if (b.getSample (ch, i - 1) < 0.0f && b.getSample (ch, i) >= 0.0f) ++c;
        return (double) c * sampleRate / (double) (to - from);
    }

    bool Grain::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.grain : " << what << "\n";
            all = all && ok;
        };
        const float mid = 0.5f, size20 = vSize (20.0);

        // 1. Hauteur neutre — la sortie est le sec retardé de la latence déclarée, et rien
        //    d'autre : ni peigne, ni seconde tête audible.
        {
            prepare (kSr, 512); reset();
            const int L = latencySamples();
            Bench b (1, L + 512);
            b.impulse (1.0f);
            b.all (mid, size20, 0.0f, mid);
            process (b.buf, b.p, b.n);
            double before = 0.0, after = 0.0;
            for (int i = 0; i < L; ++i)       before = juce::jmax (before, (double) std::abs (b.buf.getSample (0, i)));
            for (int i = L + 1; i < b.n; ++i) after  = juce::jmax (after,  (double) std::abs (b.buf.getSample (0, i)));
            const double at = b.buf.getSample (0, L);
            note (before < 1.0e-12 && after < 1.0e-9 && std::abs (at - 1.0) < 1.0e-6,
                  "hauteur neutre : impulsion rendue intacte à l'échantillon " + juce::String (L)
                      + " (valeur " + juce::String (at, 9) + "), rien avant (" + juce::String (before, 12)
                      + ") ni après (" + juce::String (after, 12) + ")");
        }

        // 2. Hauteur — un sinus à 500 Hz ressort une octave au-dessus, puis une en dessous,
        //    à niveau conservé (les deux poids somment à 1).
        {
            auto run = [&] (double semis)
            {
                prepare (kSr, 512); reset();
                Bench b (1, 60000);
                b.sine (500.0, kSr, 0.5f);
                b.all (vPitch (semis), size20, 0.0f, mid);
                process (b.buf, b.p, b.n);
                const int from = latencySamples() + 4000;
                double acc = 0.0;
                for (int i = from; i < b.n; ++i) acc += (double) b.buf.getSample (0, i) * b.buf.getSample (0, i);
                return std::make_pair (zeroCrossHz (b.buf, 0, from, b.n, kSr), std::sqrt (acc / (double) (b.n - from)));
            };
            const auto up = run (12.0), down = run (-12.0);
            const double ref = 0.5 / std::sqrt (2.0);
            note (std::abs (up.first - 1000.0) < 25.0 && std::abs (down.first - 250.0) < 12.0
                      && up.second > 0.7 * ref && up.second < 1.3 * ref
                      && down.second > 0.7 * ref && down.second < 1.3 * ref,
                  "hauteur : 500 Hz rendu à " + juce::String (up.first, 1) + " Hz à +12 demi-tons et "
                      + juce::String (down.first, 1) + " Hz à -12 (1000 et 250 attendus), niveaux "
                      + juce::String (up.second, 4) + " et " + juce::String (down.second, 4)
                      + " pour " + juce::String (ref, 4));
        }

        // 3. Durée préservée — la propriété qui sépare core.grain de core.repitch : une
        //    salve de 100 ms décalée d'une octave dure encore 100 ms, plus un grain.
        {
            prepare (kSr, 512); reset();
            const int L = latencySamples(), burst = 4800;
            Bench b (1, 24000);
            b.sine (500.0, kSr, 0.5f, 0, burst);
            b.all (vPitch (12.0), size20, 0.0f, mid);
            process (b.buf, b.p, b.n);
            int first = -1, last = -1;
            for (int i = 0; i < b.n; ++i)
                if (std::abs (b.buf.getSample (0, i)) > 0.01f) { if (first < 0) first = i; last = i; }
            const int len = last - first;
            note (first > 0 && len > (int) (0.85 * burst) && len < (int) (burst + 2.5 * 960.0),
                  "durée préservée : salve de " + juce::String (burst) + " échantillons rendue en "
                      + juce::String (len) + " (un varispeed en rendrait " + juce::String (burst / 2)
                      + "), début à " + juce::String (first) + " pour une latence de " + juce::String (L));
        }

        // 4. Taille de grain — le fondu des deux têtes bat à la période que donne la loi :
        //    la puissance au croisement (phase 1/4) vaut la moitié de celle en butée
        //    (phase 0). Lue avec la BONNE taille, la mesure trouve les 3 dB ; lue avec une
        //    taille fausse, elle ne trouve plus rien. C'est la loi elle-même qui est testée.
        {
            auto ratio = [&] (double ms, double assumedScale)
            {
                prepare (kSr, 512); reset();
                Bench b (1, 96000);
                juce::Random rng (20260916);
                b.noise (rng, 0.5f);
                b.all (vPitch (12.0), vSize (ms), 0.0f, mid);
                process (b.buf, b.p, b.n);
                const double w = 0.001 * ms * kSr * assumedScale;
                double pk = 0.0, dip = 0.0; int ck = 0, cd = 0;
                for (int i = 12000; i < b.n; ++i)
                {
                    double ph = -(double) i / w;      // +12 demi-tons : la phase recule de 1/w par échantillon
                    ph -= std::floor (ph);
                    const double e = (double) b.buf.getSample (0, i) * b.buf.getSample (0, i);
                    const double toEdge = std::min (std::min (ph, 1.0 - ph), std::abs (ph - 0.5));
                    const double toMid  = std::min (std::abs (ph - 0.25), std::abs (ph - 0.75));
                    if      (toEdge < 0.05) { pk  += e; ++ck; }
                    else if (toMid  < 0.05) { dip += e; ++cd; }
                }
                return (pk / (double) juce::jmax (1, ck)) / juce::jmax (1.0e-30, dip / (double) juce::jmax (1, cd));
            };
            const double r10 = ratio (10.0, 1.0), r60 = ratio (60.0, 1.0), wrong = ratio (10.0, 1.37);
            note (r10 > 1.7 && r10 < 2.3 && r60 > 1.7 && r60 < 2.3 && wrong < 1.3,
                  "taille de grain : creux de croisement mesuré à " + juce::String (r10, 3) + " (10 ms) et "
                      + juce::String (r60, 3) + " (60 ms), 2,0 attendus ; avec une taille fausse de 37 %, "
                      + juce::String (wrong, 3) + " (le creux a disparu)");
        }

        // 5. Réinjection — l'impulsion revient un tour plus tard et l'énergie du retour suit
        //    le carré du réglage.
        {
            auto tail = [&] (float fb)
            {
                prepare (kSr, 512); reset();
                const int L = latencySamples();
                Bench b (1, 3 * L);
                b.impulse (1.0f);
                b.all (vPitch (12.0), size20, fb, mid);
                process (b.buf, b.p, b.n);
                double e = 0.0;
                for (int i = 2 * L - 1440; i < juce::jmin (b.n, 2 * L + 1440); ++i)
                    e += (double) b.buf.getSample (0, i) * b.buf.getSample (0, i);
                return e;
            };
            const double e0 = tail (0.0f), e3 = tail (vFb (0.3)), e6 = tail (vFb (0.6));
            note (e0 < 1.0e-12 && e3 > 1.0e-6 && e6 > 2.5 * e3,
                  "réinjection : énergie du second tour " + juce::String (e0, 12) + " à 0, "
                      + juce::String (e3, 6) + " à 0,3 et " + juce::String (e6, 6) + " à 0,6 (×4 attendu)");
        }

        // 6. Écart stéréo — au centre les deux canaux sont identiques ; à fond ils divergent,
        //    parce que leurs têtes ne tournent plus à la même vitesse.
        {
            auto spread = [&] (float stereoV)
            {
                prepare (kSr, 512); reset();
                Bench b (2, 48000);
                b.sine (500.0, kSr, 0.5f);
                b.all (mid, size20, 0.0f, stereoV);
                process (b.buf, b.p, b.n);
                double diff = 0.0, ref = 0.0;
                for (int i = 24000; i < b.n; ++i)
                {
                    const double l = b.buf.getSample (0, i), r = b.buf.getSample (1, i);
                    diff += (l - r) * (l - r); ref += l * l;
                }
                return std::sqrt (diff / juce::jmax (1.0e-30, ref));
            };
            const double none = spread (mid), wide = spread (1.0f);
            note (none < 1.0e-6 && wide > 0.2,
                  "écart stéréo : différence gauche/droite " + juce::String (none, 9) + " au centre, "
                      + juce::String (wide, 3) + " à fond (> 0,2 attendu)");
        }

        // 7. Hauteur qui change à CHAQUE PAS (cas demandé par le pilote) — un palier tous les
        //    128 échantillons sur 16384, servi en blocs de 128 comme le fait le socle : la
        //    sortie reste finie, bornée, sans marche, et la latence déclarée ne bouge pas.
        {
            prepare (kSr, 512); reset();
            const int before = latencySamples();
            Bench b (1, 16384);
            b.sine (220.0, kSr, 0.5f);
            b.all (mid, mid, 0.0f, mid);
            static const float steps[8] = { 0.5f, 0.95f, 0.1f, 0.72f, 0.28f, 1.0f, 0.0f, 0.62f };
            float* pc = b.curve (M_PITCH);
            for (int i = 0; i < b.n; ++i) pc[i] = steps[(i / 128) % 8];
            for (int off = 0; off < b.n; off += 128)
            {
                juce::AudioBuffer<float> sub (b.buf.getArrayOfWritePointers(), 1, off, 128);
                ParamCurves c;
                c.v[(size_t) M_PITCH]  = b.curve (M_PITCH)  + off;
                c.v[(size_t) M_SIZE]   = b.curve (M_SIZE)   + off;
                c.v[(size_t) M_FB]     = b.curve (M_FB)     + off;
                c.v[(size_t) M_STEREO] = b.curve (M_STEREO) + off;
                process (sub, c, 128);
            }
            double peak = 0.0, jump = 0.0;
            bool finite = true;
            for (int i = 1; i < b.n; ++i)
            {
                const double v = b.buf.getSample (0, i);
                finite = finite && std::isfinite (v);
                peak = juce::jmax (peak, std::abs (v));
                if (i > before) jump = juce::jmax (jump, std::abs (v - b.buf.getSample (0, i - 1)));
            }
            note (finite && peak < 0.6 && jump < 0.12 && latencySamples() == before,
                  "128 paliers de hauteur à 128 échantillons : crête " + juce::String (peak, 4)
                      + " (entrée 0,5), plus grand écart entre deux échantillons " + juce::String (jump, 4)
                      + " (la pente propre du signal vaut déjà 0,06), latence "
                      + juce::String (latencySamples()) + " inchangée");
        }
        return all;
    }
}

void registerGrain()
{
    SkillRegistry::instance().add (Grain().info(), [] { return std::make_unique<Grain>(); });
}
}
