// Drive — distorsion par mise en forme d'onde, anti-repliement par primitive
// (CdC §3.8, ligne « Distorsion », latence « nulle ou faible » ; contrat §3.9).
// Invariants et leur raison :
//   · latence déclarée ZÉRO, et c'est un choix assumé — l'anti-repliement se fait par
//     intégration de la non-linéarité (ADAA d'ordre 1 : on rend la moyenne de la forme
//     sur le segment qui joint deux échantillons, pas sa valeur ponctuelle) et non par
//     suréchantillonnage. Un suréchantillonneur à phase linéaire aurait une latence à
//     déclarer ET une pré-oscillation avant son pic, que la mesure sur impulsion du
//     vérificateur lit comme une latence plus courte que la latence déclarée (§4.3) ;
//   · aucun paramètre structurel — il n'y a pas de taux de suréchantillonnage à exposer,
//     donc rien qui puisse changer la latence entre deux passes (§3.3.1). Le jour où le
//     socle transmettra master.quality (§3.10), c'est l'ordre de l'intégration qui s'y
//     brancherait, jamais la latence, qui reste nulle dans tous les cas ;
//   · prix de ce choix, dit ici parce qu'il s'entend : l'intégration d'ordre 1 vaut un
//     moyennage sur un échantillon, soit un retard de groupe d'un DEMI-échantillon et
//     -1,25 dB à 8 kHz (48 kHz). La latence déclarée est entière : zéro est la seule
//     valeur honnête, et le demi-échantillon reste sous la tolérance du vérificateur ;
//   · les trois formes se fondent linéairement — la primitive d'un mélange linéaire est
//     le mélange linéaire des primitives : le fondu reste exact pour l'anti-repliement,
//     ce qui permet de laisser la forme LIBRE au lieu d'en faire des paliers verrouillés ;
//   · la composante continue du biais est retirée en sortie (y = f(u) - f(biais)) : sans
//     cela, l'asymétrie enverrait un continu dans la suite de la chaîne, et le silence
//     en entrée ne rendrait plus le silence ;
//   · pas de gain de compensation : l'entrée « gain » de l'emplacement appartient au
//     moteur (§3.2), c'est elle qui rattrape le niveau, pas la skill ;
//   · loi -6 dB — une mise en forme d'onde rend un signal en phase avec le sec (§3.7) ;
//   · deux canaux au plus — le socle n'en présente jamais davantage (Engine.cpp, chans) ;
//   · rien n'est alloué : tout l'état tient dans l'objet, prepare() ne fait que le purger (§4.2).
#include "Drive.h"
#include "../../Skill.h"
#include <array>
#include <cmath>
#include <vector>

namespace plug::skills
{
namespace
{
    // Entrées de la grille générique occupées (indices de grid::kModulable).
    constexpr int M_DRIVE = 0, M_SHAPE = 1, M_BIAS = 2, M_SMOOTH = 3;

    constexpr double kDriveSpan = 4.144653167;   // 36 dB en népers d'amplitude : ln(10^(36/20))
    constexpr double kBiasMax   = 1.0;           // biais ±1, du symétrique au franchement décalé
    constexpr double kLpMin     = 1000.0;        // passe-bas de sortie : 1 kHz → 20 kHz
    constexpr double kLpSpan    = 2.995732274;   // ln(20)
    constexpr double kHalfPi    = 1.570796326794897;
    constexpr double kLog2      = 0.693147180559945;
    constexpr double kEps       = 1.0e-5;        // sous cet écart, la division perdrait ses chiffres

    constexpr size_t kMaxChannels = 2;

    // Trois formes, toutes impaires et bornées à 1 : f(0) = 0, donc le silence reste
    // le silence quel que soit le fondu (épreuve 3 du vérificateur).
    inline double shape (int k, double x) noexcept
    {
        switch (k)
        {
            case 0:  return std::tanh (x);                          // doux : compression progressive
            case 1:  return juce::jlimit (-1.0, 1.0, x);             // écrêtage : palier franc
            default: return std::sin (kHalfPi * x);                  // repli : au-delà de 1, l'onde revient
        }
    }

