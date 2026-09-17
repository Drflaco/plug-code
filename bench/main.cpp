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
