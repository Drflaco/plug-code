// PlugSkillTest — le vérificateur de contrat des skills (CdC §3.9) et leur banc.
// Garantit : aucune skill n'entre au registre sans que ces épreuves passent.
// Elles sont mécaniques — identité, aide, latence, blocs, silence, stabilité,
// déterminisme, allocation — et s'ajoutent aux cas numériques que la skill
// fournit elle-même (selfTest). Sert §3.9, §4.2, §4.3, §4.4.
// Usage : PlugSkillTest [--blocks N] [--rate 48000] [--only <id>]
// Code de retour 0 si tout passe.

#include <juce_audio_utils/juce_audio_utils.h>
#include "BlockTimer.h"
#include "GridMap.h"
#include "Skill.h"
#include "skills/Skills.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

using namespace plug;

namespace
{
    int failures = 0;
    juce::String report;

    void check (bool ok, const juce::String& what)
    {
        report << (ok ? "    [OK]   " : "    [FAIL] ") << what << "\n";
        if (! ok) ++failures;
    }

    // Courbes de test : une valeur constante par entrée déclarée, nullptr ailleurs —
    // exactement ce que le socle fournit (invariant de ParamCurves).
    struct Curves
    {
        std::array<std::vector<float>, grid::kModulableCount> store;
        ParamCurves curves;

        Curves (const SkillInfo& info, int n, float value)
        {
            for (const auto& d : info.params)
                if (d.modulable >= 0 && d.modulable < grid::kModulableCount)
                {
                    store[(size_t) d.modulable].assign ((size_t) juce::jmax (1, n), value);
                    curves.v[(size_t) d.modulable] = store[(size_t) d.modulable].data();
                }
        }
        void set (int m, float v) { if (! store[(size_t) m].empty()) std::fill (store[(size_t) m].begin(), store[(size_t) m].end(), v); }
        void ramp (int m, float a, float b)
        {
            auto& s = store[(size_t) m];
            for (size_t i = 0; i < s.size(); ++i) s[i] = a + (b - a) * (float) i / (float) juce::jmax<size_t> (1, s.size() - 1);
        }
    };

