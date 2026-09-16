#include "DummySkills.h"
#include <vector>

namespace plug::dummies
{
    namespace
    {
        constexpr int M_MAIN = 0, M_A = 1, M_B = 2;

        //==========================================================================
        // FACTICE — gain : main = gain linéaire 0..2 (0,5 = unité). paramA est déclaré
        // « verrouillé par défaut » sans effet sonore : il n'existe que pour éprouver
        // l'amendement J3-5 (le verrou déclaré devient le verrou initial).
        class GainDummy : public Skill
        {
        public:
            const SkillInfo& info() const override
            {
                static const SkillInfo i { kGainId, 1, "FACTICE gain", MixLaw::Minus6, true,
                    { { M_MAIN, "Gain", "FACTICE. Gain linéaire, 0,5 = unité.", LockClass::Free, true },
                      { M_A,    "Témoin verrou", "FACTICE. Verrouillé par défaut pour le test J3-5 ; sans effet.", LockClass::LockedByDefault, false } } };
                return i;
            }
            void prepare (double, int) override {}
            void reset() override {}
            int latencySamples() const override { return 0; }
            void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
            {
                const float* g = p[M_MAIN];
                for (int ch = 0; ch < wet.getNumChannels(); ++ch)
                {
                    auto* d = wet.getWritePointer (ch);
                    for (int i = 0; i < n; ++i) d[i] *= grid::gainLinear (g[i]);
                }
            }
            bool selfTest (juce::String& log) override
            {
                juce::AudioBuffer<float> b (1, 4); b.clear(); b.setSample (0, 0, 1.0f);
                std::vector<float> c (4, 0.25f); ParamCurves p; for (auto& v : p.v) v = c.data();
                process (b, p, 4);
                const bool ok = std::abs (b.getSample (0, 0) - 0.5f) < 1e-6f;
                log << (ok ? "  [OK] " : "  [FAIL] ") << "factice.gain : main 0,25 → gain 0,5\n";
                return ok;
            }
        };

        //==========================================================================
        // FACTICE — délai : paramA = temps 0..kMaxDummyDelay échantillons, paramB =
        // réinjection 0..0,9. Sortie = signal retardé (le sec passe par le mélange
        // de l'emplacement). Latence nulle. Temps lu à l'échantillon, sans interpolation.
        class DelayDummy : public Skill
        {
        public:
            const SkillInfo& info() const override
            {
                static const SkillInfo i { kDelayId, 1, "FACTICE délai", MixLaw::Minus3, true,
                    { { M_A, "Temps", "FACTICE. 0..100 ms.", LockClass::Free, false },
                      { M_B, "Réinjection", "FACTICE. 0..0,9.", LockClass::Free, true } } };
                return i;
            }
            void prepare (double, int) override
            {
                for (auto& r : ring) r.assign ((size_t) kMaxDummyDelay + 1, 0.0f);   // allocation ici seulement
                pos = 0;
            }
            void reset() override { for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f); pos = 0; }
            int latencySamples() const override { return 0; }
            void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
            {
                const float* tm = p[M_A]; const float* fb = p[M_B];
                const int len = kMaxDummyDelay + 1;
                const int chans = juce::jmin (2, wet.getNumChannels());
                int pp = pos;
                for (int i = 0; i < n; ++i)
                {
                    const int d = juce::jlimit (1, kMaxDummyDelay, (int) std::lround (tm[i] * kMaxDummyDelay));
                    const float f = 0.9f * juce::jlimit (0.0f, 1.0f, fb[i]);
                    int rp = pp - d; if (rp < 0) rp += len;
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        auto* data = wet.getWritePointer (ch);
                        const float delayed = ring[(size_t) ch][(size_t) rp];
                        ring[(size_t) ch][(size_t) pp] = data[i] + f * delayed;
                        data[i] = delayed;
                    }
                    if (++pp == len) pp = 0;
                }
                pos = pp;
            }
            bool selfTest (juce::String& log) override
            {
                prepare (48000.0, 64);
                juce::AudioBuffer<float> b (1, 64); b.clear(); b.setSample (0, 0, 1.0f);
                std::vector<float> t (64, 10.0f / kMaxDummyDelay), z (64, 0.0f);
                ParamCurves p; for (auto& v : p.v) v = z.data(); p.v[M_A] = t.data();
                process (b, p, 64);
                int at = -1; for (int i = 0; i < 64; ++i) if (b.getSample (0, i) > 0.5f) { at = i; break; }
                const bool ok = at == 10;
                log << (ok ? "  [OK] " : "  [FAIL] ") << "factice.delay : impulsion retardée de 10 échantillons (" << at << ")\n";
                return ok;
            }
        private:
            std::array<std::vector<float>, 2> ring;
            int pos = 0;
        };

        //==========================================================================
        // FACTICE — latent : passe-tout retardé de kLatentLatency, latence déclarée
        // égale. Sert à prouver l'alignement du sec, la constance de la latence pendant
        // les pas (§3.3.2) et le bypass aligné (§4.3). main = gain.
        class LatentDummy : public Skill
        {
        public:
            const SkillInfo& info() const override
            {
                static const SkillInfo i { kLatentId, 1, "FACTICE latent", MixLaw::Minus6, true,
                    { { M_MAIN, "Gain", "FACTICE. Gain linéaire, 0,5 = unité.", LockClass::Free, false } } };
                return i;
            }
            void prepare (double, int) override { for (auto& r : ring) r.assign ((size_t) kLatentLatency, 0.0f); pos = 0; }
            void reset() override { for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f); pos = 0; }
            int latencySamples() const override { return kLatentLatency; }
            void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int n) override
            {
                const float* g = p[M_MAIN];
                const int chans = juce::jmin (2, wet.getNumChannels());
                int pp = pos;
                for (int i = 0; i < n; ++i)
                {
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        auto* data = wet.getWritePointer (ch);
                        const float delayed = ring[(size_t) ch][(size_t) pp];
                        ring[(size_t) ch][(size_t) pp] = data[i];
                        data[i] = delayed * grid::gainLinear (g[i]);
                    }
                    if (++pp == kLatentLatency) pp = 0;
                }
                pos = pp;
            }
            bool selfTest (juce::String& log) override
            {
                prepare (48000.0, 512);
                juce::AudioBuffer<float> b (1, 512); b.clear(); b.setSample (0, 0, 1.0f);
                std::vector<float> h (512, 0.5f); ParamCurves p; for (auto& v : p.v) v = h.data();
                process (b, p, 512);
                int at = -1; for (int i = 0; i < 512; ++i) if (b.getSample (0, i) > 0.5f) { at = i; break; }
                const bool ok = at == kLatentLatency;
                log << (ok ? "  [OK] " : "  [FAIL] ") << "factice.latent : impulsion retardée de " << at << " = latence déclarée " << kLatentLatency << "\n";
                return ok;
            }
        private:
            std::array<std::vector<float>, 2> ring;
            int pos = 0;
        };

        bool registered = false;
    }

    void registerAll()
    {
        if (registered) return;
        registered = true;
        auto& r = SkillRegistry::instance();
        r.add (GainDummy().info(),   [] { return std::make_unique<GainDummy>(); });
        r.add (DelayDummy().info(),  [] { return std::make_unique<DelayDummy>(); });
        r.add (LatentDummy().info(), [] { return std::make_unique<LatentDummy>(); });
    }

    bool selfTestAll (juce::String& log)
    {
        registerAll();
        bool ok = true;
        for (const auto& id : SkillRegistry::instance().ids())
            if (auto s = SkillRegistry::instance().create (id))
                ok &= s->selfTest (log);
        return ok;
    }
}
