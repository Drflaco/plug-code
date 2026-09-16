// PlugRender — rendu hors hôte et matrice de test du socle J3.
// Garantit : un rendu est une fonction pure de (entrée audio, état, tempo,
// position) ; deux passes donnent le même fichier octet par octet (CdC §3.6).
// Sert §3.3 (séquenceur, verrous, fondus, horloge), §3.4, §3.5, §3.6, §3.7 fin.
// Usage :
//   PlugRender test [dossier_sortie]                    → matrice J3, code 0 si tout passe
//   PlugRender render --in a.wav --state s.xml --out b.wav [--bpm 120] [--ppq 0]
//                     [--bars 8] [--block 128] [--irregular] [--stopped] [--noposition]
//   PlugRender bench [--blocks N]                        → coût par bloc du socle
//   PlugRender geste --geste N [--out f.plugstate]   → construit un preset de geste (§2)
//   PlugRender reference [dir]                           → fige les rendus de référence (48 et 44,1 kHz)
//   PlugRender gen-input a.wav [--seconds 16]           → signal de test déterministe

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "BlockTimer.h"
#include "Engine.h"
#include "GridMap.h"
#include "PresetLibrary.h"
#include "StateSchema.h"
#include "StepValue.h"
#include "dummies/DummySkills.h"
#include <cstdio>
#include <cmath>

using namespace plug;

namespace
{
    // Taux d'échantillonnage du rendu : variable pour éprouver le socle aux deux
    // réglages réels (48 kHz, référence du CdC §4.1 ; 44,1 kHz, où le pilote
    // travaille aussi). Toujours restauré après usage.
    double kSampleRate = 48000.0;
    constexpr int kBlock = 128;

    struct ScopedRate
    {
        explicit ScopedRate (double r) : old (kSampleRate) { kSampleRate = r; }
        ~ScopedRate() { kSampleRate = old; }
        double old;
    };

    int failures = 0;
    juce::String report;

    void check (bool ok, const juce::String& what)
    {
        report << (ok ? "[OK]   " : "[FAIL] ") << what << "\n";
        if (! ok) ++failures;
    }

    //==========================================================================
    // Source de paramètres : les PARAM de l'état (valeurs brutes APVTS : flottant
    // 0..1, interrupteur 0/1, choix = indice). Aucune automation hors hôte.
    class StateParamSource : public ParamSource
    {
    public:
        explicit StateParamSource (const juce::ValueTree& plugState)
        {
            const auto ids = grid::allIds();
            for (int i = 0; i < (int) ids.size(); ++i)
                values[(size_t) i] = grid::entryDefault (i);
            for (const auto& c : plugState)
                if (c.hasType ("PARAM"))
                {
                    const int idx = grid::indexOf (c.getProperty ("id").toString());
                    if (idx >= 0) values[(size_t) idx] = (float) (double) c.getProperty ("value");
                }
        }
        float get (int gridIndex) const override { return values[(size_t) gridIndex]; }
        void set (int gridIndex, float v) { values[(size_t) gridIndex] = v; }
    private:
        std::array<float, (size_t) grid::kTotalCount> values {};
    };