    bool finite (const juce::AudioBuffer<float>& b, int n)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = b.getSample (ch, i);
                if (! std::isfinite (v) || std::abs (v) > 64.0f) return false;
            }
        return true;
    }

    void fillNoise (juce::AudioBuffer<float>& b, int n, juce::Random& rng)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i) b.setSample (ch, i, rng.nextFloat() * 1.6f - 0.8f);
    }

    //==========================================================================
    void verify (const juce::String& id, double sr, int blocks)
    {
        auto& reg = SkillRegistry::instance();
        const auto* info = reg.info (id);
        auto skill = reg.create (id);
        report << "\n== " << id << " — "
               << (info != nullptr ? juce::String::fromUTF8 (info->label) : juce::String ("?")) << " ==\n";
        if (info == nullptr || skill == nullptr) { check (false, "skill introuvable dans le registre"); return; }

        // 1. Identité et déclarations (§3.9).
        check (info->id.isNotEmpty() && info->id.containsChar ('.'), "identité « " + info->id + " » de la forme famille.nom");
        check (info->version >= 1, "version déclarée : " + juce::String (info->version));
        // ENCODAGE (défaut vu dans Live le 17/09) : le libellé est des OCTETS UTF-8, jamais
        // une juce::String construite implicitement — juce::String(const char*) décode
        // octet par octet et « Délai » y devient « DÃ©lai ». On le décode explicitement, et
        // on vérifie que ces octets SONT de l'UTF-8 valide : si /utf-8 disparaissait du
        // CMake, MSVC réencoderait les littéraux dans la page de code ANSI et ce cas tombe.
        check (juce::String::fromUTF8 (info->label).isNotEmpty(), "libellé renseigné");
        check (juce::CharPointer_UTF8::isValidString (info->label, std::numeric_limits<int>::max()),
               "libellé encodé en UTF-8 valide");
        {
            bool utf8 = true;
            for (const auto& d : info->params)
                utf8 &= juce::CharPointer_UTF8::isValidString (d.label, std::numeric_limits<int>::max())
                     && juce::CharPointer_UTF8::isValidString (d.help, std::numeric_limits<int>::max())
                     && juce::CharPointer_UTF8::isValidString (d.unit, std::numeric_limits<int>::max());
            check (utf8, "libellés, aides et unités des paramètres encodés en UTF-8 valide");
        }
        check (! info->factice, "déclarée non factice (entre au catalogue §3.8)");
        check (! info->params.empty(), juce::String (info->params.size()) + " paramètre(s) déclaré(s)");

        std::set<int> seen;
        bool mapping = true, helps = true, dup = false;
        for (const auto& d : info->params)
        {
            mapping &= (d.modulable >= 0 && d.modulable < grid::kModulableCount);
            helps &= (d.label != nullptr && *d.label != 0 && d.help != nullptr && juce::String (d.help).length() >= 10);
            if (! seen.insert (d.modulable).second) dup = true;
        }
        check (mapping, "toutes les entrées visent la grille générique (0..12)");
        check (! dup, "aucune entrée occupée deux fois");
        check (helps, "chaque paramètre porte un libellé et une aide en français (§3.11)");

        bool lockClasses = true;
        for (const auto& d : info->params)
            lockClasses &= (d.lockClass == LockClass::Free || d.lockClass == LockClass::LockedByDefault || d.lockClass == LockClass::Structural);
        check (lockClasses, "classe de verrou déclarée pour chaque paramètre (§3.3.1)");

        // 1 bis. Lisibilité (J4b c-2). `unit` et `display` sont OPTIONNELS : une skill
        // qui ne les remplit pas reste conforme, l'interface retombe sur 0,00–1,00.
        // Mais une fonction fournie doit dire quelque chose aux trois points de la
        // course, sans quoi le knob afficherait du vide là où il promet une unité.
        {
            bool displays = true;
            int declared = 0;
            for (const auto& d : info->params)
            {
                if (d.display == nullptr) continue;
                ++declared;
                for (float v : { 0.0f, 0.5f, 1.0f })
                    displays &= d.display (v).isNotEmpty();
            }
            check (displays, declared == 0
                                 ? juce::String ("aucun texte de valeur déclaré : repli 0,00–1,00 (J4b c-2, étape 3)")
                                 : juce::String (declared) + " paramètre(s) à texte déclaré : non vide en 0, 0,5 et 1");
        }

        // 2. Latence déclarée : constante entre deux prepare, jamais négative (§4.3).
        const int maxBlock = 512;
        skill->prepare (sr, maxBlock);
        const int lat1 = skill->latencySamples();
        skill->prepare (sr, maxBlock);
        const int lat2 = skill->latencySamples();
        check (lat1 >= 0 && lat1 == lat2, "latence déclarée " + juce::String (lat1) + ", stable entre deux prepare");

        // 3. Silence en entrée juste après reset → silence en sortie.
        {
            skill->prepare (sr, maxBlock); skill->reset();
            juce::AudioBuffer<float> b (2, 256); b.clear();
            Curves c (*info, 256, 0.5f);
            skill->process (b, c.curves, 256);
            check (b.getMagnitude (0, 256) < 1.0e-6f && finite (b, 256), "silence après reset → silence (pas de bruit propre)");
        }

        // 4. Blocs de taille variable, irréguliers ou vides (§4.2) + stabilité numérique.
        {
            skill->prepare (sr, maxBlock); skill->reset();
            juce::Random rng (20260916);
            const int sizes[] = { 0, 1, 7, 128, 512, 3, 0, 64, 333 };
            bool ok = true;
            for (int round = 0; round < 40 && ok; ++round)
                for (int sz : sizes)
                {
                    juce::AudioBuffer<float> b (2, juce::jmax (1, sz));
                    b.clear(); fillNoise (b, sz, rng);
                    Curves c (*info, juce::jmax (1, sz), 0.5f);
                    skill->process (b, c.curves, sz);
                    ok &= finite (b, sz);
                }
            check (ok, "blocs 0/1/7/128/512/3/64/333 acceptés, sortie finie et bornée");
        }

        // 5. Balayage complet de chaque paramètre : rien ne diverge sur les bornes.
        {
            bool ok = true;
            for (const auto& d : info->params)
            {
                skill->prepare (sr, maxBlock); skill->reset();
                juce::Random rng (7);
                for (int pass = 0; pass < 12; ++pass)
                {
                    juce::AudioBuffer<float> b (2, 256);
                    fillNoise (b, 256, rng);
                    Curves c (*info, 256, 0.5f);
                    c.set (d.modulable, pass % 2 == 0 ? 0.0f : 1.0f);
                    if (pass % 3 == 2) c.ramp (d.modulable, 0.0f, 1.0f);
                    skill->process (b, c.curves, 256);
                    ok &= finite (b, 256);
                }
            }
            check (ok, "chaque paramètre balayé de 0 à 1 (bornes et rampes) sans divergence");
        }

        // 6. Déterminisme : deux instances neuves, même entrée, même sortie (§3.6).
        {
            auto s2 = reg.create (id);
            skill->prepare (sr, maxBlock); skill->reset();
            s2->prepare (sr, maxBlock); s2->reset();
            juce::Random r1 (99), r2 (99);
            bool same = true;
            for (int blk = 0; blk < 32 && same; ++blk)
            {
                juce::AudioBuffer<float> a (2, 128), b (2, 128);
                fillNoise (a, 128, r1); fillNoise (b, 128, r2);
                Curves ca (*info, 128, 0.42f), cb (*info, 128, 0.42f);
                skill->process (a, ca.curves, 128);
                s2->process (b, cb.curves, 128);
                for (int ch = 0; ch < 2 && same; ++ch)
                    for (int i = 0; i < 128; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i)) { same = false; break; }
            }
            check (same, "deux instances neuves rendent le même signal, échantillon par échantillon");
        }

        // 7. Latence réelle = latence déclarée, mesurée sur impulsion (§4.3).
        if (lat1 > 0)
        {
            skill->prepare (sr, maxBlock); skill->reset();
            const int n = juce::jmax (4 * lat1, 1024);
            juce::AudioBuffer<float> b (2, n); b.clear();
            b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
            Curves c (*info, n, 0.5f);
            for (int off = 0; off < n; off += 128)
            {
                const int len = juce::jmin (128, n - off);
                juce::AudioBuffer<float> sub (b.getArrayOfWritePointers(), 2, off, len);
                Curves cs (*info, len, 0.5f);
                skill->process (sub, cs.curves, len);
            }
            // La latence se lit sur le PIC de la réponse, pas sur le premier échantillon
            // non nul : un module à phase linéaire (fenêtrage, grain) répand de l'énergie
            // avant son pic, et serait recalé à tort alors que sa déclaration est juste.
            // Le pic, lui, tombe à la latence déclarée pour les deux familles.
            int at = -1, peak = -1;
            float peakMag = 0.0f, energyBefore = 0.0f, energyTotal = 0.0f;
            const int early = juce::jmax (1, lat1 - juce::jmax (2, lat1 / 4));
            for (int i = 0; i < n; ++i)
            {
                const float m = std::abs (b.getSample (0, i));
                if (at < 0 && m > 1.0e-4f) at = i;
                if (m > peakMag) { peakMag = m; peak = i; }
                energyTotal += m * m;
                if (i < early) energyBefore += m * m;
            }
            const int tolerance = juce::jmax (2, lat1 / 8);
            check (peak >= 0 && std::abs (peak - lat1) <= tolerance,
                   "pic de la réponse à l'échantillon " + juce::String (peak) + ", latence déclarée " + juce::String (lat1)
                       + " (tolérance " + juce::String (tolerance) + ", première sortie à " + juce::String (at) + ")");
            check (energyTotal <= 0.0f || energyBefore <= 0.1f * energyTotal,
                   "rien d'important ne sort avant la latence déclarée (" + juce::String (100.0f * energyBefore / juce::jmax (1.0e-12f, energyTotal), 2) + " % de l'énergie)");
        }

        // 8. Les cas numériques de la skill elle-même (§3.9).
        {
            juce::String log;
            const bool ok = skill->selfTest (log);
            check (ok, "cas numériques de la skill");
            report << log;
        }

        // 9. Coût par bloc à 128 échantillons — le budget §4.4 se recalibre ici.
        {
            skill->prepare (sr, 128); skill->reset();
            juce::AudioBuffer<float> b (2, 128);
            juce::Random rng (3);
            Curves c (*info, 128, 0.5f);
            BlockTimer t;
            for (int i = 0; i < 2000 + blocks; ++i)
            {
                fillNoise (b, 128, rng);
                if (i == 2000) t.reset();
                t.begin();
                skill->process (b, c.curves, 128);
                t.end (128);
            }
            const auto s = t.compute();
            const double blockUs = 1e6 * 128.0 / sr;
            report << "    coût : moyenne " << juce::String (s.meanUs, 3) << " µs, p99 " << juce::String (s.p99Us, 3)
                   << ", p99.9 " << juce::String (s.p999Us, 3) << " µs (" << juce::String (100.0 * s.p999Us / blockUs, 3)
                   << " % du bloc), max " << juce::String (s.maxUs, 3) << " µs\n";
            check (s.p999Us < 0.25 * blockUs, "p99.9 seule dans un emplacement sous le budget du bloc");
        }
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray a;
    for (int i = 1; i < argc; ++i) a.add (argv[i]);
    auto arg = [&] (const juce::String& k, const juce::String& d) { const int i = a.indexOf (k); return (i >= 0 && i + 1 < a.size()) ? a[i + 1] : d; };

    const double sr = arg ("--rate", "48000").getDoubleValue();
    const int blocks = arg ("--blocks", "20000").getIntValue();
    const juce::String only = arg ("--only", "");

    registerAllSkills();
    report << "PlugSkillTest — contrat §3.9 — " << (int) sr << " Hz — " << juce::Time::getCurrentTime().toString (true, true) << "\n";

    auto ids = SkillRegistry::instance().ids();
    int checked = 0;
    for (const auto& id : ids)
    {
        if (id.startsWith ("factice.")) continue;          // les factices du J3 ne prétendent pas au contrat
        if (only.isNotEmpty() && id != only) continue;
        verify (id, sr, blocks);
        ++checked;
    }

    if (checked == 0) report << "\n[note] aucune skill à vérifier (registre vide hors factices)\n";
    report << "\nRésultat : " << (failures == 0 ? "TOUT PASSE" : juce::String (failures) + " ÉCHEC(S)")
           << " — " << checked << " skill(s) vérifiée(s)\n";

    std::printf ("%s", report.toRawUTF8());
    return failures == 0 ? 0 : 1;
}