    // Primitives exactes des trois formes. Pour le doux, log(cosh x) est réécrit
    // |x| + log1p(e^-2|x|) - log 2 : la forme directe déborderait dès |x| ≈ 710.
    inline double primitive (int k, double x) noexcept
    {
        const double a = std::abs (x);
        switch (k)
        {
            case 0:  return a + std::log1p (std::exp (-2.0 * a)) - kLog2;
            case 1:  return a <= 1.0 ? 0.5 * x * x : a - 0.5;
            default: return -std::cos (kHalfPi * x) / kHalfPi;
        }
    }

    // Fondu doux → écrêtage → repli, en deux moitiés de course.
    inline void blendFor (float v, int& a, int& b, double& t) noexcept
    {
        const double c = (double) juce::jlimit (0.0f, 1.0f, v);
        if (c <= 0.5) { a = 0; b = 1; t = 2.0 * c; }
        else          { a = 1; b = 2; t = 2.0 * (c - 0.5); }
    }

    inline double driveGain (float v) noexcept { return std::exp ((double) juce::jlimit (0.0f, 1.0f, v) * kDriveSpan); }
    inline double biasFor   (float v) noexcept { return ((double) juce::jlimit (0.0f, 1.0f, v) - 0.5) * 2.0 * kBiasMax; }
    inline double smoothHz  (float v) noexcept { return kLpMin * std::exp ((double) juce::jlimit (0.0f, 1.0f, v) * kLpSpan); }

    // Lisibilité (J4b c-2) : les conversions de process(), rendues lisibles. La forme
    // est un fondu continu entre trois non-linéarités : on nomme la plus proche.
    juce::String dispDrive  (float v) { return display::sig (20.0 * std::log10 (driveGain (v))); }
    juce::String dispShape  (float v)
    {
        const double c = (double) juce::jlimit (0.0f, 1.0f, v);
        if (c <= 0.25) return display::text ("Doux");
        if (c <= 0.75) return display::text ("Écrêtage");   // accents : voir display::text
        return display::text ("Repli");
    }
    juce::String dispBias   (float v) { return display::sig (100.0 * biasFor (v) / kBiasMax); }
    juce::String dispSmooth (float v) { return display::sig (smoothHz (v)); }

    //==========================================================================
    class Drive : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "core.drive", 1, "Distorsion", MixLaw::Minus6, false,
                {
                    { M_DRIVE,  "Drive",
                      "Gain d'attaque de la forme d'onde : 0 dB à +36 dB, course exponentielle (le milieu vaut +18 dB). Le niveau de sortie ne se rattrape pas ici mais avec l'entrée « gain » de l'emplacement. Libre : c'est le terrain naturel de la variation par pas.",
                      LockClass::Free, true, "dB", dispDrive },
                    { M_SHAPE,  "Forme",
                      "Fondu continu entre trois formes : 0 = doux (tanh, compression progressive), 0,5 = écrêtage franc, 1 = repli (l'onde revient au lieu de plafonner, ce qui engendre beaucoup de partiels). Libre : le fondu est continu, il n'y a donc pas de marche à craindre entre deux pas.",
                      LockClass::Free, true, "", dispShape },
                    { M_BIAS,   "Asymétrie",
                      "Décale le signal avant la forme : 0,5 = symétrique (harmoniques impaires seules), 0 et 1 = ±1 de décalage, ce qui fait apparaître les harmoniques paires. La composante continue engendrée est retirée en sortie, donc le silence reste le silence. Libre.",
                      LockClass::Free, true, "%", dispBias },
                    { M_SMOOTH, "Lissage",
                      "Passe-bas d'un pôle après la forme : 1 kHz à 20 kHz, course exponentielle. À 1 il n'adoucit presque rien ; baissé, il retire la friture des harmoniques les plus hautes. Libre.",
                      LockClass::Free, true, "Hz", dispSmooth },
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
            lastDrive = lastShape = lastBias = lastSmooth = -1.0f;
            gain = 1.0; bias = 0.0; blendT = 0.0; fBias = 0.0;
            shapeA = 0; shapeB = 1;
            lpCoef = 1.0f;
            cacheValid = false;
        }

