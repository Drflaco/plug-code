#include "Engine.h"
#include "GridMap.h"
#include "Modulation.h"
#include "Skill.h"
#include "StateSchema.h"
#include "StepValue.h"
#include "dummies/DummySkills.h"
#include <atomic>
#include <array>
#include <cmath>

namespace plug
{
    namespace
    {
        using state::kSlots;   // mêmes entités que state:: : aucune ambiguïté sous using namespace state
        using state::kSteps;
        constexpr int kM = grid::kModulableCount;
        constexpr int kMaxModRoutes = 16;
        constexpr int kMaxMacroRoutes = 32;
        constexpr int M_MIX = 7, M_GAIN = 8;

        //==========================================================================
        // Le modèle : l'état lu une fois, résolu (cibles de pas calculées par la
        // fonction pure), en tableaux fixes. Construit hors audio, lu par l'audio.
        struct SlotModel
        {
            bool present = false;            // une skill connue occupe l'emplacement
            bool tailRing = true;
            std::array<ParamSpec, kM> spec {};
            struct Step { bool on = true; std::array<bool, kM> has {}; std::array<float, kM> target {}; };
            std::array<Step, kSteps> steps {};
            EnvSpec env1;
            Source2Spec src2;
            struct ModRoute { int src = 0; int dst = 0; float depth = 0.0f; };
            std::array<ModRoute, kMaxModRoutes> routes {};
            int routeCount = 0;
            MixLaw law = MixLaw::Minus3;
        };

        struct MacroRoute { int slot0 = 0, dst = 0; float lo = 0, hi = 0, curve = 0; };

        struct Model
        {
            std::array<SlotModel, kSlots> slots {};
            std::array<std::array<MacroRoute, kMaxMacroRoutes>, grid::kMacroCount> macroRoutes {};
            std::array<int, grid::kMacroCount> macroRouteCount {};
            int latency = 0;
        };

        void readEnv (const juce::ValueTree& t, EnvSpec& e)
        {
            using namespace state;
            e.count = 0;
            for (const auto& c : t)
                if (c.hasType (id::Point) && e.count < kMaxEnvPoints)
                    e.pts[(size_t) e.count++] = { (float) (double) c.getProperty (id::t), (float) (double) c.getProperty (id::v) };
            e.rate = (float) (double) t.getProperty (id::rate, 0.5);
            e.sync = (bool) t.getProperty (id::sync, true);
            e.loop = (bool) t.getProperty (id::loop, true);
            const auto trig = t.getProperty (id::trigger, "transport").toString();
            e.trigger = trig == "audio" ? 1 : trig == "midi" ? 2 : 0;
        }

        float macroCurve (float m, float curve) noexcept
        {
            return std::pow (juce::jlimit (0.0f, 1.0f, m), std::pow (2.0f, 2.0f * juce::jlimit (-1.0f, 1.0f, curve)));
        }

        //==========================================================================
        // Ligne à retard du sec d'un emplacement ou du master : aligne le sec sur la
        // latence déclarée du traité (§4.3). Préallouée à kMaxLatency.
        struct DryDelay
        {
            std::array<std::vector<float>, 2> ring;
            int pos = 0, length = 0;

            void prepare() { for (auto& r : ring) r.assign ((size_t) Engine::kMaxLatency, 0.0f); pos = 0; }
            void clear() noexcept { for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f); pos = 0; }
            void setLength (int n) noexcept { length = juce::jlimit (0, Engine::kMaxLatency, n); if (length == 0) pos = 0; }

