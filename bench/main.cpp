// PlugBench — banc J2 hors hôte.
// Vérifie : compte et identifiants de la grille, drapeaux non automatisables,
// latence déclarée = latence réelle (0 et 256), bypass aligné, aller-retour
// d'état, coût par bloc à 48 kHz / 128 sur le passe-tout.
// Usage : PlugBench [fichier_rapport] [nb_blocs]
// Code de retour 0 si tout passe.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PlugProcessor.h"
#include <cstdio>
#include <set>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what, juce::String& log)
    {
        log << (ok ? "[OK]   " : "[FAIL] ") << what << "\n";
        if (! ok) ++failures;
    }

    // Renvoie l'index absolu du premier échantillon non nul en sortie après une impulsion à t=0.
    int measureImpulseDelay (plug::PlugProcessor& p, int blockSize, bool bypassed)
    {
        const int maxBlocks = 1024;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;

        for (int b = 0; b < maxBlocks; ++b)
        {
            buf.clear();
            if (b == 0)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, 0, 1.0f);

            if (bypassed) p.processBlockBypassed (buf, midi);
            else          p.processBlock (buf, midi);

            for (int i = 0; i < blockSize; ++i)
                if (std::abs (buf.getSample (0, i)) > 0.0f)
                    return b * blockSize + i;
        }
        return -1;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File reportFile = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    const int benchBlocks = argc > 2 ? juce::String (argv[2]).getIntValue() : 200000;

    juce::String log;
    log << "PlugBench — J2 — " << juce::Time::getCurrentTime().toString (true, true) << "\n\n";

    //==========================================================================
    // 1. Grille
    {
        plug::PlugProcessor p;
        const auto ids = plug::grid::allIds();
        const auto& params = p.getParameters();

        check ((int) ids.size() == plug::grid::kTotalCount, "grille : " + juce::String ((int) ids.size()) + " identifiants attendus " + juce::String (plug::grid::kTotalCount), log);
        check (params.size() == plug::grid::kTotalCount, "processeur : " + juce::String (params.size()) + " paramètres exposés", log);

        int mismatches = 0, nonAuto = 0;
        for (int i = 0; i < juce::jmin (params.size(), (int) ids.size()); ++i)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
            if (rp == nullptr || rp->paramID != ids[(size_t) i]) ++mismatches;
            if (rp != nullptr && ! rp->isAutomatable()) ++nonAuto;
        }
        check (mismatches == 0, "ordre et identifiants conformes (" + juce::String (mismatches) + " écarts)", log);
        check (nonAuto == 2, "non automatisables : " + juce::String (nonAuto) + " (attendu 2 : master.mixLaw, master.quality)", log);

        // Unicité des identifiants VST3 (hash JUCE des chaînes).
        std::set<juce::uint32> hashes;
        for (const auto& id : ids)
            hashes.insert ((juce::uint32) id.hashCode());
        check ((int) hashes.size() == plug::grid::kTotalCount, "identifiants sans collision de hash", log);

        log << "\nGrille (" << (int) ids.size() << ") :\n";
        for (const auto& id : ids) log << "  " << id << "\n";
        log << "\n";
    }

    //==========================================================================
    // 2. Latence et bypass
    for (int lat : { 0, 256 })
    {
        plug::PlugProcessor p;
        p.setTestLatency (lat);
        p.prepareToPlay (48000.0, 128);

        check (p.getLatencySamples() == lat, "latence déclarée = " + juce::String (p.getLatencySamples()) + " (attendu " + juce::String (lat) + ")", log);

        const int d = measureImpulseDelay (p, 128, false);
        check (d == lat, "latence réelle (processBlock) = " + juce::String (d), log);

        plug::PlugProcessor pb;
        pb.setTestLatency (lat);
        pb.prepareToPlay (48000.0, 128);
        const int db = measureImpulseDelay (pb, 128, true);
        check (db == lat, "latence réelle (processBlockBypassed) = " + juce::String (db) + " — bypass aligné", log);

        // Blocs irréguliers : le retard reste exact avec des tailles 1, 7, 128, 500, 0.
        plug::PlugProcessor pi;
        pi.setTestLatency (lat);
        pi.prepareToPlay (48000.0, 512);
        {
            const int sizes[] = { 1, 7, 128, 500, 0, 64 };
            juce::MidiBuffer midi;
            int absIndex = 0, found = -1;
            bool first = true;
            for (int round = 0; round < 50 && found < 0; ++round)
                for (int sz : sizes)
                {
                    juce::AudioBuffer<float> buf (2, juce::jmax (sz, 1));
                    buf.clear();
                    buf.setSize (2, sz, false, false, true);
                    if (first && sz > 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); first = false; }
                    pi.processBlock (buf, midi);
                    for (int i = 0; i < sz && found < 0; ++i)
                        if (std::abs (buf.getSample (0, i)) > 0.0f) found = absIndex + i;
                    absIndex += sz;
                    if (found >= 0) break;
                }
            check (found == lat, "latence réelle avec blocs irréguliers = " + juce::String (found), log);
        }
        p.releaseResources(); pb.releaseResources(); pi.releaseResources();
    }

    //==========================================================================
    // 3. État : aller-retour
    {
        plug::PlugProcessor a;
        auto* pA = a.state().getParameter ("slot03.main");
        auto* pB = a.state().getParameter ("master.quality");
        pA->setValueNotifyingHost (0.25f);
        pB->setValueNotifyingHost (1.0f);

        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        plug::PlugProcessor b;
        b.setStateInformation (blob.getData(), (int) blob.getSize());
        const float vA = b.state().getParameter ("slot03.main")->getValue();
        const float vB = b.state().getParameter ("master.quality")->getValue();
        check (std::abs (vA - 0.25f) < 1e-6f, "état : slot03.main restauré à " + juce::String (vA), log);
        check (std::abs (vB - 1.0f) < 1e-6f, "état : master.quality restauré à " + juce::String (vB), log);
        check (blob.getSize() > 0, "état : " + juce::String ((int) blob.getSize()) + " octets", log);
    }

    //==========================================================================
    // 4. Coût par bloc — référence zéro du budget §4.4
    for (int lat : { 0, 256 })
    {
        plug::PlugProcessor p;
        p.setTestLatency (lat);
        p.prepareToPlay (48000.0, 128);

        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        juce::Random rng (42);

        plug::BlockTimer t;
        for (int b = 0; b < 2000 + benchBlocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

            if (b == 2000) t.reset();
            t.begin();
            p.processBlock (buf, midi);
            t.end (128);
        }

        const auto s = t.compute();
        const double blockUs = 1e6 * 128.0 / 48000.0;
        const double budgetUs = blockUs * 0.25;

        log << "\nCoût par bloc (48 kHz / 128, latence " << lat << ", " << (juce::int64) s.count << " blocs mesurés) :\n"
            << "  bloc = " << juce::String (blockUs, 2) << " µs, budget 25 % = " << juce::String (budgetUs, 2) << " µs\n"
            << "  moyenne " << juce::String (s.meanUs, 3) << " µs\n"
            << "  p50     " << juce::String (s.p50Us, 3) << " µs\n"
            << "  p99     " << juce::String (s.p99Us, 3) << " µs\n"
            << "  p99.9   " << juce::String (s.p999Us, 3) << " µs  (" << juce::String (100.0 * s.p999Us / blockUs, 4) << " % du bloc)\n"
            << "  max     " << juce::String (s.maxUs, 3) << " µs  (" << juce::String (100.0 * s.maxUs / blockUs, 4) << " % du bloc)\n";

        check (s.p999Us < budgetUs, "p99.9 sous le budget", log);
        p.releaseResources();
    }

    //==========================================================================
    log << "\nRésultat : " << (failures == 0 ? "TOUT PASSE" : juce::String (failures) + " ÉCHEC(S)") << "\n";

    std::printf ("%s", log.toRawUTF8());
    if (reportFile != juce::File())
        reportFile.replaceWithText (log);

    return failures == 0 ? 0 : 1;
}