        int latencySamples() const override { return 0; }

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
        {
            if (n <= 0) return;
            juce::ScopedNoDenormals noDenormals;

            const float* dr = p[M_DRIVE];
            const float* sh = p[M_SHAPE];
            const float* bi = p[M_BIAS];
            const float* sm = p[M_SMOOTH];

            const int chans = juce::jmin ((int) kMaxChannels, wet.getNumChannels());
            std::array<float*, kMaxChannels> d {};
            for (int ch = 0; ch < chans; ++ch) d[(size_t) ch] = wet.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
            {
                // Conversions seulement quand une entrée bouge : un pas tenu ne paie ni
                // exp() ni le recalcul de la primitive antérieure (le cas ordinaire, annexe A.0).
                if (dr[i] != lastDrive)  { lastDrive = dr[i];  gain = driveGain (dr[i]); cacheValid = false; }
                if (bi[i] != lastBias)   { lastBias  = bi[i];  bias = biasFor (bi[i]);   cacheValid = false; }
                if (sh[i] != lastShape)  { lastShape = sh[i];  blendFor (sh[i], shapeA, shapeB, blendT); cacheValid = false; }
                if (sm[i] != lastSmooth)
                {
                    lastSmooth = sm[i];
                    lpCoef = (float) juce::jlimit (0.0, 1.0, 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * smoothHz (sm[i]) / sr));
                }

                if (! cacheValid)
                {
                    // Les deux bouts du segment sont évalués avec les MÊMES réglages : la skill
                    // rend toujours la moyenne d'une non-linéarité fixe, jamais un mélange de deux.
                    fBias = blendShape (bias);
                    for (size_t ch = 0; ch < kMaxChannels; ++ch)   // tous les canaux : le socle peut en présenter un de plus au bloc suivant
                    {
                        auto& s = chan[ch];
                        s.uPrev = gain * (double) s.xPrev + bias;
                        s.fPrev = blendPrimitive (s.uPrev);
                    }
                    cacheValid = true;
                }

                for (int ch = 0; ch < chans; ++ch)
                {
                    auto& s = chan[(size_t) ch];
                    const double x = (double) d[(size_t) ch][i];
                    const double u = gain * x + bias;
                    const double du = u - s.uPrev;
                    const double fu = blendPrimitive (u);

                    const double y = (std::abs (du) > kEps) ? (fu - s.fPrev) / du
                                                            : blendShape (0.5 * (u + s.uPrev));

                    s.uPrev = u; s.fPrev = fu; s.xPrev = (float) x;
                    s.z += lpCoef * ((float) (y - fBias) - s.z);
                    d[(size_t) ch][i] = s.z;
                }
            }
        }

        bool selfTest (juce::String& log) override;

    private:
        // Aux deux bouts du fondu, une seule forme est évaluée : le cas ordinaire est
        // un réglage posé sur « doux », « écrêtage » ou « repli », pas entre deux.
        double blendShape (double x) const noexcept
        {
            if (blendT <= 0.0) return shape (shapeA, x);
            if (blendT >= 1.0) return shape (shapeB, x);
            return (1.0 - blendT) * shape (shapeA, x) + blendT * shape (shapeB, x);
        }
        double blendPrimitive (double x) const noexcept
        {
            if (blendT <= 0.0) return primitive (shapeA, x);
            if (blendT >= 1.0) return primitive (shapeB, x);
            return (1.0 - blendT) * primitive (shapeA, x) + blendT * primitive (shapeB, x);
        }

        struct State
        {
            float xPrev = 0.0f;      // entrée brute : le segment se rebâtit avec les réglages courants
            double uPrev = 0.0, fPrev = 0.0;
            float z = 0.0f;          // mémoire du passe-bas de sortie
        };