            // Écrit in dans l'anneau, dépose la version retardée dans out (in et out peuvent être distincts).
            void process (const float* const* in, float* const* out, int chans, int n) noexcept
            {
                if (length == 0)
                {
                    for (int ch = 0; ch < chans; ++ch) if (out[ch] != in[ch]) std::copy (in[ch], in[ch] + n, out[ch]);
                    return;
                }
                int p = pos;
                for (int i = 0; i < n; ++i)
                {
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        auto& r = ring[(size_t) ch];
                        const float d = r[(size_t) p];
                        r[(size_t) p] = in[ch][i];
                        out[ch][i] = d;
                    }
                    if (++p == length) p = 0;
                }
                pos = p;
            }
        };

        //==========================================================================
        // État audio d'un emplacement : la skill, ses rampes, son activation.
        struct SlotRuntime
        {
            std::unique_ptr<Skill> skill;
            std::atomic<Skill*> pending { nullptr };    // posé hors audio, pris au bloc suivant
            std::atomic<Skill*> retired { nullptr };    // rendu par l'audio, détruit hors audio
            juce::String skillId;                        // message thread seulement
            int latency = 0;

            DryDelay dry;
            std::array<juce::LinearSmoothedValue<float>, kM> base {};
            std::array<float, kM> current {};            // vpas au dernier échantillon (J3-1)
            std::array<float, kM> rampStart {};
            std::array<float, kM> target {};
            std::array<bool, kM> hasTarget {};
            std::array<int, kM> rampPos {};
            int rampLen = 0;
            bool stepOn = true;
            float activation = 1.0f;

            EnvRunner env1, env2;
            Follower follower;
        };
    }

    //==========================================================================
    struct Engine::Impl
    {
        double sr = 48000.0;
        int maxBlock = 512;
        const ParamSource* params = nullptr;

        std::array<Model, 3> models;
        std::atomic<int> published { 0 };
        std::atomic<int> inUse { -1 };

        Clock clock;
        std::array<SlotRuntime, kSlots> slots;
        DryDelay masterDry;

        // Tampons préalloués (§4.2)
        juce::AudioBuffer<float> inCopy, dryBuf, wetBuf, chainDry;
        std::array<std::array<std::vector<float>, kM>, kSlots> values;   // courbes composées
        std::vector<float> envBuf1, envBuf2, folBuf, actBuf;
        struct Seg { int offset, len, step; bool started; };
        std::array<Seg, 64> segs {};
        int segCount = 0;

        Impl() { dummies::registerAll(); }

        ~Impl()
        {
            for (auto& s : slots)
            {
                delete s.pending.exchange (nullptr);
                delete s.retired.exchange (nullptr);
            }
        }

        //======================================================================
        void prepare (double sampleRate, int maxBlockSize)
        {
            sr = sampleRate; maxBlock = juce::jmax (1, maxBlockSize);
            clock.prepare (sr);
            inCopy.setSize (2, maxBlock); dryBuf.setSize (2, maxBlock); wetBuf.setSize (2, maxBlock); chainDry.setSize (2, maxBlock);
            envBuf1.assign ((size_t) maxBlock, 0.0f); envBuf2.assign ((size_t) maxBlock, 0.0f);
            folBuf.assign ((size_t) maxBlock, 0.0f); actBuf.assign ((size_t) maxBlock, 0.0f);
            for (auto& s : values) for (auto& v : s) v.assign ((size_t) maxBlock, 0.0f);
            masterDry.prepare();
            for (auto& s : slots)
            {
                s.dry.prepare();
                for (auto& b : s.base) b.reset (sr, 0.005);   // 5 ms : lissage de la base, indépendant du bloc
                s.env1.prepare (sr); s.env2.prepare (sr); s.follower.prepare (sr);
                if (s.skill) s.skill->prepare (sr, maxBlock);
            }
            reset();
        }

        void reset()
        {
            clock.reset();
            masterDry.clear();
            for (auto& s : slots)
            {
                s.dry.clear();
                s.current.fill (0.0f); s.rampStart.fill (0.0f); s.target.fill (0.0f); s.hasTarget.fill (false); s.rampPos.fill (0);
                s.rampLen = 0; s.stepOn = true; s.activation = 1.0f;
                s.env1.reset(); s.env2.reset(); s.follower.reset();
                if (s.skill) s.skill->reset();
            }
            // Les bases partent de leur valeur courante, pas de zéro : aucune rampe parasite au premier bloc.
            for (int i = 0; i < kSlots; ++i)
                for (int mIdx = 0; mIdx < kM; ++mIdx)
                    slots[(size_t) i].base[(size_t) mIdx].setCurrentAndTargetValue (param (grid::slotIndex (i, grid::kModulable[(size_t) mIdx])));
        }

        //======================================================================
        // Construction du modèle (message thread) et publication bornée.
        void setState (const juce::ValueTree& st)
        {
            using namespace state;
            juce::ValueTree s = st;
            if (s.getChildWithName (id::Slots).getNumChildren() != kSlots)
                ensureSchema (s);

            const int p = published.load(), u = inUse.load();
            int idx = 0;
            while (idx == p || idx == u) ++idx;   // trois tampons : il en reste toujours un libre
            Model& m = models[(size_t) idx];
            m = Model {};

            int totalLatency = 0;
            for (int i = 0; i < kSlots; ++i)
            {
                auto slotTree = slot (s, i + 1);
                auto& sm = m.slots[(size_t) i];
                auto& rt = slots[(size_t) i];
                const auto skillId = slotTree.getProperty (id::skill).toString();
                const auto* info = SkillRegistry::instance().info (skillId);
                sm.present = info != nullptr;
                sm.tailRing = slotTree.getProperty (id::tail, "ring").toString() != "cut";
                sm.law = info ? info->mixLaw : MixLaw::Minus6;

                // Changement de skill : instance préparée ici, échangée au prochain bloc (§4.2).
                if (skillId != rt.skillId)
                {
                    delete rt.retired.exchange (nullptr);
                    auto inst = SkillRegistry::instance().create (skillId);
                    if (inst) inst->prepare (sr, maxBlock);
                    rt.latency = inst ? inst->latencySamples() : 0;
                    delete rt.pending.exchange (inst.release());
                    rt.skillId = skillId;
                }
                totalLatency += rt.latency;

                for (int mIdx = 0; mIdx < kM; ++mIdx)
                {
                    const juce::String name (grid::kModulableName[(size_t) mIdx]);
                    sm.spec[(size_t) mIdx] = readParamSpec (slotTree, name);
                }
                for (int stIdx = 0; stIdx < kSteps; ++stIdx)
                {
                    auto stepTree = step (slotTree, stIdx + 1);
                    const auto spec = readStepSpec (stepTree);
                    auto& dst = sm.steps[(size_t) stIdx];
                    dst.on = spec.on;
                    for (int mIdx = 0; mIdx < kM; ++mIdx)
                    {
                        const juce::String name (grid::kModulableName[(size_t) mIdx]);
                        const auto v = stepTarget (sm.spec[(size_t) mIdx], spec, mIdx, readExplicit (stepTree, name));
                        dst.has[(size_t) mIdx] = v.has_value();
                        dst.target[(size_t) mIdx] = v.value_or (0.0f);
                    }
                }

                auto mod = slotTree.getChildWithName (id::Mod);
                readEnv (mod.getChildWithName (id::Env1), sm.env1);
                auto s2 = mod.getChildWithName (id::Source2);
                sm.src2.follower = s2.getProperty (id::kind, "env").toString() == "follower";
                readEnv (s2, sm.src2.env);
                sm.src2.fol.attack = (float) (double) s2.getProperty (id::attack, 0.01);
                sm.src2.fol.release = (float) (double) s2.getProperty (id::release, 0.1);
                sm.routeCount = 0;
                for (const auto& r : mod)
                    if (r.hasType (id::Route) && sm.routeCount < kMaxModRoutes)
                    {
                        const int dst = grid::modulableIndex (r.getProperty (id::dst).toString());
                        if (dst < 0) continue;
                        sm.routes[(size_t) sm.routeCount++] = { r.getProperty (id::src).toString() == "source2" ? 1 : 0, dst,
                                                                 (float) (double) r.getProperty (id::depth, 0.0) };
                    }
            }

            for (int mc = 0; mc < grid::kMacroCount; ++mc)
            {
                auto macro = s.getChildWithName (id::Macros).getChildWithProperty (id::index, mc + 1);
                int& n = m.macroRouteCount[(size_t) mc]; n = 0;
                for (const auto& r : macro)
                    if (r.hasType (id::Route) && n < kMaxMacroRoutes)
                    {
                        const int dst = grid::modulableIndex (r.getProperty (id::param).toString());
                        const int sl = (int) r.getProperty (id::slot, 1) - 1;
                        if (dst < 0 || sl < 0 || sl >= kSlots) continue;
                        m.macroRoutes[(size_t) mc][(size_t) n++] = { sl, dst, (float) (double) r.getProperty (id::lo, 0.0),
                                                                     (float) (double) r.getProperty (id::hi, 0.0), (float) (double) r.getProperty (id::curve, 0.0) };
                    }
            }

            m.latency = juce::jmin (Engine::kMaxLatency, totalLatency);
            published.store (idx);
        }

        //======================================================================
        float param (int gridIndex) const noexcept { return params ? params->get (gridIndex) : grid::entryDefault (gridIndex); }

        void takePendingSkills() noexcept
        {
            for (auto& s : slots)
                if (auto* p = s.pending.exchange (nullptr))
                {
                    auto* old = s.skill.release();
                    s.skill.reset (p);
                    // Aucune libération ici (thread audio, §4.2) : l'ancien est rendu au message
                    // thread, qui l'a vidé avant de poser un nouveau pending. Au pire un pointeur
                    // se perd, jamais un delete sur l'audio.
                    s.retired.store (old);
                }
        }

        void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Transport& tr)
        {
            // Bloc plus grand que préparé : sous-blocs, sans allocation (§4.2).
            const int total = buffer.getNumSamples();
            int pos = 0;
            Transport t = tr;
            const double spb = clock.samplesPerBeat (t);
            while (pos < total)
            {
                const int len = juce::jmin (maxBlock, total - pos);
                juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, len);
                juce::MidiBuffer subMidi;
                for (const auto meta : midi)
                    if (meta.samplePosition >= pos && meta.samplePosition < pos + len)
                        subMidi.addEvent (meta.getMessage(), meta.samplePosition - pos);
                processBlock (sub, subMidi, t);
                if (t.playing) t.ppq += len / spb;
                pos += len;
            }
        }

        void processBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Transport& tr)
        {
            takePendingSkills();
            const int idx = published.load();
            inUse.store (idx);
            const Model& m = models[(size_t) idx];

            const int n = buffer.getNumSamples();
            const int chans = juce::jmin (2, buffer.getNumChannels());
            if (n == 0 || chans == 0) return;

            // Horloge
            ClockSettings cs;
            cs.length = grid::seqLength (param (grid::kSeqBase + grid::SLength));
            cs.stepBeats = grid::divisionBeats (grid::divisionIndex (param (grid::kSeqBase + grid::SDivision)));
            cs.swing = param (grid::kSeqBase + grid::SSwing);
            const double spb = clock.samplesPerBeat (tr);
            const double stepSamples = clock.stepDurationSamples (tr, cs);
            const double S0 = (tr.hasPosition ? tr.ppq : 0.0) * spb;   // roue libre : position gérée par Clock
            segCount = 0;
            clock.segments (tr, cs, n, [&] (int off, int len, int step, bool started)
            {
                if (segCount < (int) segs.size()) segs[(size_t) segCount++] = { off, len, step, started };
            });

            // Sec du master : copie de l'entrée, retardée de la latence totale.
            for (int ch = 0; ch < chans; ++ch) inCopy.copyFrom (ch, 0, buffer, ch, 0, n);
            masterDry.setLength (m.latency);
            masterDry.process (inCopy.getArrayOfReadPointers(), chainDry.getArrayOfWritePointers(), chans, n);

            // Macros : offsets par (emplacement, paramètre), constants sur le bloc.
            std::array<std::array<float, kM>, kSlots> macroOff {};
            for (int mc = 0; mc < grid::kMacroCount; ++mc)
            {
                const float mv = param (grid::kMacroBase + mc);
                for (int r = 0; r < m.macroRouteCount[(size_t) mc]; ++r)
                {
                    const auto& route = m.macroRoutes[(size_t) mc][(size_t) r];
                    macroOff[(size_t) route.slot0][(size_t) route.dst] += route.lo + macroCurve (mv, route.curve) * (route.hi - route.lo);
                }
            }

            // Chaîne série : l'emplacement i reçoit la sortie de i−1 (§3.2).
            for (int i = 0; i < kSlots; ++i)
            {
                const auto& sm = m.slots[(size_t) i];
                auto& rt = slots[(size_t) i];

                if (! sm.present)
                    continue;   // emplacement vide : l'audio traverse, rien à composer (coût nul, mesure J3)

                // Bases lissées, réglages d'emplacement
                for (int mIdx = 0; mIdx < kM; ++mIdx)
                    rt.base[(size_t) mIdx].setTargetValue (param (grid::slotIndex (i, grid::kModulable[(size_t) mIdx])));
                const bool slotActive = param (grid::slotIndex (i, grid::Active)) >= 0.5f;
                const int glideSamples = (int) std::lround (grid::glideSteps (param (grid::slotIndex (i, grid::Glide))) * stepSamples);
                const double fadeSamples = grid::fadeSeconds (param (grid::slotIndex (i, grid::Fade))) * sr;
                const float fadeStep = fadeSamples < 1.0 ? 1.0f : (float) (1.0 / fadeSamples);

                // Sources de modulation : suiveur sur l'entrée de l'emplacement, puis enveloppes.
                rt.follower.set (sm.src2.fol);
                for (int k = 0; k < n; ++k)
                {
                    float x = 0.0f;
                    for (int ch = 0; ch < chans; ++ch) x += buffer.getSample (ch, k);
                    folBuf[(size_t) k] = rt.follower.process (x / (float) chans);
                }
                rt.env1.render (sm.env1, S0, tr.playing, spb, folBuf.data(), &midi, n, envBuf1.data());
                if (sm.src2.follower) std::copy (folBuf.begin(), folBuf.begin() + n, envBuf2.begin());
                else rt.env2.render (sm.src2.env, S0, tr.playing, spb, folBuf.data(), &midi, n, envBuf2.data());

                // Composition, par tranche à pas constant puis par échantillon (contrat J3 b).
                for (int sIdx = 0; sIdx < segCount; ++sIdx)
                {
                    const auto& seg = segs[(size_t) sIdx];
                    if (seg.started)
                    {
                        const auto& st = sm.steps[(size_t) juce::jlimit (0, kSteps - 1, seg.step)];
                        rt.stepOn = st.on;
                        for (int mIdx = 0; mIdx < kM; ++mIdx)
                        {
                            rt.rampStart[(size_t) mIdx] = rt.current[(size_t) mIdx];   // J3-1 : depuis vpas(t_s⁻)
                            rt.hasTarget[(size_t) mIdx] = sm.present && st.has[(size_t) mIdx];
                            rt.target[(size_t) mIdx] = st.target[(size_t) mIdx];
                            rt.rampPos[(size_t) mIdx] = 0;
                        }
                        rt.rampLen = glideSamples;
                    }

                    for (int k = seg.offset; k < seg.offset + seg.len; ++k)
                    {
                        for (int mIdx = 0; mIdx < kM; ++mIdx)
                        {
                            const auto& spec = sm.spec[(size_t) mIdx];
                            const float base = rt.base[(size_t) mIdx].getNextValue();
                            const float tgt = rt.hasTarget[(size_t) mIdx] ? rt.target[(size_t) mIdx] : base;
                            float vpas;
                            if (spec.glide && rt.rampLen > 0)
                            {
                                const float phase = juce::jmin (1.0f, (float) rt.rampPos[(size_t) mIdx] / (float) rt.rampLen);
                                vpas = rt.rampStart[(size_t) mIdx] + (tgt - rt.rampStart[(size_t) mIdx]) * phase;
                                ++rt.rampPos[(size_t) mIdx];
                            }
                            else
                                vpas = tgt;
                            rt.current[(size_t) mIdx] = vpas;

                            float v;
                            if (spec.locked || spec.structural)
                                v = base;                                    // J3-2 : rien d'interne ne bouge
                            else
                            {
                                float mod = 0.0f;
                                for (int r = 0; r < sm.routeCount; ++r)
                                    if (sm.routes[(size_t) r].dst == mIdx)
                                        mod += sm.routes[(size_t) r].depth * (sm.routes[(size_t) r].src == 0 ? envBuf1[(size_t) k] : envBuf2[(size_t) k]);
                                v = juce::jlimit (0.0f, 1.0f, vpas + macroOff[(size_t) i][(size_t) mIdx] + mod);
                            }
                            values[(size_t) i][(size_t) mIdx][(size_t) k] = v;
                        }

                        // Activation : rampe vers 1 (pas actif et emplacement actif) ou 0 (§3.3.2).
                        const float aTarget = (rt.stepOn && slotActive) ? 1.0f : 0.0f;
                        if (rt.activation < aTarget) rt.activation = juce::jmin (aTarget, rt.activation + fadeStep);
                        else if (rt.activation > aTarget) rt.activation = juce::jmax (aTarget, rt.activation - fadeStep);
                        actBuf[(size_t) k] = rt.activation;
                    }
                }

                if (! sm.present || rt.skill == nullptr)
                    continue;   // emplacement vide ou skill inconnue : l'audio traverse (§3.9), latence 0

                // Sec de l'emplacement, aligné sur sa latence.
                rt.dry.setLength (rt.latency);
                rt.dry.process (buffer.getArrayOfReadPointers(), dryBuf.getArrayOfWritePointers(), chans, n);

                // Traité : entrée × alimentation (queue laissée mourir : on cesse d'alimenter), puis skill.
                for (int ch = 0; ch < chans; ++ch)
                {
                    const float* in = buffer.getReadPointer (ch);
                    float* w = wetBuf.getWritePointer (ch);
                    for (int k = 0; k < n; ++k)
                        w[k] = in[k] * (sm.tailRing ? actBuf[(size_t) k] : 1.0f);
                }
                ParamCurves curves;
                for (int mIdx = 0; mIdx < kM; ++mIdx) curves.v[(size_t) mIdx] = values[(size_t) i][(size_t) mIdx].data();
                juce::AudioBuffer<float> wetView (wetBuf.getArrayOfWritePointers(), chans, 0, n);
                rt.skill->process (wetView, curves, n);

                // Mélange local (loi de la skill), activation, gain d'emplacement.
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* out = buffer.getWritePointer (ch);
                    const float* d = dryBuf.getReadPointer (ch);
                    const float* w = wetBuf.getReadPointer (ch);
                    for (int k = 0; k < n; ++k)
                    {
                        float ad, aw;
                        mixGains (sm.law, values[(size_t) i][M_MIX][(size_t) k], ad, aw);
                        const float a = actBuf[(size_t) k];
                        const float wetInactive = sm.tailRing ? aw : 0.0f;     // coupée : le traité se tait
                        const float dryGain = 1.0f + a * (ad - 1.0f);           // inactif : le sec passe entier
                        const float wetGain = wetInactive + a * (aw - wetInactive);
                        out[k] = (d[k] * dryGain + w[k] * wetGain) * grid::gainLinear (values[(size_t) i][M_GAIN][(size_t) k]);
                    }
                }
            }

            // Master : sec retardé de la latence totale contre chaîne, loi de mélange, volume (§3.7, §3.10).
            const auto law = mixLawFromIndex (param (grid::kMasterBase + grid::MMixLaw));
            float md, mw;
            mixGains (law, param (grid::kMasterBase + grid::MMix), md, mw);
            const float vol = grid::gainLinear (param (grid::kMasterBase + grid::MVolume));
            for (int ch = 0; ch < chans; ++ch)
            {
                float* out = buffer.getWritePointer (ch);
                const float* d = chainDry.getReadPointer (ch);
                for (int k = 0; k < n; ++k)
                    out[k] = (d[k] * md + out[k] * mw) * vol;
            }
            for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
                buffer.clear (ch, 0, n);
        }
    };

    //==========================================================================
    Engine::Engine() : impl (std::make_unique<Impl>()) {}
    Engine::~Engine() = default;

    void Engine::prepare (double sampleRate, int maxBlockSize) { impl->prepare (sampleRate, maxBlockSize); }
    void Engine::reset() { impl->reset(); }
    void Engine::setParamSource (const ParamSource* source) noexcept { impl->params = source; }
    void Engine::setState (const juce::ValueTree& plugState) { impl->setState (plugState); }
    void Engine::process (juce::AudioBuffer<float>& b, const juce::MidiBuffer& m, const Transport& t) { impl->process (b, m, t); }
    int Engine::latencySamples() const noexcept { return impl->models[(size_t) impl->published.load()].latency; }
    int Engine::currentStep() const noexcept { return impl->clock.currentStep(); }
    bool Engine::isFreeRunning() const noexcept { return impl->clock.isFreeRunning(); }
    const float* Engine::valueCurve (int slot0, int modulable) const noexcept
    {
        return impl->values[(size_t) juce::jlimit (0, kSlots - 1, slot0)][(size_t) juce::jlimit (0, kM - 1, modulable)].data();
    }
}