    //==========================================================================
    // Signal de test : somme de sinus, bruit déterministe et train d'impulsions.
    juce::AudioBuffer<float> makeInput (double seconds)
    {
        const int n = (int) (seconds * kSampleRate);
        juce::AudioBuffer<float> b (2, n);
        juce::Random rng (20260916);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kSampleRate;
            const float s = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t)
                          + 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 3301.0 * t)
                          + 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
            const float imp = (i % 24000 == 0) ? 0.8f : 0.0f;
            b.setSample (0, i, s + imp);
            b.setSample (1, i, 0.9f * s + imp);
        }
        return b;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (f), kSampleRate,
                                                                          (unsigned) b.getNumChannels(), 32, {}, 0));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    bool readWav (const juce::File& f, juce::AudioBuffer<float>& out)
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::FileInputStream (f), true));
        if (r == nullptr) return false;
        out.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        return r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
    }

    //==========================================================================
    struct RenderOptions
    {
        double bpm = 120.0;
        double startPpq = 0.0;
        bool playing = true;
        bool hasPosition = true;
        bool irregular = false;          // tailles de bloc 1, 7, 128, 500, 0, 64…
        int jumpAtSample = -1;           // saut de position : à cet échantillon…
        double jumpToPpq = 0.0;          // …la position devient celle-ci
    };

    // Rendu complet : fabrique un moteur neuf, applique l'état, traite l'entrée bloc par bloc.
    juce::AudioBuffer<float> render (const juce::ValueTree& state, const juce::AudioBuffer<float>& input,
                                     const RenderOptions& o, std::vector<int>* latencyTrace = nullptr,
                                     std::vector<float>* valueTrace = nullptr, int traceSlot = 0, int traceParam = 0)
    {
        StateParamSource params (state);
        Engine engine;
        engine.setParamSource (&params);
        engine.prepare (kSampleRate, 512);
        engine.setState (state);

        // Comme un hôte : la position musicale se déduit de la position en échantillons
        // (pas d'accumulation bloc à bloc), et un saut déplace la lecture de l'entrée
        // avec la position. Sortie indexée par échantillon traité.
        const int total = input.getNumSamples();
        juce::AudioBuffer<float> out (input.getNumChannels(), total);
        out.clear();

        const int sizes[] = { 1, 7, 128, 500, 0, 64, 300 };
        const double spb = kSampleRate * 60.0 / o.bpm;
        int outPos = 0, k = 0;
        int inPos = 0;                         // lecture de l'entrée
        double ppqBase = o.startPpq;           // position au point de référence…
        int refOut = 0;                        // …qui est cet échantillon de sortie

        while (outPos < total)
        {
            int len = o.irregular ? sizes[k++ % 7] : kBlock;
            len = juce::jmin (len, total - outPos);
            if (o.jumpAtSample >= 0 && outPos < o.jumpAtSample && outPos + len > o.jumpAtSample)
                len = o.jumpAtSample - outPos;                    // coupe le bloc au saut
            if (o.jumpAtSample >= 0 && outPos == o.jumpAtSample)
            {
                ppqBase = o.jumpToPpq; refOut = outPos;
                inPos = (int) std::lround (o.jumpToPpq * spb);
            }
            len = juce::jmin (len, total - inPos);
            if (len <= 0 && ! (o.irregular && len == 0)) break;
            if (inPos >= total) break;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.copyFrom (ch, outPos, input, ch, inPos, len);

            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), outPos, len);
            juce::MidiBuffer midi;
            Transport tr;
            tr.bpm = o.bpm;
            tr.ppq = o.playing ? ppqBase + (outPos - refOut) / spb : o.startPpq;
            tr.playing = o.playing; tr.hasPosition = o.hasPosition;
            engine.process (block, midi, tr);

            if (latencyTrace) latencyTrace->push_back (engine.latencySamples());
            if (valueTrace)
            {
                const float* curve = engine.valueCurve (traceSlot, traceParam);
                for (int i = 0; i < len; ++i) valueTrace->push_back (curve[i]);
            }

            outPos += len; inPos += len;
        }
        return out;
    }

    bool sameBytes (const juce::File& a, const juce::File& b)
    {
        juce::MemoryBlock ma, mb;
        return a.loadFileAsData (ma) && b.loadFileAsData (mb) && ma == mb;
    }

    int firstDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0, float tol = 0.0f)
    {
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
        for (int i = from; i < n; ++i)
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                if (std::abs (a.getSample (ch, i) - b.getSample (ch, i)) > tol)
                    return i;
        return -1;
    }

    //==========================================================================
    // État de référence J3 : trois emplacements factices, pas générés, verrous,
    // glide, macro et enveloppe routées.
    juce::ValueTree makeReferenceState()
    {
        auto s = state::createDefault();
        state::ensureParams (s);

        state::setSkill (s, 1, dummies::kGainId, nullptr);
        state::setSkill (s, 2, dummies::kDelayId, nullptr);
        state::setSkill (s, 3, dummies::kLatentId, nullptr);

        // Emplacement 1 : gain séquencé, pas 1..16 générés, main glissé, paramA verrouillé.
        state::setRange (s, 1, "main", 0.2f, 0.9f, nullptr);
        state::setProb  (s, 1, "main", 1.0f, nullptr);
        state::setTransition (s, 1, "main", true, nullptr);
        state::setLocked (s, 1, "paramA", true, nullptr);
        state::setRange (s, 1, "paramA", 0.0f, 1.0f, nullptr);
        for (int st = 2; st <= 16; st += 3) state::setStepOn (s, 1, st, false, nullptr);
        state::generate (s, 1, 1, 16, 0.8f, nullptr);

        // Emplacement 2 : délai, pas explicites sur paramA (temps), queue coupée.
        state::setTail (s, 2, false, nullptr);
        state::setStepMode (s, 2, 3, StepMode::Explicit, nullptr);
        state::setExplicit (s, 2, 3, "paramA", 0.75f, nullptr);
        state::setStepOn (s, 2, 5, false, nullptr);
        state::setStepOn (s, 2, 6, false, nullptr);

        // Emplacement 3 : latent, un pas sur deux inactif, queue laissée mourir.
        for (int st = 1; st <= 32; st += 2) state::setStepOn (s, 3, st, false, nullptr);

        // Macro 1 → slot 1 main (+0.1), enveloppe slot 1 → paramB, suiveur slot 2 → main.
        state::addMacroRoute (s, 1, 1, "main", 0.0f, 0.1f, 0.0f, nullptr);
        state::setEnvelope (s, 1, { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } }, 0.5f, true, true, "transport", nullptr);
        state::addModRoute (s, 1, "env1", "paramB", 0.5f, nullptr);
        state::setSource2 (s, 2, "follower", 0.01f, 0.1f, nullptr);
        state::addModRoute (s, 2, "source2", "main", 0.3f, nullptr);

        // Grille : longueur 16, division 1/16, swing 0.3, macro1 = 0.5, glide slot 1 = 0.25 (1 pas).
        state::setParam (s, "seq.length",   (16 - 2) / 30.0f);
        state::setParam (s, "seq.division", grid::divisionValueFor (6));   // 1/16
        state::setParam (s, "seq.swing",    0.3f);
        state::setParam (s, "macro1",       0.5f);
        state::setParam (s, "slot01.glide", 0.25f);
        state::setParam (s, "slot01.fade",  0.2f);
        state::setParam (s, "slot03.fade",  0.1f);
        state::setParam (s, "master.mix",   0.8f);
        return s;
    }

    //==========================================================================
    void runTests (const juce::File& dir)
    {
        dir.createDirectory();
        report << "PlugRender — matrice J3 — " << juce::Time::getCurrentTime().toString (true, true) << "\n\n";

        const auto input = makeInput (8.0);
        writeWav (dir.getChildFile ("input.wav"), input);
        const auto ref = makeReferenceState();
        ref.createXml()->writeTo (dir.getChildFile ("state_ref.xml"));

        // T1 — rendu déterministe : deux passes, même fichier ; état relu depuis XML, même fichier.
        {
            RenderOptions o;
            auto a = render (ref, input, o);
            auto b = render (ref, input, o);
            writeWav (dir.getChildFile ("T1_pass1.wav"), a);
            writeWav (dir.getChildFile ("T1_pass2.wav"), b);
            check (sameBytes (dir.getChildFile ("T1_pass1.wav"), dir.getChildFile ("T1_pass2.wav")), "T1 rendu déterministe : deux passes, fichiers identiques octet par octet");

            auto reloaded = juce::ValueTree::fromXml (*juce::XmlDocument::parse (dir.getChildFile ("state_ref.xml")));
            auto c = render (reloaded, input, o);
            writeWav (dir.getChildFile ("T1_reloaded.wav"), c);
            check (sameBytes (dir.getChildFile ("T1_pass1.wav"), dir.getChildFile ("T1_reloaded.wav")), "T1 état relu depuis XML : fichier identique");
            check (a.getMagnitude (0, a.getNumSamples()) > 0.01f, "T1 le rendu n'est pas silencieux");
            check (firstDifference (a, input) >= 0, "T1 le rendu diffère de l'entrée (le socle agit)");
        }

        // T2 — indépendance à la taille de bloc.
        {
            RenderOptions o; auto a = render (ref, input, o);
            RenderOptions oi; oi.irregular = true; auto b = render (ref, input, oi);
            const int d = firstDifference (a, b);
            check (d < 0, "T2 blocs 128 et blocs irréguliers (1/7/128/500/0/64/300) : rendu identique" + (d >= 0 ? " (1re différence à " + juce::String (d) + ")" : juce::String()));
        }

        // T3 — génération reproductible à graine égale (fonction pure).
        {
            auto s1 = state::createDefault(); state::ensureParams (s1);
            auto s2 = state::createDefault(); state::ensureParams (s2);
            state::setSkill (s1, 1, dummies::kGainId, nullptr); state::setSkill (s2, 1, dummies::kGainId, nullptr);
            state::setRange (s1, 1, "main", 0.1f, 0.6f, nullptr); state::setRange (s2, 1, "main", 0.1f, 0.6f, nullptr);
            state::generate (s1, 1, 1, 32, 0.7f, nullptr);
            state::generate (s2, 1, 1, 32, 0.7f, nullptr);
            bool same = true, inRange = true, anyValue = false;
            for (int st = 1; st <= 32; ++st)
            {
                auto v1 = stepTargetFromState (s1, 1, st, "main");
                auto v2 = stepTargetFromState (s2, 1, st, "main");
                same &= (v1.has_value() == v2.has_value()) && (! v1 || std::abs (*v1 - *v2) == 0.0f);
                if (v1) { anyValue = true; inRange &= (*v1 >= 0.1f && *v1 <= 0.6f); }
            }
            check (same && anyValue, "T3 deux générations à graine maîtresse égale : mêmes valeurs de pas");
            check (inRange, "T3 valeurs générées dans la plage [0.1, 0.6]");

            state::setMasterSeed (s2, 777u, nullptr);
            state::generate (s2, 1, 1, 32, 0.7f, nullptr);
            bool differs = false;
            for (int st = 1; st <= 32; ++st)
            {
                auto v1 = stepTargetFromState (s1, 1, st, "main");
                auto v2 = stepTargetFromState (s2, 1, st, "main");
                if (v1 && v2 && *v1 != *v2) differs = true;
            }
            check (differs, "T3 graine maîtresse différente : valeurs différentes");

            // Resserrer la plage projette le même tirage : ordre relatif conservé (modèle 1, amendement 4).
            std::vector<float> before, after;
            for (int st = 1; st <= 32; ++st) if (auto v = stepTargetFromState (s1, 1, st, "main")) before.push_back (*v);
            state::setRange (s1, 1, "main", 0.3f, 0.4f, nullptr);
            for (int st = 1; st <= 32; ++st) if (auto v = stepTargetFromState (s1, 1, st, "main")) after.push_back (*v);
            bool sameOrder = before.size() == after.size();
            for (size_t i = 1; sameOrder && i < before.size(); ++i)
                sameOrder &= ((before[i] >= before[i - 1]) == (after[i] >= after[i - 1]));
            check (sameOrder && ! after.empty() && after.front() >= 0.3f && after.front() <= 0.4f, "T3 plage resserrée : même forme, amplitude réduite");

            // Capture : les valeurs deviennent explicites et ne suivent plus la plage.
            state::capture (s1, 1, 1, 32, nullptr);
            state::setRange (s1, 1, "main", 0.0f, 1.0f, nullptr);
            std::vector<float> captured;
            for (int st = 1; st <= 32; ++st) if (auto v = stepTargetFromState (s1, 1, st, "main")) captured.push_back (*v);
            check (captured == after, "T3 capture : valeurs figées, indépendantes de la plage");
        }

        // T4 — glide plus long qu'un pas : continuité à chaque bord de pas (amendement J3-1).
        {
            auto s = makeReferenceState();
            state::setParam (s, "slot01.glide", 0.5f);           // 2 pas
            state::setParam (s, "macro1", 0.0f);
            state::clearModRoutes (s, 1, nullptr);
            std::vector<float> trace;
            RenderOptions o;
            render (s, input, o, nullptr, &trace, 0, grid::modulableIndex ("main"));
            float maxJump = 0.0f; int jumpAt = -1;
            for (size_t i = 1; i < trace.size(); ++i)
            {
                const float j = std::abs (trace[i] - trace[i - 1]);
                if (j > maxJump) { maxJump = j; jumpAt = (int) i; }
            }
            float range = 0.0f;
            for (auto v : trace) range = juce::jmax (range, v);
            check (maxJump < 0.01f, "T4 glide 2 pas : saut max entre deux échantillons = " + juce::String (maxJump, 5) + (jumpAt >= 0 ? " à " + juce::String (jumpAt) : juce::String()));
            check (range > 0.3f, "T4 la valeur composée bouge bien (max " + juce::String (range, 3) + ")");
        }

        // T5 — resynchronisation exacte au saut de position (§3.3.3).
        {
            RenderOptions o; auto cont = render (ref, input, o);
            const double barPpq = 4.0;
            const int samplesPerBar = (int) (barPpq * 60.0 / o.bpm * kSampleRate);   // 96 000 à 120 BPM
            const int jumpAt = samplesPerBar;                                        // fin de mesure 1, on saute…
            RenderOptions oj; oj.jumpAtSample = jumpAt; oj.jumpToPpq = 2 * barPpq;   // …au début de la mesure 3
            auto jumped = render (ref, input, oj);
            // Après le saut, le rendu doit égaler le rendu continu lu à la position cible,
            // une fois les mémoires des factices purgées (délai à réinjection, suiveur,
            // latence) : deux secondes de marge, puis égalité stricte sur une mesure.
            const int settle = 2 * (int) kSampleRate;
            const int from = 2 * samplesPerBar;
            bool ok = true; int firstBad = -1;
            for (int i = settle; i < 2 * samplesPerBar - kBlock && ok; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    if (std::abs (jumped.getSample (ch, jumpAt + i) - cont.getSample (ch, from + i)) > 1e-6f) { ok = false; firstBad = i; }
            check (ok, "T5 saut de position : rendu égal au rendu continu à la position cible" + (ok ? juce::String() : " (écart à +" + juce::String (firstBad) + ")"));
        }

        // T6 — latence constante pendant les pas, et alignement du sec.
        {
            std::vector<int> lat;
            RenderOptions o;
            render (ref, input, o, &lat);
            bool constant = ! lat.empty();
            for (auto l : lat) constant &= (l == lat.front());
            check (constant && lat.front() == dummies::kLatentLatency, "T6 latence déclarée constante = " + juce::String (lat.empty() ? -1 : lat.front()) + " sur " + juce::String ((int) lat.size()) + " blocs, pas actifs et inactifs");

            // Chaîne : latent seul, mix 100 % sec au master → impulsion retardée de la latence, pas actif ou non.
            auto s = state::createDefault(); state::ensureParams (s);
            state::setSkill (s, 3, dummies::kLatentId, nullptr);
            for (int st = 1; st <= 32; st += 2) state::setStepOn (s, 3, st, false, nullptr);
            state::setParam (s, "master.mix", 0.0f);
            juce::AudioBuffer<float> imp (2, 4 * kBlock); imp.clear();
            imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 1.0f);
            auto out = render (s, imp, o);
            int at = -1;
            for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 0.5f) { at = i; break; }
            check (at == dummies::kLatentLatency, "T6 sec du master aligné sur la latence (" + juce::String (at) + ")");
        }

        // T7 — migration v1 → v2 et conservation de l'inconnu.
        {
            juce::ValueTree v1 ("PlugState");
            v1.setProperty ("schemaVersion", 1, nullptr);
            juce::ValueTree p ("PARAM"); p.setProperty ("id", "slot02.main", nullptr); p.setProperty ("value", 0.33, nullptr); v1.addChild (p, -1, nullptr);
            juce::ValueTree alien ("FutureSkillData"); alien.setProperty ("x", 42, nullptr); v1.addChild (alien, -1, nullptr);
            auto s = v1.createCopy();
            state::ensureSchema (s);
            check ((int) s.getProperty ("schemaVersion") == state::kSchemaVersion, "T7 schemaVersion 1 → " + juce::String (state::kSchemaVersion));
            check (s.getChildWithName ("Slots").getNumChildren() == 16 && state::slot (s, 16).isValid(), "T7 16 emplacements créés par défaut");
            check (s.getChildWithName ("FutureSkillData").isValid(), "T7 nœud inconnu conservé");
            check (state::step (state::slot (s, 4), 32).isValid(), "T7 32 pas par emplacement");
            auto xml = s.createXml(); auto back = juce::ValueTree::fromXml (*xml);
            check (back.isEquivalentTo (s), "T7 aller-retour XML équivalent");
        }

        // T8 — verrou : la valeur composée = base, quelles que soient les sources internes (amendement J3-2).
        {
            auto s = makeReferenceState();
            state::setLocked (s, 1, "main", true, nullptr);      // main porte des pas générés, une macro et du glide
            state::setParam (s, "slot01.main", 0.42f);
            std::vector<float> trace;
            RenderOptions o;
            render (s, input, o, nullptr, &trace, 0, grid::modulableIndex ("main"));
            bool allBase = ! trace.empty();
            for (auto v : trace) allBase &= (std::abs (v - 0.42f) < 1e-6f);
            check (allBase, "T8 paramètre verrouillé : valeur composée = base sur tout le rendu");
        }

        // T9 — lois de mélange au point milieu (§3.7).
        {
            float d, w;
            mixGains (MixLaw::Minus6, 0.5f, d, w); check (std::abs (d - 0.5f) < 1e-6f && std::abs (w - 0.5f) < 1e-6f, "T9 loi -6 dB : 0.5 / 0.5");
            mixGains (MixLaw::Minus3, 0.5f, d, w); check (std::abs (d - 0.70710678f) < 1e-5f && std::abs (w - 0.70710678f) < 1e-5f, "T9 loi -3 dB : 0.7071 / 0.7071");
            mixGains (MixLaw::Zero,   0.5f, d, w); check (std::abs (d - 1.0f) < 1e-6f && std::abs (w - 1.0f) < 1e-6f, "T9 loi 0 dB : 1 / 1");
        }

        // T10 — transport arrêté : pas figé ; sans position : roue libre dégradée mais déterministe.
        {
            RenderOptions o; o.playing = false;
            auto a = render (ref, input, o); auto b = render (ref, input, o);
            check (firstDifference (a, b) < 0, "T10 transport arrêté : rendu déterministe");
            RenderOptions n; n.hasPosition = false;
            auto c = render (ref, input, n); auto d = render (ref, input, n);
            check (firstDifference (c, d) < 0, "T10 sans position hôte : roue libre à 120 BPM déterministe");
        }

        // T11 — auto-tests des factices (contrat §3.9, cas numériques).
        {
            juce::String log;
            const bool ok = dummies::selfTestAll (log);   // avant de composer le message : l'ordre d'évaluation des arguments n'est pas garanti
            check (ok, "T11 auto-tests des modules factices\n" + log);
        }

        // T12 — 44,1 kHz : le pilote y travaille aussi (ETAT Rév. 5). Mêmes garanties qu'à 48 kHz.
        {
            ScopedRate rate (44100.0);
            const auto in44 = makeInput (4.0);
            const auto ref44 = makeReferenceState();
            RenderOptions o;
            auto a = render (ref44, in44, o);
            auto b = render (ref44, in44, o);
            writeWav (dir.getChildFile ("T12_44k_pass1.wav"), a);
            writeWav (dir.getChildFile ("T12_44k_pass2.wav"), b);
            check (sameBytes (dir.getChildFile ("T12_44k_pass1.wav"), dir.getChildFile ("T12_44k_pass2.wav")), "T12 44,1 kHz : deux passes identiques octet par octet");

            RenderOptions oi; oi.irregular = true;
            auto c = render (ref44, in44, oi);
            check (firstDifference (a, c) < 0, "T12 44,1 kHz : blocs irréguliers, rendu identique");

            std::vector<int> lat;
            render (ref44, in44, o, &lat);
            bool constant = ! lat.empty();
            for (auto l : lat) constant &= (l == lat.front());
            check (constant && lat.front() == dummies::kLatentLatency, "T12 44,1 kHz : latence déclarée constante = " + juce::String (lat.empty() ? -1 : lat.front()));
        }

        // T13 — non-régression du rendu : comparaison octet par octet aux références
        // figées avant l'optimisation du socle (J4a phase 0). Une optimisation qui change
        // un seul octet est fausse. Références : measure/j4a/ref, écrites par
        // « PlugRender reference » et versionnées.
        {
            const auto refDir = juce::File::getCurrentWorkingDirectory().getChildFile ("measure/j4a/ref");
            const auto r48 = refDir.getChildFile ("socle_48k.wav");
            const auto r44 = refDir.getChildFile ("socle_44k.wav");
            if (r48.existsAsFile() && r44.existsAsFile())
            {
                check (sameBytes (dir.getChildFile ("T1_pass1.wav"), r48), "T13 rendu 48 kHz identique à la référence figée");
                check (sameBytes (dir.getChildFile ("T12_44k_pass1.wav"), r44), "T13 rendu 44,1 kHz identique à la référence figée");
            }
            else
                report << "[note] T13 : aucune référence figée dans measure/j4a/ref\n";
        }

        report << "\nRésultat : " << (failures == 0 ? "TOUT PASSE" : juce::String (failures) + " ÉCHEC(S)") << "\n";
    }

    juce::String arg (const juce::StringArray& a, const juce::String& key, const juce::String& def = {})
    {
        const int i = a.indexOf (key);
        return (i >= 0 && i + 1 < a.size()) ? a[i + 1] : def;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray a;
    for (int i = 1; i < argc; ++i) a.add (argv[i]);
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (a.isEmpty() || a[0] == "test")
    {
        const auto dir = cwd.getChildFile (a.size() > 1 ? a[1] : "measure/j3");
        runTests (dir);
        std::printf ("%s", report.toRawUTF8());
        dir.getChildFile ("rapport_J3.txt").replaceWithText (report);
        return failures == 0 ? 0 : 1;
    }

    if (a[0] == "geste")
    {
        // Construit l'état d'un des trois gestes de référence (CdC §2) et l'écrit en
        // .plugstate. Un preset porte l'état COMPLET (§3.6, décision pilote J4a) :
        // identités de skill, ordre, valeurs, motifs, verrous, graines.
        // Les valeurs de paramètres restent neutres ici ; elles se règlent à l'écoute,
        // c'est le travail du pilote (§2, « se décident en écoutant, pas en spécifiant »).
        const int geste = arg (a, "--geste", "1").getIntValue();
        const auto out = cwd.getChildFile (arg (a, "--out", "presets/geste" + juce::String (geste) + ".plugstate"));

        auto s = state::createDefault();
        state::ensureParams (s);

        // Chaîne série : l'emplacement N reçoit la sortie de N−1 (§3.2).
        juce::StringArray chain;
        switch (geste)
        {
            case 1:  chain = { "core.fm", "core.grain", "core.gate", "core.reverb" }; break;   // texture profonde
            case 2:  chain = { "core.grain", "core.repitch", "core.delay" }; break;            // granulaire de texture
            default: chain = { "core.repitch", "core.delay", "core.filter" }; break;           // repitch fondu
        }
        for (int i = 0; i < chain.size(); ++i)
            state::setSkill (s, i + 1, chain[i], nullptr);

        // Séquenceur : 16 pas en doubles croches, la ligne du geste porte la variation.
        state::setParam (s, "seq.length",   (16 - 2) / 30.0f);
        state::setParam (s, "seq.division", grid::divisionValueFor (6));
        state::setParam (s, "seq.swing",    0.0f);
        state::setParam (s, "master.mix",   1.0f);

        if (geste == 1)
        {
            // Le gate découpe, la réverbe laisse mourir sa queue (§3.3.2, défaut proposé).
            for (int st = 1; st <= 16; ++st) if (st % 4 != 1) state::setStepOn (s, 3, st, false, nullptr);
            state::setTail (s, 4, true, nullptr);
            state::setRange (s, 1, "main", 0.2f, 0.7f, nullptr);
            state::generate (s, 1, 1, 16, 0.6f, nullptr);
        }
        else if (geste == 2)
        {
            // Texture : grains lents et dispersés, pas de grille nette (§2, geste 2).
            state::setTail (s, 3, true, nullptr);
            state::setRange (s, 1, "main", 0.3f, 0.8f, nullptr);
            state::setProb  (s, 1, "main", 0.4f, nullptr);
            state::generate (s, 1, 1, 16, 0.5f, nullptr);
        }
        else
        {
            // Geste 3 : le fondu agit sur le repitch lui-même, donc glissement sur sa hauteur.
            state::setTransition (s, 1, "main", true, nullptr);
            state::setParam (s, "slot01.glide", 0.5f);
            state::setRange (s, 1, "main", 0.25f, 0.75f, nullptr);
            state::generate (s, 1, 1, 16, 0.7f, nullptr);
            state::setTail (s, 2, true, nullptr);
        }

        const bool ok = PresetLibrary::write (out, s);
        std::printf ("%s : %s\n", ok ? "preset écrit" : "ÉCHEC", out.getFullPathName().toRawUTF8());
        for (int i = 0; i < chain.size(); ++i)
            std::printf ("  emplacement %d : %s%s\n", i + 1, chain[i].toRawUTF8(),
                         SkillRegistry::instance().info (chain[i]) == nullptr ? "   (absente du registre : l'audio traversera, §3.9)" : "");
        return ok ? 0 : 2;
    }

    if (a[0] == "reference")
    {
        // Fige les rendus de référence du socle (deux taux) : tout changement ultérieur
        // du moteur doit reproduire ces fichiers octet par octet (contrat J4a phase 0).
        const auto dir = cwd.getChildFile (a.size() > 1 ? a[1] : "measure/j4a/ref");
        dir.createDirectory();
        for (double rate : { 48000.0, 44100.0 })
        {
            ScopedRate sr (rate);
            const auto in = makeInput (rate == 48000.0 ? 8.0 : 4.0);
            RenderOptions o;
            auto out = render (makeReferenceState(), in, o);
            const auto f = dir.getChildFile (rate == 48000.0 ? "socle_48k.wav" : "socle_44k.wav");
            writeWav (f, out);
            std::printf ("référence écrite : %s\n", f.getFullPathName().toRawUTF8());
        }
        return 0;
    }

    if (a[0] == "bench")
    {
        // Coût par bloc du socle seul (sans enveloppe VST3) : état vide, puis état de référence
        // (trois factices, pas générés, macro, enveloppe, suiveur). Référence du budget §4.4 pour J3.
        const int blocks = arg (a, "--blocks", "200000").getIntValue();
        ScopedRate rate (arg (a, "--rate", "48000").getDoubleValue());
        juce::AudioBuffer<float> buf (2, kBlock);
        juce::Random rng (7);
        std::printf ("— %d Hz, blocs de %d (%.2f µs par bloc), budget 25 %% = %.1f µs —\n",
                     (int) kSampleRate, kBlock, 1e6 * kBlock / kSampleRate, 0.25 * 1e6 * kBlock / kSampleRate);
        for (int which = 0; which < 3; ++which)
        {
            // Cas 3 : les 16 emplacements occupés — le pire cas de la grille, celui que
            // le budget §4.4 doit tenir avant même le DSP des vraies skills.
            auto st = which == 0 ? [] { auto s = state::createDefault(); state::ensureParams (s); return s; }()
                    : which == 1 ? makeReferenceState()
                    : [] {
                          auto s = makeReferenceState();
                          for (int sl = 4; sl <= 16; ++sl)
                          {
                              state::setSkill (s, sl, sl % 3 == 0 ? dummies::kDelayId : dummies::kGainId, nullptr);
                              state::generate (s, sl, 1, 16, 0.7f, nullptr);
                          }
                          return s;
                      }();
            StateParamSource params (st);
            Engine engine; engine.setParamSource (&params); engine.prepare (kSampleRate, kBlock); engine.setState (st);
            BlockTimer t;
            double ppq = 0.0;
            for (int b = 0; b < 2000 + blocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < kBlock; ++i) buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
                juce::MidiBuffer midi; Transport tr; tr.ppq = ppq;
                if (b == 2000) t.reset();
                t.begin(); engine.process (buf, midi, tr); t.end ((uint32_t) kBlock);
                ppq += kBlock / (kSampleRate / 2.0);
            }
            const auto s = t.compute();
            const double blockUs = 1e6 * kBlock / kSampleRate;
            std::printf ("%s : %d blocs — moyenne %.3f µs, p50 %.3f, p99 %.3f, p99.9 %.3f (%.3f %% du bloc), max %.3f µs\n",
                         which == 0 ? "socle vide (16 emplacements sans skill)   "
                       : which == 1 ? "3 emplacements (référence J3)             "
                                    : "16 emplacements occupés (pire cas grille) ",
                         (int) s.count, s.meanUs, s.p50Us, s.p99Us, s.p999Us, 100.0 * s.p999Us / blockUs, s.maxUs);
        }
        return 0;
    }

    if (a[0] == "gen-input")
    {
        auto b = makeInput (arg (a, "--seconds", "16").getDoubleValue());
        return writeWav (cwd.getChildFile (a[1]), b) ? 0 : 2;
    }

    if (a[0] == "render")
    {
        juce::AudioBuffer<float> in;
        if (! readWav (cwd.getChildFile (arg (a, "--in")), in)) { std::printf ("entrée illisible\n"); return 2; }
        auto xml = juce::XmlDocument::parse (cwd.getChildFile (arg (a, "--state")));
        if (xml == nullptr) { std::printf ("état illisible\n"); return 2; }
        auto st = juce::ValueTree::fromXml (*xml);
        state::ensureSchema (st);
        RenderOptions o;
        o.bpm = arg (a, "--bpm", "120").getDoubleValue();
        o.startPpq = arg (a, "--ppq", "0").getDoubleValue();
        o.irregular = a.contains ("--irregular");
        o.playing = ! a.contains ("--stopped");
        o.hasPosition = ! a.contains ("--noposition");
        auto out = render (st, in, o);
        return writeWav (cwd.getChildFile (arg (a, "--out")), out) ? 0 : 2;
    }

    std::printf ("usage : PlugRender test [dir] | render --in --state --out | gen-input out.wav\n");
    return 2;
}
