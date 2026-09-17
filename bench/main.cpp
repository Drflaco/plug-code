// PlugBench — banc J2 hors hôte.
// Vérifie : compte et identifiants de la grille, drapeaux non automatisables,
// aller-retour d'état, coût par bloc à 48 kHz / 128 du processeur à vide
// (latence et bypass : voir PlugRender depuis J3), et depuis le J4b étape 1 le
// piège APVTS/undo : une automation qui joue ne doit pas entrer dans l'historique
// d'annulation du pilote (ETAT Rév. 9, a) « Piège connu »).
// Usage : PlugBench [fichier_rapport] [nb_blocs]
// Code de retour 0 si tout passe.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PlugProcessor.h"
#include "StateSchema.h"
#include "ui/Presenter.h"
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

    // 2. Latence et bypass : déplacés au J3 dans PlugRender (test T6), le moteur porte la latence.

    //==========================================================================
    // 2 bis. Presets d'état exposés comme programmes (§3.6, décision pilote J4a).
    // L'épreuve qui compte : exposer des programmes ne doit AJOUTER AUCUN paramètre
    // à la grille figée, sinon les automations des projets existants se décalent.
    {
        plug::PlugProcessor p;
        const int n = p.getNumPrograms();
        check (p.getParameters().size() == plug::grid::kTotalCount,
               "presets exposés : la grille reste à " + juce::String (p.getParameters().size()) + " paramètres", log);
        log << "       " << n << " programme(s) :";
        for (int i = 0; i < juce::jmin (n, 8); ++i) log << " « " << p.getProgramName (i) << " »";
        log << "\n";

        // L'exposition en programmes est suspendue (voir PlugProcessor.h) : l'hôte
        // doit voir un seul programme, donc aucun paramètre « Program » ajouté.
        check (n == 1, "un seul programme annoncé : aucun paramètre « Program » ajouté par l'enveloppe VST3", log);
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
    // 4. Coût par bloc du processeur à vide (socle J3 sans skill) — référence du budget §4.4
    for (int lat : { 0 })
    {
        plug::PlugProcessor p;
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
    // 5. Undo et automation — le piège APVTS/undo (ETAT Rév. 9, a) « Piège connu »).
    //
    // Ce que fait Live : l'hôte écrit dans les paramètres pendant qu'un clip
    // automatisé joue ; l'APVTS recopie ces valeurs dans l'arbre à son propre timer,
    // AVEC l'UndoManager qu'on lui a donné. Si c'est celui du pilote, chaque valeur
    // automatisée devient une action annulable, et Ctrl+Z défait l'automation au
    // lieu de défaire le geste du pilote.
    //
    // Le flush est forcé ici par `copyState()`, qui appelle exactement la même
    // `flushParameterValuesToValueTree()` que le timer : `runDispatchLoopUntil` est
    // compilé hors du binaire par JUCE_MODAL_LOOPS_PERMITTED=0, et on ne relâche pas
    // une contrainte du produit pour une mesure. La confirmation dans Live (clip
    // automatisé qui joue, puis Ctrl+Z) reste au pilote.
    {
        plug::PlugProcessor p;
        auto& um = p.undoManager();
        auto& view = p.presenter();

        // a) Un geste du pilote : une transaction nommée.
        view.setTail (2, false);
        const juce::String userName = um.getUndoDescription();
        const int userActions = um.getNumActionsInCurrentTransaction();
        check (um.canUndo() && userName.isNotEmpty(),
               "undo : le geste du pilote ouvre une transaction nommée « " + userName + " » ("
                   + juce::String (userActions) + " action(s))", log);

        // b) L'hôte automatise : un clip qui joue, c'est-à-dire des valeurs qui arrivent
        //    en continu ET un flush toutes les 20 à 500 ms. On reproduit les deux :
        //    8 tours de 4 valeurs, un flush par tour. Hors de tout geste d'interface,
        //    hors de toute commande du Presenter.
        constexpr int kTicks = 8, kPerTick = 4;
        auto* automated = p.state().getParameter ("slot03.main");
        const float beforeAutomation = plug::state::readParam (p.stateTree(), "slot03.main");
        for (int t = 0; t < kTicks; ++t)
        {
            for (int i = 0; i < kPerTick; ++i)
                automated->setValueNotifyingHost (0.20f + 0.02f * (float) (t * kPerTick + i));
            p.state().copyState();                   // le flush, tel que le timer le ferait
        }
        const float afterAutomation = plug::state::readParam (p.stateTree(), "slot03.main");

        const int afterActions = um.getNumActionsInCurrentTransaction();
        const int polluted = afterActions - userActions;

        log << "\nUndo et automation (J4b étape 1) :\n"
            << "  transaction du pilote : « " << userName << " », " << userActions << " action(s)\n"
            << "  " << (kTicks * kPerTick) << " valeurs automatisées sur slot03.main en " << kTicks
            << " flush : " << juce::String (beforeAutomation, 3) << " vers " << juce::String (afterAutomation, 3) << "\n"
            << "  actions ajoutées à la transaction par l'automation : " << polluted << "\n"
            << "  transaction à annuler après l'automation : « " << um.getUndoDescription() << " »\n";

        check (std::abs (afterAutomation - 0.82f) < 1.0e-2f,
               "undo : l'automation atteint bien l'arbre (la vue la voit) — " + juce::String (afterAutomation, 3), log);

        // c) L'épreuve : Ctrl+Z doit défaire le geste du pilote, pas l'automation.
        um.undo();
        const bool tailRestored = plug::state::slot (p.stateTree(), 2)
                                      .getProperty (plug::state::id::tail, "ring").toString() == "ring";
        const float afterUndo = plug::state::readParam (p.stateTree(), "slot03.main");
        const bool automationUndone = std::abs (afterUndo - afterAutomation) > 1.0e-4f;

        log << "  après Ctrl+Z : geste du pilote rétabli = " << (tailRestored ? "oui" : "NON")
            << ", automation défaite = " << (automationUndone ? "OUI" : "non")
            << " (slot03.main = " << juce::String (afterUndo, 3) << ")\n";

        check (polluted == 0, "undo : l'automation n'ajoute AUCUNE action à la transaction du pilote ("
                                  + juce::String (polluted) + ")", log);
        check (tailRestored, "undo : Ctrl+Z rétablit le geste du pilote", log);
        check (! automationUndone, "undo : Ctrl+Z ne défait pas l'automation de l'hôte", log);

        // d) La contrepartie que la parade ne doit PAS coûter : un réglage venu de
        //    l'interface reste annulable, et l'hôte le voit passer.
        const float knobBefore = plug::state::readParam (p.stateTree(), "slot04.main");
        view.setParam ("slot04.main", 0.80f);
        const float knobSet = plug::state::readParam (p.stateTree(), "slot04.main");
        const float hostSees = p.state().getParameter ("slot04.main")->getValue();
        um.undo();
        const float knobBack = plug::state::readParam (p.stateTree(), "slot04.main");

        log << "  réglage d'interface slot04.main : " << juce::String (knobBefore, 3) << " vers "
            << juce::String (knobSet, 3) << " (paramètre hôte " << juce::String (hostSees, 3)
            << "), après Ctrl+Z " << juce::String (knobBack, 3) << "\n";

        check (std::abs (knobSet - 0.80f) < 1.0e-6f && std::abs (hostSees - 0.80f) < 1.0e-6f,
               "undo : un réglage d'interface atteint l'arbre ET le paramètre exposé à l'hôte", log);
        check (std::abs (knobBack - knobBefore) < 1.0e-6f,
               "undo : un réglage d'interface reste annulable (retour à " + juce::String (knobBack, 3) + ")", log);
    }

    //==========================================================================
    log << "\nRésultat : " << (failures == 0 ? "TOUT PASSE" : juce::String (failures) + " ÉCHEC(S)") << "\n";

    std::printf ("%s", log.toRawUTF8());
    if (reportFile != juce::File())
        reportFile.replaceWithText (log);

    return failures == 0 ? 0 : 1;
}