        std::array<State, kMaxChannels> chan {};
        double sr = 48000.0, gain = 1.0, bias = 0.0, blendT = 0.0, fBias = 0.0;
        float lastDrive = -1.0f, lastShape = -1.0f, lastBias = -1.0f, lastSmooth = -1.0f, lpCoef = 1.0f;
        int shapeA = 0, shapeB = 1;
        bool cacheValid = false;
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
        void all (float drive, float shapeV, float biasV, float smooth)
        {
            set (M_DRIVE, drive); set (M_SHAPE, shapeV); set (M_BIAS, biasV); set (M_SMOOTH, smooth);
        }
        void sine (double freqHz, double sampleRate, float amp)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate));
        }
        void dc (float v)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, v);
        }
    };

    // Amplitude d'une raie, fenêtre de Hann (la fuite entre harmoniques est négligeable).
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
    inline float vDrive  (double dB) noexcept { return (float) (dB * 0.11512925464970229 / kDriveSpan); }
    inline float vSmooth (double hz) noexcept { return (float) (std::log (hz / kLpMin) / kLpSpan); }

    bool Drive::selfTest (juce::String& log)
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 8192;
        bool all = true;
        auto note = [&] (bool ok, const juce::String& what)
        {
            log << (ok ? "  [OK] " : "  [FAIL] ") << "core.drive : " << what << "\n";
            all = all && ok;
        };

        // Sortie sur un continu établi : l'intégration retombe sur la forme elle-même,
        // ce qui donne des cas exacts pour chacune des trois formes.
        auto atDc = [&] (float in, float drive, float shapeV, float biasV)
        {
            Bench b (1, 4800);
            b.dc (in);
            b.all (drive, shapeV, biasV, 1.0f);
            prepare (kSr, 4800); reset();
            process (b.buf, b.p, 4800);
            return (double) b.buf.getSample (0, 4799);
        };
        // Amplitude d'un harmonique sur un sinus à 1 kHz.
        auto harmonic = [&] (float amp, float drive, float shapeV, float biasV, float smooth, int k)
        {
            Bench b (1, n);
            b.sine (1000.0, kSr, amp);
            b.all (drive, shapeV, biasV, smooth);
            prepare (kSr, n); reset();
            process (b.buf, b.p, n);
            return magAt (b.buf, 0, n, 1000.0 * k, kSr);
        };

        const float sym = 0.5f, open = 1.0f;
        const float fSoft = 0.0f, fClip = 0.5f, fFold = 1.0f;

        // 1. Drive — la troisième harmonique d'un sinus monte quand on pousse.
        {
            const double lowH1 = harmonic (0.3f, 0.0f, fSoft, sym, open, 1);
            const double lowH3 = harmonic (0.3f, 0.0f, fSoft, sym, open, 3);
            const double hiH1  = harmonic (0.3f, 1.0f, fSoft, sym, open, 1);
            const double hiH3  = harmonic (0.3f, 1.0f, fSoft, sym, open, 3);
            const double lo = 20.0 * std::log10 (juce::jmax (1.0e-12, lowH3 / lowH1));
            const double hi = 20.0 * std::log10 (juce::jmax (1.0e-12, hiH3 / hiH1));
            note (hi - lo >= 20.0, "harmonique 3 : " + juce::String (lo, 1) + " dB à 0 dB de drive, "
                                       + juce::String (hi, 1) + " dB à +36 dB (écart ≥ 20 attendu)");
        }
        // 2. Forme — doux : un continu à 0,5 poussé de 6,02 dB (gain 2) rend tanh(1).
        {
            const double out = atDc (0.5f, vDrive (6.020599913), fSoft, sym);
            note (std::abs (out - 0.7615941560) < 0.002, "doux : continu 0,5 × 2 → " + juce::String (out, 6) + " (tanh 1 = 0,761594)");
        }
        // 3. Forme — écrêtage : le même continu poussé de 12,04 dB (gain 4) plafonne à 1,
        //    et un sinus poussé à fond devient un carré (fondamentale 4/π, harmonique 3 au tiers).
        {
            const double out = atDc (0.5f, vDrive (12.04119983), fClip, sym);
            const double h1 = harmonic (0.5f, 1.0f, fClip, sym, open, 1);
            const double h3 = harmonic (0.5f, 1.0f, fClip, sym, open, 3);
            const double r  = h3 / juce::jmax (1.0e-12, h1);
            note (std::abs (out - 1.0) < 0.002 && std::abs (h1 - 1.2732395447) < 0.02 && std::abs (r - 1.0 / 3.0) < 0.02,
                  "écrêtage : continu → " + juce::String (out, 6) + " (1,000000) ; carré → fondamentale "
                      + juce::String (h1, 5) + " (1,27324) et rapport h3/h1 " + juce::String (r, 5) + " (0,33333)");
        }
        // 4. Forme — repli : gain 4, un continu à 0,25 atteint la crête (sin π/2 = 1) et
        //    un continu à 0,5 est replié jusqu'au zéro (sin π = 0).
        {
            const double crest = atDc (0.25f, vDrive (12.04119983), fFold, sym);
            const double back  = atDc (0.5f,  vDrive (12.04119983), fFold, sym);
            note (std::abs (crest - 1.0) < 0.002 && std::abs (back) < 0.001,
                  "repli : crête " + juce::String (crest, 6) + " (1,000000) et repli complet " + juce::String (back, 6) + " (0,000000)");
        }
        // 5. Asymétrie — symétrique, aucune harmonique paire ; décalée, elles apparaissent ;
        //    et dans les deux cas le silence reste le silence (la continue du biais est retirée).
        {
            const double h1s = harmonic (0.5f, vDrive (12.04119983), fSoft, sym,  open, 1);
            const double h2s = harmonic (0.5f, vDrive (12.04119983), fSoft, sym,  open, 2);
            const double h1a = harmonic (0.5f, vDrive (12.04119983), fSoft, 1.0f, open, 1);
            const double h2a = harmonic (0.5f, vDrive (12.04119983), fSoft, 1.0f, open, 2);
            Bench q (2, 2048);
            q.all (vDrive (12.04119983), fSoft, 1.0f, open);
            prepare (kSr, 2048); reset();
            process (q.buf, q.p, 2048);
            const double quiet = (double) q.buf.getMagnitude (0, 2048);
            const double rs = h2s / juce::jmax (1.0e-12, h1s), ra = h2a / juce::jmax (1.0e-12, h1a);
            note (rs < 1.0e-4 && ra > 0.05 && quiet < 1.0e-6,
                  "harmonique 2 relative : " + juce::String (rs, 9) + " à 0,5 et " + juce::String (ra, 5)
                      + " à 1 ; silence en entrée → crête " + juce::String (quiet, 9));
        }
        // 6. Lissage — un sinus à 8 kHz, presque sans drive : trois octaves au-dessus de
        //    la coupure la plus basse, le pôle doit mordre.
        {
            Bench a (1, n), b (1, n);
            a.sine (8000.0, kSr, 0.05f); a.all (0.0f, fSoft, sym, open);
            b.sine (8000.0, kSr, 0.05f); b.all (0.0f, fSoft, sym, vSmooth (1000.0));
            prepare (kSr, n); reset(); process (a.buf, a.p, n);
            prepare (kSr, n); reset(); process (b.buf, b.p, n);
            const double opened = magAt (a.buf, 0, n, 8000.0, kSr), closed = magAt (b.buf, 0, n, 8000.0, kSr);
            const double drop = 20.0 * std::log10 (juce::jmax (1.0e-12, opened / juce::jmax (1.0e-12, closed)));
            note (drop >= 12.0, "8 kHz : " + juce::String (drop, 1) + " dB entre lissage 1 et lissage 0 (≥ 12 attendus)");
        }
        // 7. Bornage — drive à fond sur du pleine échelle : les trois formes restent dans ±1.
        {
            bool ok = true;
            double worst = 0.0;
            for (float f : { fSoft, fClip, fFold })
            {
                Bench b (2, n);
                b.sine (500.0, kSr, 1.0f);
                b.all (1.0f, f, sym, open);
                prepare (kSr, n); reset();
                process (b.buf, b.p, n);
                const double peak = (double) b.buf.getMagnitude (0, n);
                worst = juce::jmax (worst, peak);
                ok = ok && peak <= 1.001 && std::isfinite (peak);
            }
            note (ok, "drive +36 dB sur pleine échelle : crête maximale des trois formes " + juce::String (worst, 6) + " (≤ 1,001)");
        }
        return all;
    }
}

void registerDrive()
{
    SkillRegistry::instance().add (Drive().info(), [] { return std::make_unique<Drive>(); });
}
}
