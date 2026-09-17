// Repitch — varispeed : la bande accélère ou ralentit, la hauteur ET la durée changent
// ensemble (CdC §3.8, ligne « Repitch / varispeed », latence faible ; geste 3 « repitch
// fondu » ; contrat §3.9).
// Invariants et leur raison :
//   · une seule tête de lecture, à taux variable, dans une ligne à retard : c'est la
//     définition du varispeed. Ce qui entre en deux secondes ressort en une ou en quatre.
//     C'est ce qui sépare cette skill de core.grain, qui préserve la durée (§3.8) ;
//   · la tête dérive forcément — plus vite que l'écriture quand on monte, moins vite quand
//     on descend. Elle est donc RECALÉE d'une portée par un fondu de 4 ms dès qu'elle
//     atteint une butée. Le recalage est la contrepartie assumée du varispeed dans un
//     insert : une bande infinie n'existe pas, il faut bien rendre la main au présent ;
//   · latence déclarée = 2 échantillons de garde d'interpolation + la longueur du fondu.
//     C'est la position de repos de la tête, et c'est le minimum : sous cette valeur, la
//     tête sortante du fondu lirait le futur. Calculée dans prepare(), constante entre
//     deux prepare() (§4.3). À hauteur neutre la tête ne bouge pas : la sortie est
//     exactement le sec retardé de cette latence, ni plus ni moins ;
//   · la hauteur glisse par défaut (§3.3, transition) : le geste 3 demande que le fondu
//     agisse sur le repitch lui-même, pas sur le mélange. L'inertie est exposée pour ça ;
//   · les deux têtes du fondu sont lues À CHAQUE échantillon, même hors recalage (la
//     sortante est alors confondue avec l'entrante et pèse zéro) : le coût par bloc ne
//     dépend pas de l'endroit où tombe un recalage (annexe A.0, point 3) ;
//   · les deux retards sont bornés en dur dans [garde, ligne] : pendant un fondu, un
//     désaccord stéréo peut pousser le taux au-delà de 2 et faire sortir la tête sortante
//     de la course. C'est une sécurité de lecture, pas un réglage ;
//   · loi -3 dB — le traité n'est pas en phase avec le sec (§3.7) ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp) ;
//   · rien n'est alloué dans process() : les deux lignes sont dimensionnées au pire cas
//     (portée maximale + fondu) dans prepare() (§4.2).
#include "Repitch.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_PITCH = 0, M_SPAN = 1, M_INERTIA = 2, M_STEREO = 9;

    constexpr float kPitchSemis   = 12.0f;         // ±1 octave : au-delà, la tête recale plus qu'elle ne lit
    constexpr double kSpanMinS    = 0.010;         // 10 ms de course : la bande bégaie
    constexpr double kSpanMaxS    = 0.500;         // 500 ms : elle plonge loin avant de revenir
    constexpr float kSpanSpan     = 3.912023005f;  // ln(500 / 10) = ln(50)
    constexpr double kInertiaMaxS = 0.500;         // poids du moteur de bande, course quadratique
    constexpr double kFadeS       = 0.004;         // fondu de recalage : fixe, il fixe la latence
    constexpr float kDetuneCents  = 25.0f;         // écart stéréo maximal, par canal
    constexpr int   kGuard        = 2;             // retard minimal : l'interpolation lit un point avant
    constexpr size_t kMaxChannels = 2;

    inline float rateFor (float v) noexcept
    {
        const float semis = (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0f * kPitchSemis;
        return std::exp2 (semis / 12.0f);          // v = 0,5 rend exactement 1,0 : la transparence en dépend
    }

    inline float spanFor (float v, double sr) noexcept
    {
        return (float) (kSpanMinS * sr) * std::exp (juce::jlimit (0.0f, 1.0f, v) * kSpanSpan);
    }

    // Lisibilité (J4b c-2) : les conversions de process(), rendues lisibles.
    juce::String dispPitch   (float v) { return display::sig ((double) (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0 * kPitchSemis); }
    juce::String dispSpan    (float v) { return display::sig (1000.0 * kSpanMinS * std::exp ((double) juce::jlimit (0.0f, 1.0f, v) * kSpanSpan)); }
    juce::String dispInertia (float v) { const double c = (double) juce::jlimit (0.0f, 1.0f, v); return display::sig (1000.0 * kInertiaMaxS * c * c); }
    juce::String dispStereo  (float v) { return display::sig ((double) (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 2.0 * kDetuneCents); }

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
    class Repitch : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.repitch", 1, "Repitch", MixLaw::Minus3, false,
                {
                    { M_PITCH,   "Hauteur",
                      "Vitesse de lecture de la bande, de -12 à +12 demi-tons (0,5 = vitesse d'origine) : la hauteur et la durée changent ensemble, comme une bande qu'on accélère. Verrouillé par défaut (§3.3.1) : la hauteur est le cas type du verrou, une valeur tirée à chaque pas emmène le traité hors de la tonalité de la source et fait recaler la tête sans arrêt. Le geste 3 se joue justement en le déverrouillant et en le faisant glisser — c'est pour ça que sa transition par défaut est le glissement, pas le saut.",
                      LockClass::LockedByDefault, true, "demi-tons", dispPitch },
                    { M_SPAN,    "Portée",
                      "Distance que la tête peut parcourir avant d'être recalée par un fondu de 4 ms : de 10 ms à 500 ms, course exponentielle (le milieu tombe vers 70 ms). Courte, la bande bégaie et le recalage devient une texture ; longue, elle plonge loin avant de revenir. Libre.",
                      LockClass::Free, true, "ms", dispSpan },
                    { M_INERTIA, "Inertie",
                      "Temps que met la vitesse de bande à rejoindre la valeur du pas : de 0 (instantané) à 500 ms, course quadratique. C'est le poids du moteur — c'est lui qui donne le glissement du geste 3 plutôt qu'une marche. Libre.",
                      LockClass::Free, true, "ms", dispInertia },
                    { M_STEREO,  "Écart stéréo",
                      "Désaccorde les deux canaux, jusqu'à 25 centièmes de demi-ton par canal (0,5 = aucun écart, gauche et droite rigoureusement identiques). Les deux têtes s'écartent lentement, puis recalent chacune de leur côté : l'image s'ouvre. Libre.",
                      LockClass::Free, true, "cents", dispStereo },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            fadeLen  = juce::jmax (8, (int) std::lround (kFadeS * sr));
            latency  = kGuard + fadeLen;
            maxDelay = latency + (int) std::lround (kSpanMaxS * sr) + fadeLen;

            int size = 8;
            while (size < maxDelay + kGuard + 4) size <<= 1;    // puissance de deux : l'indexation est un masque
            mask = size - 1;
            for (auto& line : lines) line.assign ((size_t) size, 0.0f);
            reset();
        }

        void reset() override
        {
            for (auto& line : lines) std::fill (line.begin(), line.end(), 0.0f);   // ne réalloue pas
            for (auto& s : chan) s = Head { (float) latency, (float) latency, 0 };
            writePos = -1;
            primed = false;
            lastPitch = lastSpan = lastInertia = lastStereo = -1.0f;
            rateTarget = rateSm = 1.0f;
            span = spanFor (0.5f, sr);
            inertiaCoef = 1.0f;
            detune = 1.0f;
        }

        int latencySamples() const override { return latency; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0) return;
            juce::ScopedNoDenormals noDenormals;

            const float* pi = p[M_PITCH];
            const float* si = p[M_SPAN];
            const float* ii = p[M_INERTIA];
            const float* ti = p[M_STEREO];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            const bool stereo = chans >= 2;
            std::array<float*, kMaxChannels> d {};
            for (int ch = 0; ch < chans; ++ch) d[(size_t) ch] = wet.getWritePointer (ch);
            const int size = mask + 1;
            const float lo = (float) kGuard, hi = (float) maxDelay;

            for (int i = 0; i < n; ++i)
            {
                // Conversions seulement quand une entrée bouge : un pas tenu ne paie ni
                // exp() ni exp2() (le cas ordinaire, annexe A.0).
                if (pi[i] != lastPitch)   { lastPitch   = pi[i]; rateTarget  = rateFor (pi[i]); }
                if (si[i] != lastSpan)    { lastSpan    = si[i]; span        = spanFor (si[i], sr); }
                if (ti[i] != lastStereo)  { lastStereo  = ti[i]; detune      = detuneFor (ti[i]); }
                if (ii[i] != lastInertia) { lastInertia = ii[i]; inertiaCoef = coefFor (ii[i]); }

                // La vitesse rejoint la valeur du pas avec l'inertie demandée. À la première
                // passe après un reset, elle s'y pose d'un coup : un moteur ne démarre pas
                // d'une vitesse qu'aucun pas n'a demandée.
                if (! primed) { rateSm = rateTarget; primed = true; }
                rateSm += (rateTarget - rateSm) * inertiaCoef;

                writePos = (writePos + 1) & mask;

                for (int ch = 0; ch < chans; ++ch)
                {
                    const float rate = ! stereo ? rateSm : (ch == 0 ? rateSm / detune : rateSm * detune);
                    const float drift = 1.0f - rate;     // retard gagné (rate < 1) ou perdu (rate > 1) par échantillon
                    auto& s = chan[(size_t) ch];

                    s.d += drift;
                    s.old += drift;
                    if (s.fade == 0)
                    {
                        if (s.d < (float) latency)          { s.old = s.d; s.d += span; s.fade = fadeLen; }
                        else if (s.d > (float) latency + span) { s.old = s.d; s.d -= span; s.fade = fadeLen; }
                        else                                  s.old = s.d;   // hors fondu, une seule position
                    }
                    s.d   = juce::jlimit (lo, hi, s.d);
                    s.old = juce::jlimit (lo, hi, s.old);

                    float a = 1.0f;
                    if (s.fade > 0)
                    {
                        // Fondu en cosinus surélevé : pente nulle aux deux bouts, donc le
                        // recalage ne laisse ni clic à l'entrée ni marche à la sortie.
                        a = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi
                                                    * (float) (fadeLen - s.fade) / (float) fadeLen);
                        --s.fade;
                    }

                    float* line = lines[(size_t) ch].data();
                    const float out = a * readCubic (line, size, mask, writePos, s.d)
                                    + (1.0f - a) * readCubic (line, size, mask, writePos, s.old);

                    line[writePos] = d[(size_t) ch][i];
                    d[(size_t) ch][i] = out;
                }
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        struct Head { float d = 0.0f, old = 0.0f; int fade = 0; };

        float coefFor (float v) const noexcept
        {
            const double c = juce::jlimit (0.0f, 1.0f, v);
            const double seconds = kInertiaMaxS * c * c;
            const double samples = seconds * sr;
            return samples > 1.0 ? (float) (1.0 - std::exp (-1.0 / samples)) : 1.0f;   // sous l'échantillon : d'un coup
        }

        std::array<std::vector<float>, kMaxChannels> lines {};
        std::array<Head, kMaxChannels> chan {};
        double sr = 48000.0;
        int fadeLen = 192, latency = 194, maxDelay = 24386, mask = 32767, writePos = -1;
        bool primed = false;
        float rateTarget = 1.0f, rateSm = 1.0f, span = 3360.0f, inertiaCoef = 1.0f, detune = 1.0f;
        float lastPitch = -1.0f, lastSpan = -1.0f, lastInertia = -1.0f, lastStereo = -1.0f;
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
        void all (float pitchV, float spanV, float inertiaV, float stereoV)
        {
            set (M_PITCH, pitchV); set (M_SPAN, spanV); set (M_INERTIA, inertiaV); set (M_STEREO, stereoV);
        }
        float* curve (int m) { return store[(size_t) m].data(); }

        void sine (double freqHz, double sampleRate, float amp, int from = 0, int to = -1)
        {
            const int end = to < 0 ? n : to;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = from; i < end; ++i)
                    buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate));
        }
        void ramp()
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, (float) i / (float) n);
        }
        void impulse (float amp) { for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.setSample (ch, 0, amp); }
    };

    // Valeurs d'entrée qui donnent l'unité demandée (inverses des lois ci-dessus).
    inline float vPitch   (double semis) noexcept { return (float) (0.5 + semis / (2.0 * kPitchSemis)); }
    inline float vSpan    (double ms)    noexcept { return (float) (std::log (0.001 * ms / kSpanMinS) / kSpanSpan); }
    inline float vInertia (double ms)    noexcept { return (float) std::sqrt (0.001 * ms / kInertiaMaxS); }

    inline double zeroCrossHz (const juce::AudioBuffer<float>& b, int ch, int from, int to, double sampleRate)
    {
        int c = 0;
        for (int i = from + 1; i < to; ++i)
            if (b.getSample (ch, i - 1) < 0.0f && b.getSample (ch, i) >= 0.0f) ++c;
        return (double) c * sampleRate / (double) (to - from);
    }

    bool Repitch::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.repitch : " << what << "\n";
            all = all && ok;
        };
        const float mid = 0.5f, span200 = vSpan (200.0), fast = 0.0f;

        // 1. Vitesse neutre — la tête ne bouge pas : la sortie est le sec retardé de la
        //    latence déclarée, à l'échantillon près, et rien d'autre ne sort.
        {
            prepare (kSr, 512); reset();
            const int L = latencySamples();
            Bench b (1, L + 512);
            b.impulse (1.0f);
            b.all (mid, span200, fast, mid);
            process (b.buf, b.p, b.n);
            double before = 0.0, after = 0.0;
            for (int i = 0; i < L; ++i)       before = juce::jmax (before, (double) std::abs (b.buf.getSample (0, i)));
            for (int i = L + 1; i < b.n; ++i) after  = juce::jmax (after,  (double) std::abs (b.buf.getSample (0, i)));
            const double at = b.buf.getSample (0, L);
            note (before < 1.0e-12 && after < 1.0e-9 && std::abs (at - 1.0) < 1.0e-6,
                  "vitesse neutre : impulsion rendue intacte à l'échantillon " + juce::String (L)
                      + " (valeur " + juce::String (at, 9) + "), rien avant (" + juce::String (before, 12)
                      + ") ni après (" + juce::String (after, 12) + ")");
        }

        // 2. Hauteur — la bande lue deux fois plus vite double la fréquence, deux fois plus
        //    lentement la divise. Les fenêtres de mesure sont prises entre deux recalages.
        {
            auto run = [&] (double semis, int from, int to)
            {
                prepare (kSr, 512); reset();
                Bench b (1, 24000);
                b.sine (1000.0, kSr, 0.5f);
                b.all (vPitch (semis), span200, fast, mid);
                process (b.buf, b.p, b.n);
                return zeroCrossHz (b.buf, 0, from, to, kSr);
            };
            const double up = run (12.0, 5200, 9500), down = run (-12.0, 1000, 19000);
            note (std::abs (up - 2000.0) < 60.0 && std::abs (down - 500.0) < 15.0,
                  "hauteur : 1000 Hz rendu à " + juce::String (up, 1) + " Hz à +12 demi-tons et "
                      + juce::String (down, 1) + " Hz à -12 (2000 et 500 attendus)");
        }

        // 3. Durée NON préservée — c'est la propriété qui sépare core.repitch de
        //    core.grain : une salve de 100 ms lue une octave plus bas dure 200 ms.
        {
            prepare (kSr, 512); reset();
            const int burst = 4800;
            Bench b (1, 24000);
            b.sine (500.0, kSr, 0.5f, 0, burst);
            b.all (vPitch (-12.0), span200, fast, mid);
            process (b.buf, b.p, b.n);
            int first = -1, last = -1;
            for (int i = 0; i < b.n; ++i)
                if (std::abs (b.buf.getSample (0, i)) > 0.01f) { if (first < 0) first = i; last = i; }
            const int len = last - first;
            note (first > 0 && len > (int) (1.7 * burst) && len < (int) (2.3 * burst),
                  "durée non préservée : salve de " + juce::String (burst) + " échantillons rendue en "
                      + juce::String (len) + " (le double attendu ; core.grain en rendrait "
                      + juce::String (burst) + ")");
        }

        // 4. Portée — sur une rampe, chaque recalage est un saut en avant nettement plus
        //    raide que la pente du signal : on les compte, et leur nombre suit la portée.
        {
            auto recalages = [&] (double ms)
            {
                prepare (kSr, 512); reset();
                Bench b (1, 90000);
                b.ramp();
                b.all (vPitch (-12.0), vSpan (ms), fast, mid);
                process (b.buf, b.p, b.n);
                int count = 0; bool inJump = false;
                for (int i = 1; i < b.n; ++i)
                {
                    const bool steep = (b.buf.getSample (0, i) - b.buf.getSample (0, i - 1)) > 5.0e-5f;
                    if (steep && ! inJump) ++count;
                    inJump = steep;
                }
                return count;
            };
            const int shortSpan = recalages (100.0), longSpan = recalages (500.0);
            const int expShort = (int) (45000.0 / (0.100 * kSr)), expLong = (int) (45000.0 / (0.500 * kSr));
            note (std::abs (shortSpan - expShort) <= 1 && std::abs (longSpan - expLong) <= 1,
                  "portée : " + juce::String (shortSpan) + " recalages à 100 ms (" + juce::String (expShort)
                      + " attendus) et " + juce::String (longSpan) + " à 500 ms (" + juce::String (expLong) + ")");
        }

        // 5. Inertie — le moteur a du poids : 30 ms après un pas d'une octave vers le bas,
        //    la bande est déjà arrivée à inertie nulle, à peine partie à inertie maximale.
        {
            auto after30ms = [&] (float inertiaV)
            {
                prepare (kSr, 512); reset();
                Bench warm (1, 4800);
                warm.sine (1000.0, kSr, 0.5f);
                warm.all (mid, span200, inertiaV, mid);
                process (warm.buf, warm.p, warm.n);                       // la bande tourne à vitesse normale

                Bench b (1, 2400);
                b.sine (1000.0, kSr, 0.5f, 0, 2400);
                b.all (vPitch (-12.0), span200, inertiaV, mid);
                process (b.buf, b.p, b.n);
                return zeroCrossHz (b.buf, 0, 200, 1640, kSr);
            };
            const double sharp = after30ms (0.0f), heavy = after30ms (vInertia (500.0));
            note (sharp < 620.0 && heavy > 900.0,
                  "inertie : 30 ms après le pas, " + juce::String (sharp, 1)
                      + " Hz sans inertie (500 visés, arrivée immédiate) et " + juce::String (heavy, 1)
                      + " Hz à 500 ms (le moteur est à peine parti)");
        }

        // 6. Écart stéréo — au centre les deux canaux sont identiques ; à fond leurs têtes
        //    ne tournent plus à la même vitesse et l'image s'ouvre.
        {
            auto spread = [&] (float stereoV)
            {
                prepare (kSr, 512); reset();
                Bench b (2, 48000);
                b.sine (1000.0, kSr, 0.5f);
                b.all (mid, span200, fast, stereoV);
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
            b.all (mid, mid, vInertia (5.0), mid);
            static const float steps[8] = { 0.5f, 0.95f, 0.1f, 0.72f, 0.28f, 1.0f, 0.0f, 0.62f };
            float* pc = b.curve (M_PITCH);
            for (int i = 0; i < b.n; ++i) pc[i] = steps[(i / 128) % 8];
            for (int off = 0; off < b.n; off += 128)
            {
                juce::AudioBuffer<float> sub (b.buf.getArrayOfWritePointers(), 1, off, 128);
                ParamCurves c;
                c.v[(size_t) M_PITCH]   = b.curve (M_PITCH)   + off;
                c.v[(size_t) M_SPAN]    = b.curve (M_SPAN)    + off;
                c.v[(size_t) M_INERTIA] = b.curve (M_INERTIA) + off;
                c.v[(size_t) M_STEREO]  = b.curve (M_STEREO)  + off;
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

void registerRepitch()
{
    SkillRegistry::instance().add (Repitch().info(), [] { return std::make_unique<Repitch>(); });
}
}
