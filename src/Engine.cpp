#include "Engine.h"
#include "GridMap.h"
#include "Modulation.h"
#include "Skill.h"
#include "StateSchema.h"
#include "StepValue.h"
#include "dummies/DummySkills.h"
#include "skills/Skills.h"
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
            // Entrées réellement composées : celles que la skill déclare, plus mix et gain
            // (lus par le moteur). Composer les 13 systématiquement coûtait l'essentiel des
            // 8 µs par emplacement du J3 (MESURES_J3) ; ParamCurves porte nullptr ailleurs.
            std::array<bool, kM> used {};
            std::array<bool, kM> hasMod {};  // une route de modulation vise cette entrée
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

        // Vrai si la courbe ne bouge pas du bloc. Une comparaison par échantillon coûte
        // mille fois moins que le cos/sin du mélange, et ouvre le chemin rapide sans
        // rien changer au résultat (J4a phase 0).
        bool isConstant (const float* v, int n) noexcept
        {
            for (int i = 1; i < n; ++i)
                if (v[i] != v[0]) return false;
            return true;
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

        // J4b c-1 : pas courant (6 bits, décalé de 1 pour que -1 tienne), transport,
        // roue libre. Un seul mot : la vue lit un état cohérent ou rien.
        std::atomic<uint32_t> uiState { 0 };
        static uint32_t packUi (int step, bool playing, bool freeRunning) noexcept
        {
            return (uint32_t) ((step + 1) & 0x3f) | (playing ? 0x40u : 0u) | (freeRunning ? 0x80u : 0u);
        }

        // Tampons préalloués (§4.2)
        juce::AudioBuffer<float> inCopy, dryBuf, wetBuf, chainDry;
        std::array<std::array<std::vector<float>, kM>, kSlots> values;   // courbes composées
        std::vector<float> envBuf1, envBuf2, folBuf, actBuf;
        struct Seg { int offset, len, step; bool started; };
        std::array<Seg, 64> segs {};
        int segCount = 0;

        // Le catalogue existe avant tout état : un moteur sans registre peuplé verrait
        // toutes les skills comme inconnues et laisserait passer l'audio (§3.9), ce qui
        // ressemble à un succès et n'en est pas. Mesuré le 16/09 : la chaîne de référence
        // affichait le coût d'un socle vide parce que PlugRender ne peuplait rien.
        Impl() { dummies::registerAll(); registerAllSkills(); }

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

                // mix et gain sont lus par le moteur lui-même ; le reste vient de la skill.
                sm.used[(size_t) M_MIX] = true;
                sm.used[(size_t) M_GAIN] = true;
                if (info != nullptr)
                    for (const auto& d : info->params)
                        if (d.modulable >= 0 && d.modulable < kM) sm.used[(size_t) d.modulable] = true;
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
                        sm.hasMod[(size_t) dst] = true;
                        sm.used[(size_t) dst] = true;   // une route sur une entrée non déclarée reste sans effet audible, mais la courbe doit exister
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

            // J4b c-1 : publication de la position de lecture pour l'interface. Un seul
            // store relâché en fin de bloc — rien à lire, rien à verrouiller, et le rendu
            // n'en dépend pas (T13 le prouve octet par octet).
            uiState.store (packUi (clock.currentStep(), tr.playing, clock.isFreeRunning()), std::memory_order_relaxed);
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

                // Sources de modulation : calculées seulement si une route les lit, ou si
                // une enveloppe se déclenche sur l'audio (le suiveur lui sert de détecteur).
                // Une source que personne ne lit ne change aucun échantillon de sortie ;
                // ne pas la calculer laisse le rendu identique (J4a phase 0).
                bool needEnv1 = false, needEnv2 = false;
                for (int r = 0; r < sm.routeCount; ++r)
                    (sm.routes[(size_t) r].src == 0 ? needEnv1 : needEnv2) = true;
                const bool env2IsFollower = sm.src2.follower;
                const bool needFollower = (needEnv2 && env2IsFollower)
                                        || (needEnv1 && sm.env1.trigger == 1)
                                        || (needEnv2 && ! env2IsFollower && sm.src2.env.trigger == 1);

                if (needFollower)
                {
                    rt.follower.set (sm.src2.fol);
                    const float* in0 = buffer.getReadPointer (0);
                    const float* in1 = chans > 1 ? buffer.getReadPointer (1) : in0;
                    const float inv = 1.0f / (float) chans;
                    for (int k = 0; k < n; ++k)
                        folBuf[(size_t) k] = rt.follower.process ((chans > 1 ? in0[k] + in1[k] : in0[k]) * inv);
                }
                if (needEnv1)
                    rt.env1.render (sm.env1, S0, tr.playing, spb, folBuf.data(), &midi, n, envBuf1.data());
                if (needEnv2)
                {
                    if (env2IsFollower) std::copy (folBuf.begin(), folBuf.begin() + n, envBuf2.begin());
                    else rt.env2.render (sm.src2.env, S0, tr.playing, spb, folBuf.data(), &midi, n, envBuf2.data());
                }

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

                    // Une entrée par une : le chemin rapide remplit la tranche d'une seule
                    // valeur quand rien ne peut bouger dessus, le chemin général reste le
                    // calcul échantillon par échantillon du contrat J3 b. Les deux donnent
                    // le même nombre — c'est ce que vérifie T13.
                    for (int mIdx = 0; mIdx < kM; ++mIdx)
                    {
                        if (! sm.used[(size_t) mIdx])
                        {
                            rt.base[(size_t) mIdx].skip (seg.len);   // personne ne lit cette courbe ; le lissage avance quand même
                            continue;
                        }

                        const auto& spec = sm.spec[(size_t) mIdx];
                        auto& sv = rt.base[(size_t) mIdx];
                        const bool locked = spec.locked || spec.structural;
                        const bool gliding = spec.glide && rt.rampLen > 0 && rt.rampPos[(size_t) mIdx] < rt.rampLen;
                        const bool modded = sm.hasMod[(size_t) mIdx] && ! locked;
                        float* out = values[(size_t) i][(size_t) mIdx].data();

                        if (! gliding && ! modded && ! sv.isSmoothing())
                        {
                            const float base = sv.getTargetValue();
                            const float tgt = rt.hasTarget[(size_t) mIdx] ? rt.target[(size_t) mIdx] : base;
                            // Rampe finie : le chemin général évalue rampStart + (tgt − rampStart) × 1,
                            // qui n'est pas toujours tgt au bit près. On garde la même écriture.
                            const float vpas = (spec.glide && rt.rampLen > 0)
                                             ? rt.rampStart[(size_t) mIdx] + (tgt - rt.rampStart[(size_t) mIdx])
                                             : tgt;
                            // J3-6 : verrouillé = la valeur de pas joue, sans macro ni modulation.
                            const float v = locked ? vpas : juce::jlimit (0.0f, 1.0f, vpas + macroOff[(size_t) i][(size_t) mIdx]);
                            std::fill (out + seg.offset, out + seg.offset + seg.len, v);
                            sv.skip (seg.len);                       // lissage arrivé : skip et getNextValue rendent la même valeur
                            rt.current[(size_t) mIdx] = vpas;
                            continue;
                        }

                        for (int k = seg.offset; k < seg.offset + seg.len; ++k)
                        {
                            const float base = sv.getNextValue();
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
                            if (locked)
                                v = vpas;                                    // J3-6 : la valeur de pas joue, rien d'autre ne la bouge
                            else
                            {
                                float mod = 0.0f;
                                for (int r = 0; r < sm.routeCount; ++r)
                                    if (sm.routes[(size_t) r].dst == mIdx)
                                        mod += sm.routes[(size_t) r].depth * (sm.routes[(size_t) r].src == 0 ? envBuf1[(size_t) k] : envBuf2[(size_t) k]);
                                v = juce::jlimit (0.0f, 1.0f, vpas + macroOff[(size_t) i][(size_t) mIdx] + mod);
                            }
                            out[k] = v;
                        }
                    }

                    // Activation : rampe vers 1 (pas actif et emplacement actif) ou 0 (§3.3.2).
                    // La cible ne change pas dans une tranche : quand la rampe est finie,
                    // l'activation est plate et se pose d'un bloc.
                    const float aTarget = (rt.stepOn && slotActive) ? 1.0f : 0.0f;
                    if (rt.activation == aTarget)
                        std::fill (actBuf.begin() + seg.offset, actBuf.begin() + seg.offset + seg.len, aTarget);
                    else
                        for (int k = seg.offset; k < seg.offset + seg.len; ++k)
                        {
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
                    if (! sm.tailRing) std::copy (in, in + n, w);
                    else for (int k = 0; k < n; ++k) w[k] = in[k] * actBuf[(size_t) k];
                }
                // Une entrée non déclarée par la skill n'a pas de courbe : le pointeur est nul.
                // Une skill qui lit ce qu'elle n'a pas déclaré tombe tout de suite au lieu de
                // lire des valeurs périmées (contrat §3.9, voir SKILL_TEMPLATE).
                ParamCurves curves;
                for (int mIdx = 0; mIdx < kM; ++mIdx)
                    curves.v[(size_t) mIdx] = sm.used[(size_t) mIdx] ? values[(size_t) i][(size_t) mIdx].data() : nullptr;
                juce::AudioBuffer<float> wetView (wetBuf.getArrayOfWritePointers(), chans, 0, n);
                rt.skill->process (wetView, curves, n);

                // Mélange local (loi de la skill), activation, gain d'emplacement.
                // La loi -3 dB demande un cosinus et un sinus : les calculer par échantillon
                // coûtait la moitié du temps d'un emplacement (MESURES_J3). Quand mix, gain et
                // activation ne bougent pas du bloc — le cas ordinaire — les trois gains se
                // calculent une fois. L'écriture finale reste la même, donc le nombre aussi.
                const float* mixCurve = values[(size_t) i][M_MIX].data();
                const float* gainCurve = values[(size_t) i][M_GAIN].data();
                const bool flat = isConstant (mixCurve, n) && isConstant (gainCurve, n) && isConstant (actBuf.data(), n);

                float cad = 0.0f, caw = 0.0f, cDryGain = 0.0f, cWetGain = 0.0f, cGain = 0.0f;
                if (flat)
                {
                    mixGains (sm.law, mixCurve[0], cad, caw);
                    const float a = actBuf[0];
                    const float wetInactive = sm.tailRing ? caw : 0.0f;
                    cDryGain = 1.0f + a * (cad - 1.0f);
                    cWetGain = wetInactive + a * (caw - wetInactive);
                    cGain = grid::gainLinear (gainCurve[0]);
                }

                for (int ch = 0; ch < chans; ++ch)
                {
                    float* out = buffer.getWritePointer (ch);
                    const float* d = dryBuf.getReadPointer (ch);
                    const float* w = wetBuf.getReadPointer (ch);

                    if (flat)
                    {
                        for (int k = 0; k < n; ++k)
                            out[k] = (d[k] * cDryGain + w[k] * cWetGain) * cGain;
                        continue;
                    }

                    for (int k = 0; k < n; ++k)
                    {
                        float ad, aw;
                        mixGains (sm.law, mixCurve[k], ad, aw);
                        const float a = actBuf[(size_t) k];
                        const float wetInactive = sm.tailRing ? aw : 0.0f;     // coupée : le traité se tait
                        const float dryGain = 1.0f + a * (ad - 1.0f);           // inactif : le sec passe entier
                        const float wetGain = wetInactive + a * (aw - wetInactive);
                        out[k] = (d[k] * dryGain + w[k] * wetGain) * grid::gainLinear (gainCurve[k]);
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
    UiSnapshot Engine::uiSnapshot() const noexcept
    {
        const uint32_t p = impl->uiState.load (std::memory_order_relaxed);
        UiSnapshot s;
        s.step = (int) (p & 0x3fu) - 1;
        s.playing = (p & 0x40u) != 0;
        s.freeRunning = (p & 0x80u) != 0;
        return s;
    }
    const float* Engine::valueCurve (int slot0, int modulable) const noexcept
    {
        return impl->values[(size_t) juce::jlimit (0, kSlots - 1, slot0)][(size_t) juce::jlimit (0, kM - 1, modulable)].data();
    }
}
