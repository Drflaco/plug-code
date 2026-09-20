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
#include "BlockTimer.h"
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
    // 6. Vue des emplacements et encodage — deux défauts vus dans Live le 17/09.
    // L'indicateur d'activité des onglets alternait plein / creux sur un état par
    // DÉFAUT, où les seize slotNN.active valent 1 ; et tous les accents sortaient en
    // mojibake. Ces cas lisent exactement ce que la vue lit, et rien d'autre.
    {
        plug::PlugProcessor p;
        auto& view = p.presenter();

        int actifs = 0;
        juce::String creux;
        for (int s = 1; s <= 16; ++s)
            if (view.slotView (s).active) ++actifs; else creux << " " << s;
        check (actifs == 16, "vue : les 16 emplacements sont actifs sur l'état par défaut ("
                                 + juce::String (actifs) + "/16"
                                 + (creux.isEmpty() ? juce::String() : ", creux :" + creux) + ")", log);

        // La même lecture, mais brute : si les deux divergent, le défaut est dans la
        // vue ; si elles concordent, il est dans l'état ou dans la grille.
        int bruts = 0;
        for (int s = 1; s <= 16; ++s)
            if (plug::state::readParam (p.stateTree(), "slot" + juce::String (s).paddedLeft ('0', 2) + ".active") >= 0.5f)
                ++bruts;
        check (bruts == 16, "état : les 16 PARAM slotNN.active valent 1 (" + juce::String (bruts) + "/16)", log);

        // ENCODAGE : un libellé accentué déclaré par une skill doit ressortir INTACT de
        // la couche de présentation, pas en « DÃ©calage stÃ©rÃ©o ».
        view.setSkill (1, "core.fm");
        const auto fm = view.slotView (1);
        const auto& stereo = fm.params[9];          // grid::kModulable[9] = « stereo »
        check (stereo.label == juce::String::fromUTF8 ("Décalage stéréo") && stereo.label.length() == 15,
               "encodage : « " + stereo.label + " » — " + juce::String (stereo.label.length())
                   + " points de code, attendu 15", log);
        check (fm.params[2].valueText.isNotEmpty() && fm.params[2].unit.isEmpty(),
               "lisibilité : le Rapport de core.fm s'affiche « " + fm.params[2].valueText + " »", log);
        log << "       core.fm : Profondeur « " << fm.params[0].valueText
            << " », Fréquence « " << fm.params[1].valueText << " »\n";

        // Correction 1 de la phase 2 (18/09) : core.filter ne déclare que main, paramA,
        // paramB et stereo. paramC..F et res1..3 doivent ressortir INERTES et au tiret ;
        // mix et gain jamais — le socle les compose toujours.
        view.setSkill (2, "core.filter");
        const auto filtre = view.slotView (2);
        int inertes = 0, actifs2 = 0;
        juce::String faux;
        for (int m = 0; m < plug::ui::kSlotParams; ++m)
        {
            const auto& q = filtre.params[(size_t) m];
            const bool attendu = (m >= 3 && m <= 6) || m >= 10;   // paramC..F, res1..3
            if (q.inert) ++inertes; else ++actifs2;
            if (q.inert != attendu || (attendu && q.label != juce::String::fromUTF8 ("—")))
                faux << " " << q.name;
        }
        check (faux.isEmpty() && inertes == 7 && actifs2 == 6,
               "vue : core.filter — 7 entrées non déclarées inertes au tiret, 6 actives ("
                   + juce::String (inertes) + " inertes" + (faux.isEmpty() ? juce::String() : ", faux :" + faux) + ")", log);

        // Correction 3 de la phase 2 (18/09) : les réglages de la grille passent par le
        // Presenter, en unités lisibles, et reviennent tels quels dans les deux vues
        // (SequencerView et LineView::length), avec une transaction nommée.
        view.setSeqLength (32);
        view.setSeqDivision (0);
        const auto sq = view.sequencerView();
        check (sq.length == 32 && view.lineView (1).length == 32 && sq.division == 0
                   && sq.divisionChoices.size() == 15 && sq.divisionChoices[0] == "1/4" && sq.divisionChoices[14] == "1/64D"
                   && view.undoView().undoName == juce::String::fromUTF8 ("Division 1/4"),
               "séquenceur : longueur 32 et division 1/4 posées et relues (" + juce::String (sq.length) + " pas, "
                   + sq.divisionChoices[sq.division] + ", annuler « " + view.undoView().undoName + " »)", log);
        view.setSeqLength (16);
        view.setSeqDivision (6);

        // Correction 4 de la phase 2 (geste A, 18/09) : une valeur explicite sur les pas
        // 5 à 12 de core.filter, qui passent en mode figé ; le pas 4 ne bouge pas ; la
        // transaction porte le libellé de la skill, sa valeur affichée et le compte.
        view.setStepsExplicit (2, 5, 12, "main", 0.25f);
        const auto counts = view.stepCountsView (2, 5, 12);
        const auto ligne = view.lineView (2);
        const bool figes = counts.explicitCount == 8 && counts.generated == 0
                        && ligne.steps[4].mode == plug::ui::StepModeView::Explicit
                        && ligne.steps[11].mode == plug::ui::StepModeView::Explicit
                        && ligne.steps[3].mode == plug::ui::StepModeView::Base
                        && std::abs (ligne.steps[4].value - 0.25f) < 1e-6f;
        check (figes && view.undoView().undoName.endsWith (juce::String::fromUTF8 ("sur 8 pas"))
                     && view.undoView().undoName.startsWith (juce::String::fromUTF8 ("Coupure à ")),
               "geste A : 8 pas figés à une même valeur, transaction « " + view.undoView().undoName + " »", log);

        // Un geste continu sur la sélection = UNE transaction, nommée par la dernière
        // valeur ; un seul Ctrl+Z la défait, un second rend la ligne à son mode de base.
        view.beginStepsGesture (2, 5, 12, "main");
        view.setStepsExplicit (2, 5, 12, "main", 0.5f);
        view.setStepsExplicit (2, 5, 12, "main", 0.75f);
        view.endStepsGesture();
        const auto nomGeste = view.undoView().undoName;
        view.undo();   // défait le geste entier
        const bool retour025 = std::abs (view.lineView (2).steps[4].value - 0.25f) < 1e-6f;
        view.undo();   // défait la pose à 0,25
        check (retour025 && view.stepCountsView (2, 5, 12).explicitCount == 0
                   && nomGeste.startsWith (juce::String::fromUTF8 ("Coupure à ")) && nomGeste.endsWith (juce::String::fromUTF8 ("sur 8 pas")),
               "geste A : un mouvement = une transaction « " + nomGeste + " », deux Ctrl+Z rendent la ligne ("
                   + juce::String (view.stepCountsView (2, 5, 12).explicitCount) + " figé(s))", log);

        // Point 6a (18/09) : montrer un paramètre = la ligne passe en mode B sur lui, et
        // l'indice de couleur suit ; chaque ParamView porte son indice 0..12.
        view.showParam (2, "paramA");
        const auto lb = view.lineView (2);
        check (lb.modeB && lb.shownParam == "paramA" && lb.shownIndex == 1
                   && filtre.params[1].index == 1 && filtre.params[9].index == 9,
               "lien 6a : showParam (paramA) → mode B, paramètre montré « " + lb.shownParam + " », indice "
                   + juce::String (lb.shownIndex), log);

        // Correction 5 (18/09) : la ligne comme motif. On génère, on fige une plage, on
        // éteint un pas, on resserre une plage ; on enregistre ; on reset ; on recharge.
        // Le motif revient à l'identique, la skill n'a pas bougé, chaque étape est nommée.
        view.generate (2, 1, 32, 0.6f);
        view.setStepsExplicit (2, 3, 6, "main", 0.9f);
        view.setStepOn (2, 8, false);
        view.setRange (2, "paramA", 0.2f, 0.7f);
        const auto avant = view.lineView (2);
        const auto plageAvant = view.slotView (2).params[1];
        const auto fichier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("plug_bench_ligne.seqline");
        const bool ecrit = view.saveLine (2, fichier) && fichier.existsAsFile();

        view.resetLine (2);
        const auto apresReset = view.stepCountsView (2, 1, 32);
        const auto ligneReset = view.lineView (2);
        const bool neutre = apresReset.generated == 0 && apresReset.explicitCount == 0 && apresReset.off == 0
                         && ligneReset.steps[7].on
                         && std::abs (view.slotView (2).params[1].min) < 1e-6f && std::abs (view.slotView (2).params[1].max - 1.0f) < 1e-6f;
        const auto nomReset = view.undoView().undoName;
        check (ecrit && neutre && nomReset == juce::String::fromUTF8 ("Reset séquence Filtre") && view.slotView (2).skillId == "core.filter",
               "séquence : enregistrée (" + juce::String ((int) fichier.getSize()) + " octets), puis « " + nomReset
                   + " » : 32 pas actifs en mode base, plages à 0–1, l'effet toujours en place", log);

        const bool lu = view.loadLine (2, fichier);
        const auto apres = view.lineView (2);
        const auto plageApres = view.slotView (2).params[1];
        bool identique = lu;
        for (int i = 0; i < 32 && identique; ++i)
            identique = apres.steps[(size_t) i].on == avant.steps[(size_t) i].on
                     && apres.steps[(size_t) i].mode == avant.steps[(size_t) i].mode
                     && std::abs (apres.steps[(size_t) i].value - avant.steps[(size_t) i].value) < 1e-6f;
        check (identique && std::abs (plageApres.min - plageAvant.min) < 1e-6f && std::abs (plageApres.max - plageAvant.max) < 1e-6f
                   && view.undoView().undoName == juce::String::fromUTF8 ("Charger séquence Filtre")
                   && view.slotView (2).skillId == "core.filter"
                   && view.defaultLineFile (2).getFileName() == "SEQ_FILTRE_2.seqline",
               "séquence : rechargée à l'identique (32 pas, modes, valeurs, plage 0,2–0,7), « " + view.undoView().undoName
                   + " », skill inchangée, nom par défaut " + view.defaultLineFile (2).getFileName(), log);
        fichier.deleteFile();

        // Amendement J3-6 (pilote, 18/09) : le verrou PROTÈGE. Résonance générée sur 1–32,
        // puis verrouillée : ce que la ligne montre ne bouge pas d'un pas ; une nouvelle
        // génération ne la touche pas ; déverrouillée, elle ne bouge toujours pas.
        view.generate (2, 1, 32, 1.0f);
        view.setShownParam (2, "paramA");
        const auto genere = view.lineView (2);
        view.setLocked (2, "paramA", true);
        const auto verrou = view.lineView (2);
        view.generate (2, 1, 32, 1.0f);
        const auto regenere = view.lineView (2);
        view.setLocked (2, "paramA", false);
        const auto libere = view.lineView (2);
        bool protege = true;
        int pasAvecValeur = 0;
        for (int i = 0; i < 32; ++i)
        {
            const float g = genere.steps[(size_t) i].value;
            protege = protege && std::abs (verrou.steps[(size_t) i].value - g) < 1e-6f
                              && std::abs (regenere.steps[(size_t) i].value - g) < 1e-6f
                              && std::abs (libere.steps[(size_t) i].value - g) < 1e-6f;
            if (genere.steps[(size_t) i].hasValue) ++pasAvecValeur;
        }
        check (protege && pasAvecValeur == 32,
               "verrou J3-6 : Résonance verrouillée après génération garde ses 32 valeurs à travers une nouvelle génération et le déverrouillage ("
                   + juce::String (pasAvecValeur) + " pas à valeur)", log);

        // 6b (18/09) : dessiner = un geste sur des pas UN PAR UN, nommé à la fin ; poser une
        // valeur sur un pas généré garde ce que ses autres paramètres jouaient ; la case
        // survolée se lit par slotViewAt sans toucher la sélection.
        view.setLocked (2, "paramA", false);
        view.generate (2, 1, 32, 1.0f);
        const float resAvant = view.lineView (2).steps[4].value;           // Résonance montrée, pas 5
        view.beginStepsGesture (2, 5, 5, "main");
        view.setStepsExplicit (2, 5, 5, "main", 0.1f);
        view.setStepsExplicit (2, 6, 6, "main", 0.2f);
        view.setStepsExplicit (2, 7, 7, "main", 0.3f);
        view.endStepsGesture (juce::String::fromUTF8 ("Coupure dessinée · pas 5–7"));
        const auto dessin = view.slotViewAt (2, 6);
        view.selectSteps (9, 9);
        view.setHover (2, 6);
        check (view.undoView().undoName == juce::String::fromUTF8 ("Coupure dessinée · pas 5–7")
                   && std::abs (dessin.params[0].raw - 0.2f) < 1e-6f
                   && std::abs (view.lineView (2).steps[4].value - resAvant) < 1e-6f
                   && view.hoverSlot() == 2 && view.hoverStep() == 6
                   && view.firstSelectedStep() == 9 && std::abs (view.slotView (2).params[0].raw - 0.2f) > 1e-3f,
               "dessin 6b : « " + view.undoView().undoName + " », pas 6 lu au survol à " + juce::String (dessin.params[0].raw, 2)
                   + ", Résonance du pas 5 intacte, sélection inchangée (pas 9)", log);
        view.setHover (0, 0);

        // Phase 3 (20/09) : le Dry / Wet général d'un emplacement — surcouche hors
        // grille. Un geste = une transaction renommée à la dernière valeur ; Ctrl+Z
        // la défait entière ; la grille n'a pas bougé (aucun PARAM ajouté).
        const float mixAvant = view.slotView (2).params[7].base;
        view.beginWetGesture (2);
        view.setWet (2, 0.6f);
        view.setWet (2, 0.4f);
        view.endWetGesture();
        const auto wetView = view.slotView (2);
        const auto wetName = view.undoView().undoName;
        view.undo();
        check (std::abs (wetView.wet - 0.4f) < 1e-6f
                   && wetName == juce::String::fromUTF8 ("Dry / Wet 40 % · emplacement 2")
                   && std::abs (wetView.params[7].base - mixAvant) < 1e-6f
                   && std::abs (view.slotView (2).wet - 1.0f) < 1e-6f
                   && p.getParameters().size() == 284,
               "Dry / Wet général : geste de deux valeurs = une transaction « " + wetName
                   + " », le mix de l'emplacement intact, un Ctrl+Z rend 100 %, grille toujours à 284", log);

        // L'amortissement (phase 3) : même logement, même discipline. Le texte en ms vient
        // de la couche de présentation (grid::dampSeconds), jamais du widget.
        view.beginDampGesture (2);
        view.setDamp (2, 0.9f);
        view.setDamp (2, 0.5f);
        view.endDampGesture();
        const auto dampView = view.slotView (2);
        const auto dampName = view.undoView().undoName;
        view.undo();
        check (std::abs (dampView.damp - 0.5f) < 1e-6f
                   && dampName == juce::String::fromUTF8 ("Amortissement 63 ms · emplacement 2")   // 0,25 × 0,5² = 62,5 ms, arrondi
                   && dampView.dampText == juce::String::fromUTF8 ("63 ms")
                   && std::abs (view.slotView (2).damp) < 1e-6f
                   && p.getParameters().size() == 284,
               "Amortissement : geste de deux valeurs = une transaction « " + dampName
                   + " », texte « " + dampView.dampText + " », un Ctrl+Z rend 0, grille toujours à 284", log);
    }

    //==========================================================================
    // 7. Rendu du séquenceur (J4b étape 4 ; règle du pilote : aucune allocation par
    // frame). On ne peut pas faire tourner l'éditeur à 60 Hz sans hôte : on force donc
    // 600 paint() sur une Image, ce qui exécute exactement le même code de dessin sans
    // le compositeur de Windows. C'est un plancher, pas une promesse d'affichage — la
    // mesure dans Live reste au pilote.
    {
        plug::PlugProcessor p;
        auto& view = p.presenter();
        view.setSkill (1, "core.fm");
        view.setSkill (2, "core.delay");
        view.generate (1, 1, 32, 0.7f);              // un motif réel, pas une grille vide
        view.setLineModeB (1, true);                 // le mode qui dessine des barres

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        if (ed == nullptr)
        {
            log << "\n[note] rendu : createEditor() n'a rien rendu\n";
        }
        else
        {
            ed->setSize (1280, 800);
            juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);

            plug::BlockTimer t;
            for (int i = 0; i < 700; ++i)
            {
                if (i == 100) t.reset();             // 100 tours de chauffe, 600 mesurés
                juce::Graphics g (img);
                t.begin();
                ed->paintEntireComponent (g, true);
                t.end (1);
            }
            const auto s = t.compute();
            log << "\nRendu de l'éditeur (600 paint() forcés sur une Image 1280x800) :\n"
                << "  moyenne " << juce::String (s.meanUs, 1) << " us\n"
                << "  p50     " << juce::String (s.p50Us, 1) << " us\n"
                << "  p99     " << juce::String (s.p99Us, 1) << " us\n"
                << "  max     " << juce::String (s.maxUs, 1) << " us\n";

            // Diagnostic visuel hors hôte (méthode qui a trouvé le « caret fantôme » le
            // 18/09) : si PLUG_EDITOR_PNG nomme un fichier, la dernière image rendue y
            // est écrite. L'agent constructeur ne voit jamais Live ; ceci est son œil.
            if (const auto png = juce::SystemStats::getEnvironmentVariable ("PLUG_EDITOR_PNG", {}); png.isNotEmpty())
            {
                const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (png);
                f.deleteFile();
                juce::FileOutputStream os (f);
                if (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os))
                    log << "  image : " << f.getFullPathName() << "\n";
            }

            // 16,7 ms = une frame à 60 Hz. Ici on redessine TOUTE l'interface à chaque
            // tour, là où l'affichage réel ne repeint que ce qui a changé : tenir sous
            // la moitié d'une frame dans ce cas défavorable est la marge visée.
            check (s.p99Us < 8000.0, "rendu : p99 de l'éditeur entier sous 8 ms ("
                                         + juce::String (s.p99Us, 1) + " us)", log);
        }

        // Balayage de sélection : ce que coûte UN mouvement de souris dans le séquenceur.
        // Le glisser rappelle refresh(), donc relit toutes les Views — pas une fois par
        // frame, une fois par événement. On mesure 100 événements, et on ne « corrige »
        // rien sans ce chiffre (REGIME §2 : mesurer, jamais supposer).
        {
            plug::BlockTimer t;
            for (int i = 0; i < 200; ++i)
            {
                const int last = 1 + (i % 32);
                if (i == 100) t.reset();
                t.begin();
                view.selectSteps (1, last);
                for (int s = 1; s <= 10; ++s) { view.slotView (s); view.lineView (s); }
                view.stepCountsView (1, 1, last);
                t.end (1);
            }
            const auto s = t.compute();
            log << "\nBalayage de sélection (100 événements, 10 lignes relues à chaque fois) :\n"
                << "  moyenne " << juce::String (s.meanUs, 1) << " us\n"
                << "  p99     " << juce::String (s.p99Us, 1) << " us\n"
                << "  max     " << juce::String (s.maxUs, 1) << " us\n";
            check (s.p99Us < 4000.0, "balayage : p99 d'un événement de glisser sous 4 ms ("
                                         + juce::String (s.p99Us, 1) + " us)", log);
        }

        // Le même balayage par le CHEMIN LÉGER, celui que l'interface emprunte depuis
        // que la sélection ne marque plus l'état : deux Views au lieu de vingt.
        {
            plug::BlockTimer t;
            for (int i = 0; i < 200; ++i)
            {
                const int last = 1 + (i % 32);
                if (i == 100) t.reset();
                t.begin();
                view.selectSteps (1, last);
                view.slotView (1);                 // les contrôles et l'inspecteur, rien d'autre
                view.stepCountsView (1, 1, last);
                t.end (1);
            }
            const auto s = t.compute();
            log << "\nBalayage de sélection, chemin léger (100 événements) :\n"
                << "  moyenne " << juce::String (s.meanUs, 1) << " us\n"
                << "  p99     " << juce::String (s.p99Us, 1) << " us\n";
            check (s.p99Us < 700.0, "balayage léger : p99 sous 0,7 ms (" + juce::String (s.p99Us, 1) + " us)", log);
        }
    }

    //==========================================================================
    // 8. Préférences HORS état (§3.11, J4b étape 6). Un preset qui embarquerait le zoom
    // rendrait un projet dépendant de l'écran sur lequel il a été fait. On touche aux
    // six préférences et on vérifie que l'état sérialisé ne bouge pas d'UN OCTET —
    // c'est la seule preuve qui vaille, l'intention ne suffit pas.
    {
        plug::PlugProcessor p;
        auto& view = p.presenter();

        juce::MemoryBlock before;
        p.getStateInformation (before);

        const auto initial = view.prefsView();
        view.toggleRatio();
        view.setPrefZoom (1.25);
        view.setPrefHoverHelp (! initial.hoverHelp);
        view.setPrefHelpDelayMs (1234);
        view.setPrefDefaultMixLaw (2);
        view.setPrefShownSlots (16);

        juce::MemoryBlock after;
        p.getStateInformation (after);

        const auto now = view.prefsView();
        check (now.zoom > 1.2 && now.helpDelayMs == 1234 && now.shownSlots == 16,
               "préférences : les six réglages sont bien pris (" + juce::String (now.helpDelayMs)
                   + " ms, " + juce::String (now.shownSlots) + " emplacements)", log);
        check (before == after, "préférences : PlugState n'a gagné AUCUN octet ("
                                    + juce::String ((int) before.getSize()) + " octets avant et après)", log);

        // Et le contenu du panneau : le fichier embarqué, et le catalogue réel.
        check (view.avenirText().contains (juce::String::fromUTF8 ("À venir")),
               "à propos : AVENIR.md est embarqué et lisible", log);
        const auto about = view.aboutView();
        int withLatency = 0;
        for (const auto& s : about.skills) if (s.latencyKnown) ++withLatency;
        check ((int) about.skills.size() == 9 && withLatency == 9,
               "à propos : 9 effets au catalogue, latence connue pour " + juce::String (withLatency), log);
        check (view.masterView().lawChoices.size() == 3,
               "à propos : les libellés de choix viennent de la grille ("
                   + view.masterView().lawChoices.joinIntoString (", ") + ")", log);

        view.setPrefShownSlots (initial.shownSlots);   // on remet la machine du pilote en l'état
        view.setPrefZoom (initial.zoom);
        view.setPrefHoverHelp (initial.hoverHelp);
        view.setPrefHelpDelayMs (initial.helpDelayMs);
        view.setPrefDefaultMixLaw (initial.defaultMixLaw);
        if (view.prefsView().ratioTwoThirds != initial.ratioTwoThirds) view.toggleRatio();
    }



    //==========================================================================
    log << "\nRésultat : " << (failures == 0 ? "TOUT PASSE" : juce::String (failures) + " ÉCHEC(S)") << "\n";

    std::printf ("%s", log.toRawUTF8());
    if (reportFile != juce::File())
        reportFile.replaceWithText (log);

    return failures == 0 ? 0 : 1;
}
