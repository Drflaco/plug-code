#include "Presenter.h"
#include "Format.h"
#include "../BuildStamp.h"
#include "../Engine.h"
#include "../GridMap.h"
#include "../PlugProcessor.h"
#include "../Skill.h"
#include "../StateEdit.h"
#include "../StateQuery.h"
#include "../StateSchema.h"
#include "BinaryData.h"
#include <cmath>

namespace plug::ui
{
    using juce::String;
    using namespace plug::ui::literals;   // "..."_fr : l'unique porte UTF-8 (ViewTypes.h)
    using juce::ValueTree;

    namespace
    {
        String slotGridId (int slot1, const String& suffix)
        {
            return "slot" + String (slot1).paddedLeft ('0', 2) + "." + suffix;
        }

        StepModeView modeView (StepMode m) noexcept
        {
            switch (m)
            {
                case StepMode::Generated: return StepModeView::Generated;
                case StepMode::Explicit:  return StepModeView::Explicit;
                case StepMode::Base:      break;
            }
            return StepModeView::Base;
        }

        // Les libellés d'une entrée à choix, tels que la grille les déclare à l'hôte.
        // Une seule vérité : ParameterGrid.cpp. Le Presenter la fait traverser.
        juce::StringArray choicesOf (juce::AudioProcessorValueTreeState& apvts, const String& gridId)
        {
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (gridId)))
                return c->choices;
            return {};
        }

        const SkillParamDecl* declFor (const SkillInfo* info, int modulable) noexcept
        {
            if (info == nullptr) return nullptr;
            for (const auto& d : info->params)
                if (d.modulable == modulable) return &d;
            return nullptr;
        }

        // Le libellé d'un paramètre pour un nom de transaction : celui de la skill en
        // place, sinon le générique. Le pilote lit son geste dans l'historique, pas
        // « setProperty », et surtout jamais « slot03.paramB ».
        String labelOf (const ValueTree& s, int slot1, const String& name)
        {
            auto sl = state::slot (s, slot1);
            const auto id = sl.getProperty (state::id::skill, "").toString();
            if (const auto* info = SkillRegistry::instance().info (id))
                for (const auto& d : info->params)
                    if (name == String (grid::kModulableName[(size_t) d.modulable]))
                        return fr (d.label);
            return Format::genericLabel (name);
        }

        // Le texte d'une valeur brute pour un paramètre d'emplacement : display + unité
        // déclarés par la skill, ou le repli 0,00–1,00 (même composition que slotView).
        String valueTextOf (const ValueTree& s, int slot1, const String& name, float raw)
        {
            auto sl = state::slot (s, slot1);
            const auto id = sl.getProperty (state::id::skill, "").toString();
            if (const auto* info = SkillRegistry::instance().info (id))
                for (const auto& d : info->params)
                    if (name == String (grid::kModulableName[(size_t) d.modulable]))
                        return Format::valueText (raw, d.display != nullptr ? d.display (raw) : String(), fr (d.unit));
            return Format::valueText (raw, String(), String());
        }
    }

    //==========================================================================
    Presenter::Command::Command (Presenter& p, const String& name) : presenter (p)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        ++presenter.busy;
        ++presenter.editing;      // c'est le pilote : l'état va cesser de décrire le preset
        presenter.proc.undoManager().beginNewTransaction (name);
    }

    Presenter::Command::~Command() { --presenter.editing; --presenter.busy; }

    Presenter::ScopedStateReplacement::ScopedStateReplacement (Presenter* p) noexcept : presenter (p)
    {
        if (presenter != nullptr) ++presenter->busy;
    }

    Presenter::ScopedStateReplacement::~ScopedStateReplacement() noexcept
    {
        if (presenter != nullptr) --presenter->busy;
    }

    //==========================================================================
    Presenter::Presenter (PlugProcessor& processor) : proc (processor)
    {
        for (auto& p : shownParam) p = "main";

        // Heuristique hostDriven (d-1) : on écoute les 256 entrées slotNN.*. Le rappel
        // peut venir du thread audio (automation qui joue), il ne fait donc qu'un
        // fetch_or sur un atomique ; le timer tranche au message thread.
        slotOfParameter.assign ((size_t) juce::jmax (0, proc.getParameters().size()), 0);
        for (const auto& id : grid::allIds())
        {
            if (! id.startsWith ("slot")) continue;
            auto* p = proc.state().getParameter (id);
            if (p == nullptr) continue;
            const int index = p->getParameterIndex();
            if (juce::isPositiveAndBelow (index, (int) slotOfParameter.size()))
                slotOfParameter[(size_t) index] = id.substring (4, 6).getIntValue();
            p->addListener (this);
        }

        library.rescan();
        proc.stateTree().addListener (this);
        startTimerHz (30);
    }

    Presenter::~Presenter()
    {
        stopTimer();
        cancelPendingUpdate();
        proc.stateTree().removeListener (this);
        for (const auto& id : grid::allIds())
            if (id.startsWith ("slot"))
                if (auto* p = proc.state().getParameter (id))
                    p->removeListener (this);
    }

    void Presenter::addListener (Listener* l)    { listeners.add (l); }
    void Presenter::removeListener (Listener* l) { listeners.remove (l); }

    //==========================================================================
    void Presenter::mark (juce::uint32 bits, int slot1)
    {
        pending.bits |= bits;
        if (slot1 >= 1 && slot1 <= kSlots)
            pending.slots |= (juce::uint16) (1u << (slot1 - 1));

        // Dès que le PILOTE fait bouger l'état, le preset affiché ne le décrit plus :
        // « geste1 * ». Ni la sélection, ni les préférences, ni le mode A/B (ils ne sont
        // pas dans le preset, décision du 16/09) ; ni un flush venu de l'hôte — sinon un
        // clip automatisé qui joue ferait clignoter l'étoile en permanence, et l'étoile
        // ne dirait plus rien du travail du pilote (décision du 17/09).
        constexpr juce::uint32 kStateBits = ViewMask::Slots | ViewMask::Params | ViewMask::Line
                                          | ViewMask::Master | ViewMask::Macros
                                          | ViewMask::Generation | ViewMask::Sequencer;
        if ((bits & kStateBits) != 0 && editing.load() != 0) presetModified = true;

        triggerAsyncUpdate();
    }

    void Presenter::markGridId (const String& gridId)
    {
        if (gridId.startsWith ("slot"))
        {
            const int slot1 = gridId.substring (4, 6).getIntValue();
            mark (gridId.endsWith (".active") ? (ViewMask::Slots | ViewMask::Params) : ViewMask::Params, slot1);
        }
        else if (gridId.startsWith ("master.")) mark (ViewMask::Master);
        else if (gridId.startsWith ("macro"))   mark (ViewMask::Macros);
        else if (gridId.startsWith ("seq."))    mark (ViewMask::Sequencer);
    }

    void Presenter::markNode (const ValueTree& t)
    {
        // On remonte jusqu'à l'emplacement : un widget ne sait pas lire un chemin
        // d'arbre, il sait relire la View d'un emplacement.
        int slot1 = 0;
        for (auto n = t; n.isValid(); n = n.getParent())
        {
            if (n.hasType (state::id::Slot))       { slot1 = (int) n.getProperty (state::id::index, 0); break; }
            if (n.hasType (state::id::Generation)) { mark (ViewMask::Generation); return; }
            if (n.hasType (state::id::Macros))     { mark (ViewMask::Macros); return; }
        }

        if (slot1 <= 0) { mark (ViewMask::All); return; }

        if (t.hasType (state::id::Step) || t.hasType (state::id::Line) || t.hasType (state::id::V))
            mark (ViewMask::Line | ViewMask::Params, slot1);
        else
            mark (ViewMask::Slots | ViewMask::Params | ViewMask::Line, slot1);
    }

    void Presenter::valueTreePropertyChanged (ValueTree& t, const juce::Identifier&)
    {
        if (t.hasType (state::id::PARAM)) { markGridId (t.getProperty (state::id::paramId).toString()); return; }
        markNode (t);
    }

    void Presenter::valueTreeChildAdded (ValueTree& parent, ValueTree& c)
    {
        if (c.hasType (state::id::PARAM)) { markGridId (c.getProperty (state::id::paramId).toString()); return; }
        markNode (parent);
    }

    void Presenter::valueTreeChildRemoved (ValueTree& parent, ValueTree& c, int)
    {
        if (c.hasType (state::id::PARAM)) { markGridId (c.getProperty (state::id::paramId).toString()); return; }
        markNode (parent);
    }

    void Presenter::valueTreeChildOrderChanged (ValueTree& parent, int, int) { markNode (parent); }
    void Presenter::valueTreeParentChanged (ValueTree&)                      { mark (ViewMask::All); }

    // apvts.replaceState() : l'arbre entier change d'objet sous nos pieds (projet ouvert,
    // preset chargé). Le processeur retire son écouteur le temps du remplacement, pas
    // nous : c'est par ici que la vue apprend qu'elle doit tout relire.
    void Presenter::valueTreeRedirected (ValueTree&)                         { mark (ViewMask::All); }

    void Presenter::handleAsyncUpdate()
    {
        const ViewMask what = pending;
        pending = ViewMask();
        if (what.bits == ViewMask::None) return;
        listeners.call ([&what] (Listener& l) { l.viewChanged (what); });
    }

    void Presenter::timerCallback()
    {
        // c-1 : le seul chemin de l'audio vers la vue. Un atomique, aucune allocation,
        // et une notification seulement quand la position change — pas 30 par seconde.
        const auto snap = proc.engine().uiSnapshot();
        const int packed = (snap.step * 4) + (snap.playing ? 2 : 0) + (snap.freeRunning ? 1 : 0);
        if (packed != lastStep)
        {
            lastStep = packed;
            TransportView tv;
            tv.step = snap.step; tv.playing = snap.playing; tv.freeRunning = snap.freeRunning;
            listeners.call ([&tv] (Listener& l) { l.transportChanged (tv); });
        }

        const auto fresh = (juce::uint16) (hostPending.exchange (0) & ~(juce::uint32) hostDriven);
        if (fresh != 0)
        {
            hostDriven = (juce::uint16) (hostDriven | fresh);
            pending.slots |= fresh;
            mark (ViewMask::Slots);
        }
    }

    void Presenter::parameterValueChanged (int parameterIndex, float)
    {
        // Peut venir du thread audio : aucune allocation, aucun verrou, on pose un bit.
        // Pendant une commande, un geste de knob ou un remplacement d'état, rien n'est
        // compté — un chargement de preset ne marque jamais (précision pilote Q4).
        if (busy.load (std::memory_order_relaxed) != 0) return;
        if (! juce::isPositiveAndBelow (parameterIndex, (int) slotOfParameter.size())) return;
        const int slot1 = slotOfParameter[(size_t) parameterIndex];
        if (slot1 < 1 || slot1 > kSlots) return;
        hostPending.fetch_or (1u << (slot1 - 1), std::memory_order_relaxed);
    }

    //==========================================================================
    SlotView Presenter::slotView (int slot1) const
    {
        SlotView v;
        v.slot1 = juce::jlimit (1, kSlots, slot1);
        auto& s = proc.stateTree();
        auto sl = state::slot (s, v.slot1);
        if (! sl.isValid()) return v;

        v.skillId = sl.getProperty (state::id::skill, "").toString();
        v.present = v.skillId.isNotEmpty();
        const auto* info = v.present ? SkillRegistry::instance().info (v.skillId) : nullptr;
        v.unknown = v.present && info == nullptr;
        const int skillVersion = (int) sl.getProperty (state::id::skillVersion, 0);
        v.skillVersion = skillVersion;
        v.skillLabel = info != nullptr ? fr (info->label) : (v.unknown ? v.skillId : Format::emptySlotText());
        if (info != nullptr)
            v.mixLaw = info->mixLaw == MixLaw::Minus6 ? "-6 dB" : (info->mixLaw == MixLaw::Zero ? "0 dB" : "-3 dB");

        v.active = state::readParam (s, slotGridId (v.slot1, "active")) >= 0.5f;
        v.tailRing = sl.getProperty (state::id::tail, "ring").toString() != "cut";
        v.glide = state::readParam (s, slotGridId (v.slot1, "glide"));
        v.fade  = state::readParam (s, slotGridId (v.slot1, "fade"));
        v.hasPattern = StateQuery::lineHasPattern (s, v.slot1);
        v.hostDriven = (hostDriven & (juce::uint16) (1u << (v.slot1 - 1))) != 0;

        const float density = (float) (double) s.getChildWithName (state::id::Generation)
                                                 .getProperty (state::id::density, 1.0);
        const int shownStep = juce::jlimit (1, kSteps, stepFirst);

        for (int m = 0; m < kSlotParams; ++m)
        {
            const String name (grid::kModulableName[(size_t) m]);
            const auto* decl = declFor (info, m);
            auto& p = v.params[(size_t) m];

            p.name = name;
            p.declared = decl != nullptr;
            // Correction 1 de la phase 2 (18/09) : non déclarée par une skill CONNUE = inerte
            // (tiret, grisée), comme la réserve — le moteur ne la lit pas. Une skill inconnue
            // (§3.9) garde ses entrées génériques visibles : ses données sont conservées.
            p.inert = decl == nullptr && (v.unknown ? Format::isReserve (name)
                                                    : Format::isInertWhenUndeclared (name));
            // fr(...) et non String(...) : ce sont des octets UTF-8 déclarés par la skill.
            p.label = decl != nullptr ? fr (decl->label)
                                      : (p.inert ? Format::inertLabel() : Format::genericLabel (name));
            p.unit  = decl != nullptr ? fr (decl->unit) : String();
            p.help  = decl != nullptr ? fr (decl->help)
                                      : (v.unknown ? Format::unknownSkillHelp (v.skillId, skillVersion)
                                                   : (p.inert ? Format::inertHelp (name)
                                                              : Format::genericHelp (name)));

            const auto spec = state::readParamSpec (sl, name);
            p.locked = spec.locked;
            p.structural = spec.structural;
            p.glide = spec.glide;
            p.prob = spec.prob;
            p.modulable = ! spec.structural;
            p.lockedByDefault = decl != nullptr && decl->lockClass == LockClass::LockedByDefault;

            const auto halo = StateQuery::haloRange (s, v.slot1, name, density);
            p.min = halo.min; p.max = halo.max; p.effLo = halo.effLo; p.effHi = halo.effHi;

            p.base = state::readParam (s, slotGridId (v.slot1, name));
            p.raw = StateQuery::stepDisplayValue (s, v.slot1, shownStep, name);
            p.stepMode = modeView (state::readStepSpec (state::step (sl, shownStep)).mode);

            const String displayed = (decl != nullptr && decl->display != nullptr) ? decl->display (p.raw) : String();
            p.valueText = Format::valueText (p.raw, displayed, p.unit);
            p.lockReason = Format::lockReason (p.locked, p.lockedByDefault, p.structural, p.help);
        }
        return v;
    }

    LineView Presenter::lineView (int slot1) const
    {
        LineView v;
        v.slot1 = juce::jlimit (1, kSlots, slot1);
        auto& s = proc.stateTree();
        auto sl = state::slot (s, v.slot1);
        if (! sl.isValid()) return v;

        v.length = grid::seqLength (state::readParam (s, "seq.length"));
        v.modeB = lineModeB[(size_t) (v.slot1 - 1)];
        v.shownParam = shownParam[(size_t) (v.slot1 - 1)];

        for (int i = 1; i <= kSteps; ++i)
        {
            const auto spec = state::readStepSpec (state::step (sl, i));
            auto& st = v.steps[(size_t) (i - 1)];
            st.on = spec.on;
            st.mode = modeView (spec.mode);
            st.value = StateQuery::stepDisplayValue (s, v.slot1, i, v.shownParam);
            st.hasValue = spec.mode != StepMode::Base;
        }
        return v;
    }

    TransportView Presenter::transportView() const
    {
        const auto snap = proc.engine().uiSnapshot();
        TransportView v;
        v.step = snap.step; v.playing = snap.playing; v.freeRunning = snap.freeRunning;
        return v;
    }

    SequencerView Presenter::sequencerView() const
    {
        auto& s = proc.stateTree();
        SequencerView v;
        v.length   = grid::seqLength (state::readParam (s, "seq.length"));
        v.division = grid::divisionIndex (state::readParam (s, "seq.division"));
        for (int i = 0; i < grid::kDivisionCount; ++i) v.divisionChoices.add (Format::divisionLabel (i));
        v.swing = state::readParam (s, "seq.swing");
        v.swingText = Format::swingText (v.swing);
        return v;
    }

    MasterView Presenter::masterView() const
    {
        auto& s = proc.stateTree();
        MasterView v;
        v.volume = state::readParam (s, "master.volume");
        v.mix    = state::readParam (s, "master.mix");
        v.law    = (int) std::lround (state::readParam (s, "master.mixLaw"));
        v.volumeText = display::dB (grid::gainLinear (v.volume));
        v.mixText = display::sig (100.0 * (double) v.mix);
        v.lawChoices = choicesOf (proc.state(), "master.mixLaw");

        // §3.10 : le moteur n'applique que le mélange et le volume. Les six autres entrées
        // existent dans la grille figée et ne sont lues par personne — elles s'affichent
        // grisées avec « (J4c) », pour que personne ne les croie actives (incident 17/09).
        for (const char* id : { "master.drive", "master.tone", "master.comp",
                                "master.lowFreq", "master.driveRouting", "master.quality" })
        {
            MasterEntryView e;
            e.id = id;
            e.label = Format::masterInertLabel (e.id);
            e.raw = state::readParam (s, e.id);
            e.inert = true;
            e.help = Format::masterInertHelp();
            e.choices = choicesOf (proc.state(), e.id);
            v.inertEntries.push_back (e);
        }
        return v;
    }

    std::array<MacroView, kMacros> Presenter::macroViews() const
    {
        auto& s = proc.stateTree();
        std::array<MacroView, kMacros> out {};
        auto macros = s.getChildWithName (state::id::Macros);

        for (int m = 1; m <= kMacros; ++m)
        {
            auto& v = out[(size_t) (m - 1)];
            v.macro1 = m;
            v.value = state::readParam (s, "macro" + String (m));

            String routes;
            auto macro = macros.getChildWithProperty (state::id::index, m);
            for (const auto& r : macro)
            {
                if (! r.hasType (state::id::Route)) continue;
                if (routes.isNotEmpty()) routes << "\n";
                routes << "→ "_fr << (int) r.getProperty (state::id::slot, 0) << " "
                       << r.getProperty (state::id::param, "").toString() << " "
                       << Format::rawText ((float) (double) r.getProperty (state::id::lo, 0.0)) << "–"_fr
                       << Format::rawText ((float) (double) r.getProperty (state::id::hi, 0.0));
            }
            v.routes = routes;
        }
        return out;
    }

    AboutView Presenter::aboutView() const
    {
        AboutView v;
        v.buildStamp = buildStamp();
        // Le bouton de la barre ne tient que « Plug 0.3.0 · commit 484061a » ; la date de
        // compilation vit dans le panneau. Découpé ici, jamais réécrit par un widget :
        // une seule source de vérité pour l'identité du binaire (incident du 17/09).
        {
            juce::StringArray parts;
            parts.addTokens (v.buildStamp, "·"_fr, "");
            parts.trim();
            v.shortStamp = parts.size() >= 2 ? parts[0] + " · "_fr + parts[1] : v.buildStamp;
        }
        v.juceVersion = juce::SystemStats::getJUCEVersion();
        for (const auto& id : SkillRegistry::instance().ids())
        {
            const auto* info = SkillRegistry::instance().info (id);
            if (info == nullptr || info->factice) continue;

            AboutSkillView s;
            s.id = info->id;
            s.label = fr (info->label);
            s.version = info->version;
            s.mixLaw = info->mixLaw == MixLaw::Minus6 ? "-6 dB" : (info->mixLaw == MixLaw::Zero ? "0 dB" : "-3 dB");

            // La latence n'est pas DÉCLARÉE par SkillInfo : on la demande à une instance
            // préparée à 48 kHz. C'est le seul moyen de la connaître sans toucher au
            // contrat §3.9 — et si l'instance ne se crée pas, on n'écrit rien plutôt
            // que d'afficher un zéro qui mentirait.
            //
            // OUI, cela ALLOUE : prepare() construit les lignes à retard des neuf skills
            // (core.grain en tête). C'est assumé et borné — une fois, à l'ouverture du
            // panneau « À propos », sur le message thread, jamais pendant l'audio ni par
            // frame. L'alternative était un champ `latencySamples` dans SkillInfo, donc
            // une modification du contrat §3.9 et des neuf skills pour un renseignement
            // d'affichage : décision pilote du 17/09, on garde l'instanciation.
            if (auto skill = SkillRegistry::instance().create (id))
            {
                skill->prepare (48000.0, 512);
                s.latency = skill->latencySamples();
                s.latencyKnown = true;
            }
            v.skills.push_back (std::move (s));
        }
        return v;
    }

    PrefsView Presenter::prefsView() const
    {
        PrefsView v;
        v.ratioTwoThirds = preferences.ratioTwoThirds();
        v.zoom = preferences.zoom();
        v.hoverHelp = preferences.hoverHelp();
        v.helpDelayMs = preferences.helpDelayMs();
        v.defaultMixLaw = preferences.defaultMixLaw();
        v.shownSlots = preferences.shownSlots();
        return v;
    }

    PresetView Presenter::presetView() const
    {
        PresetView v;
        v.folder = PresetLibrary::userDir().getFullPathName();
        v.currentName = presetName;
        v.modified = presetModified;
        for (int i = 0; i < library.size(); ++i)
        {
            v.names.add (library.name (i));
            if (library.name (i) == presetName) v.currentIndex = i;
        }
        return v;
    }

    StepCountsView Presenter::stepCountsView (int slot1, int first1, int last1) const
    {
        const auto c = StateQuery::stepCounts (proc.stateTree(), slot1, first1, last1);
        StepCountsView v;
        v.generated = c.generated;
        v.explicitCount = c.explicitCount;
        v.off = c.off;
        v.total = juce::jmax (0, juce::jmin (kSteps, juce::jmax (first1, last1))
                                     - juce::jmax (1, juce::jmin (first1, last1)) + 1);
        return v;
    }

    GenerationView Presenter::generationView() const
    {
        auto gen = proc.stateTree().getChildWithName (state::id::Generation);
        GenerationView v;
        v.masterSeed = (juce::int64) gen.getProperty (state::id::masterSeed, 0);
        v.counter = (int) gen.getProperty (state::id::counter, 0);
        v.density = (float) (double) gen.getProperty (state::id::density, 1.0);
        return v;
    }

    UndoView Presenter::undoView() const
    {
        // Le NOM de la transaction est ce qui rend la granularité visible : le pilote
        // doit lire « Annuler : Verrouiller Rapport », pas « Annuler ».
        auto& um = proc.undoManager();
        UndoView v;
        v.canUndo = um.canUndo();
        v.canRedo = um.canRedo();
        v.undoName = um.getUndoDescription();
        v.redoName = um.getRedoDescription();
        return v;
    }

    int Presenter::displayedSlots() const
    {
        auto& s = proc.stateTree();
        int highest = 0;
        for (int i = 1; i <= kSlots; ++i)
            if (state::slot (s, i).getProperty (state::id::skill, "").toString().isNotEmpty())
                highest = i;
        return juce::jlimit (1, kSlots, juce::jmax (preferences.shownSlots(), highest));
    }

    juce::StringArray Presenter::skillIds() const
    {
        juce::StringArray out;
        for (const auto& id : SkillRegistry::instance().ids())
            if (const auto* info = SkillRegistry::instance().info (id))
                if (! info->factice) out.add (id);
        return out;
    }

    String Presenter::skillLabel (const String& skillId) const
    {
        if (const auto* info = SkillRegistry::instance().info (skillId)) return fr (info->label);
        return skillId;
    }

    String Presenter::moveWarning (int from1) const
    {
        auto& s = proc.stateTree();
        const bool pattern = StateQuery::lineHasPattern (s, from1);
        const bool host = from1 >= 1 && from1 <= kSlots
                              && (hostDriven & (juce::uint16) (1u << (from1 - 1))) != 0;
        if (! pattern && ! host) return {};

        String t;
        if (pattern)
            t << "Le motif de la ligne "_fr << from1 << " et les réglages de l'emplacement "_fr
              << from1 << " restent sur place ; seul l'effet déménage (§3.2)."_fr;
        if (host)
        {
            if (t.isNotEmpty()) t << " ";
            t << "Des valeurs sont arrivées de l'hôte sur cet emplacement — "
                 "automation ou macro Live probable."_fr;
        }
        return t;
    }

    String Presenter::avenirText() const
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource ("AVENIR_md", size))
            return String::fromUTF8 (data, size);
        return {};
    }

    //==========================================================================
    void Presenter::beginGesture (const String& gridId)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* p = proc.state().getParameter (gridId);
        if (p == nullptr) return;
        ++busy;
        ++editing;
        proc.undoManager().beginNewTransaction ("Régler "_fr + p->getName (40));
        p->beginChangeGesture();
    }

    void Presenter::setParam (const String& gridId, float raw)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        auto* p = proc.state().getParameter (gridId);
        if (p == nullptr) return;

        // Hors geste (valeur tapée, réglage unique), la commande ouvre sa propre
        // transaction ; un geste de knob n'en ouvre qu'une pour tout le mouvement.
        const bool standalone = busy.load() == 0;
        if (standalone) { ++busy; ++editing; proc.undoManager().beginNewTransaction ("Régler "_fr + p->getName (40)); }

        // On écrit la valeur DANS L'ARBRE, avec l'UndoManager du pilote. L'APVTS écoute
        // son propre arbre : il relaie au paramètre, donc à l'hôte. Le recopiage
        // périodique de l'APVTS ne trouvera plus rien à écrire, et le geste reste
        // annulable alors même que ce recopiage, lui, ne l'est plus (parade du piège
        // APVTS/undo ; voir les deux UndoManager dans PlugProcessor.h).
        state::setParam (proc.stateTree(), gridId, raw, &proc.undoManager());
        if (standalone) { --editing; --busy; }
    }

    void Presenter::endGesture (const String& gridId)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (auto* p = proc.state().getParameter (gridId)) p->endChangeGesture();
        if (editing.load() > 0) --editing;
        if (busy.load() > 0) --busy;
    }

    void Presenter::setSkill (int slot1, const String& skillId)
    {
        Command c (*this, "Poser "_fr + skillLabel (skillId) + " en " + String (slot1));
        // StateEdit, pas StateSchema : la pose libère aussi les entrées que la skill
        // arrivante ne déclare pas (ETAT Rév. 9 Q1, décision pilote du 17/09).
        StateEdit::setSkill (proc.stateTree(), slot1, skillId, &proc.undoManager());
    }

    void Presenter::clearSlot (int slot1)
    {
        Command c (*this, "Vider "_fr + String (slot1));
        StateEdit::clearSlot (proc.stateTree(), slot1, &proc.undoManager());
    }

    void Presenter::moveSlot (int from1, int to1, MoveMode mode)
    {
        // Le nom lu dans l'historique doit dire QUOI, D'OÙ et VERS OÙ : « Déplacer FM
        // de 3 vers 5 ». « Déplacer » seul ne se relit pas trois gestes plus tard.
        const auto source = slotView (from1);
        const String what = source.present ? skillLabel (source.skillId) : "l'emplacement vide"_fr;
        const String verb = mode == MoveMode::Swap ? "Échanger "_fr
                          : mode == MoveMode::Copy ? "Copier "_fr
                                                   : "Déplacer "_fr;
        Command c (*this, verb + what + " de " + String (from1) + " vers " + String (to1));
        StateEdit::moveSlot (proc.stateTree(), from1, to1,
                             mode == MoveMode::Swap ? StateEdit::Mode::Swap
                           : mode == MoveMode::Copy ? StateEdit::Mode::Copy
                                                    : StateEdit::Mode::Insert,
                             &proc.undoManager());
    }

    void Presenter::setActive (int slot1, bool on)
    {
        Command c (*this, fr (on ? "Activer" : "Contourner") + " l'emplacement "_fr + String (slot1));
        state::setParam (proc.stateTree(), slotGridId (slot1, "active"), on ? 1.0f : 0.0f, &proc.undoManager());
    }

    void Presenter::setTail (int slot1, bool ring)
    {
        Command c (*this, fr (ring ? "Laisser la queue" : "Couper la queue") + " · emplacement "_fr + String (slot1));
        state::setTail (proc.stateTree(), slot1, ring, &proc.undoManager());
    }

    void Presenter::setLocked (int slot1, const String& paramName, bool locked)
    {
        Command c (*this, fr (locked ? "Verrouiller " : "Déverrouiller ")
                              + labelOf (proc.stateTree(), slot1, paramName));
        state::setLocked (proc.stateTree(), slot1, paramName, locked, &proc.undoManager());
    }

    void Presenter::setRange (int slot1, const String& paramName, float min, float max)
    {
        Command c (*this, "Plage de "_fr + labelOf (proc.stateTree(), slot1, paramName));
        state::setRange (proc.stateTree(), slot1, paramName, min, max, &proc.undoManager());
    }

    void Presenter::setProb (int slot1, const String& paramName, float prob)
    {
        Command c (*this, "Probabilité de "_fr + labelOf (proc.stateTree(), slot1, paramName));
        state::setProb (proc.stateTree(), slot1, paramName, prob, &proc.undoManager());
    }

    void Presenter::setTransition (int slot1, const String& paramName, bool glide)
    {
        Command c (*this, fr (glide ? "Glissement sur " : "Saut sur ")
                              + labelOf (proc.stateTree(), slot1, paramName));
        state::setTransition (proc.stateTree(), slot1, paramName, glide, &proc.undoManager());
    }

    void Presenter::setStepOn (int slot1, int step1, bool on)
    {
        Command c (*this, fr (on ? "Activer le pas " : "Éteindre le pas ") + String (step1));
        state::setStepOn (proc.stateTree(), slot1, step1, on, &proc.undoManager());
    }

    void Presenter::setStepExplicit (int slot1, int step1, const String& paramName, float value)
    {
        Command c (*this, "Valeur du pas "_fr + String (step1) + " · "_fr + labelOf (proc.stateTree(), slot1, paramName));
        auto& s = proc.stateTree();
        state::setStepMode (s, slot1, step1, StepMode::Explicit, &proc.undoManager());
        state::setExplicit (s, slot1, step1, paramName, value, &proc.undoManager());
    }

    void Presenter::setStepsExplicit (int slot1, int first1, int last1, const String& paramName, float value)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const int a = juce::jlimit (1, kSteps, juce::jmin (first1, last1));
        const int b = juce::jlimit (1, kSteps, juce::jmax (first1, last1));
        const float v = juce::jlimit (0.0f, 1.0f, value);
        auto& s = proc.stateTree();

        // « Coupure à 333 Hz sur 16 pas » : le libellé et le texte de la valeur sont ceux
        // de la skill (c-2), jamais 0,62 brut quand elle sait dire mieux.
        const String name = labelOf (s, slot1, paramName) + " à "_fr + valueTextOf (s, slot1, paramName, v)
                            + (a == b ? " · pas "_fr + String (a) : " sur "_fr + String (b - a + 1) + " pas"_fr);

        // Seul, la commande ouvre sa transaction ; dans un geste, elle prend celle du geste
        // et la renomme avec la valeur posée — même discipline que setParam.
        std::unique_ptr<Command> own;
        if (stepsGesture == nullptr) own = std::make_unique<Command> (*this, name);
        else                         proc.undoManager().setCurrentTransactionName (name);

        for (int i = a; i <= b; ++i)
        {
            state::setStepMode (s, slot1, i, StepMode::Explicit, &proc.undoManager());
            state::setExplicit (s, slot1, i, paramName, v, &proc.undoManager());
        }
    }

    void Presenter::beginStepsGesture (int slot1, int first1, int last1, const String& paramName)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const int n = juce::jmax (first1, last1) - juce::jmin (first1, last1) + 1;
        stepsGesture = std::make_unique<Command> (*this, labelOf (proc.stateTree(), slot1, paramName)
                                                             + " sur "_fr + String (n) + " pas"_fr);
    }

    void Presenter::endStepsGesture()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        stepsGesture.reset();
    }

    void Presenter::generate (int slot1, int first1, int last1, float density)
    {
        Command c (*this, "Générer les pas "_fr + String (first1) + " à "_fr + String (last1));
        state::generate (proc.stateTree(), slot1, first1, last1, density, &proc.undoManager());
    }

    void Presenter::capture (int slot1, int first1, int last1)
    {
        Command c (*this, "Figer les pas "_fr + String (first1) + " à "_fr + String (last1));
        state::capture (proc.stateTree(), slot1, first1, last1, &proc.undoManager());
    }

    void Presenter::setMasterSeed (juce::uint32 seed)
    {
        // « Graine 4821 » et non « Nouvelle graine » : l'historique doit dire LAQUELLE,
        // sinon deux tirages successifs sont indiscernables (schéma §2).
        Command c (*this, "Graine "_fr + String ((juce::int64) seed));
        state::setMasterSeed (proc.stateTree(), seed, &proc.undoManager());
    }

    void Presenter::setSeqLength (int steps)
    {
        const int n = juce::jlimit (2, kSteps, steps);
        Command c (*this, "Longueur "_fr + String (n) + " pas"_fr);
        // Inverse exact de grid::seqLength : (n − 2) / 30, que seqLength arrondit sur n.
        state::setParam (proc.stateTree(), "seq.length", (float) (n - 2) / 30.0f, &proc.undoManager());
    }

    void Presenter::setSeqDivision (int index)
    {
        const int k = juce::jlimit (0, grid::kDivisionCount - 1, index);
        Command c (*this, "Division "_fr + Format::divisionLabel (k));
        state::setParam (proc.stateTree(), "seq.division", grid::divisionValueFor (k), &proc.undoManager());
    }

    void Presenter::setDensity (float density)
    {
        Command c (*this, "Densité du tirage"_fr);
        // La densité vit dans le nœud Generation, que StateSchema n'expose pas par une
        // fonction dédiée : on écrit la propriété par son identifiant public, comme
        // StateEdit le fait pour skillVersion. StateSchema reste intouché.
        auto gen = proc.stateTree().getChildWithName (state::id::Generation);
        if (gen.isValid())
            gen.setProperty (state::id::density, (double) juce::jlimit (0.0f, 1.0f, density), &proc.undoManager());
        mark (ViewMask::Generation);
    }

    bool Presenter::loadPreset (const juce::File& f)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        ScopedStateReplacement guard (this);   // un chargement de preset ne marque jamais hostDriven (Q4)
        const bool ok = proc.loadPresetFile (f);
        if (ok)
        {
            // Le drapeau se pose APRÈS : le chargement a fait pleuvoir des notifications
            // qui, toutes, ont marqué l'état comme modifié. Il ne l'est pas : il EST le preset.
            presetName = f.getFileNameWithoutExtension();
            presetModified = false;
        }
        mark (ViewMask::All);
        return ok;
    }

    bool Presenter::loadPresetIndex (int index)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto f = library.file (index);
        return f != juce::File() && loadPreset (f);
    }

    bool Presenter::savePreset (const juce::File& f)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const bool ok = proc.savePresetFile (f);
        if (ok)
        {
            presetName = f.getFileNameWithoutExtension();
            presetModified = false;
            library.rescan();          // le nouveau nom doit paraître dans le menu tout de suite
        }
        mark (ViewMask::Presets);
        return ok;
    }

    void Presenter::rescanPresets()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        library.rescan();
        mark (ViewMask::Presets);
    }

    void Presenter::undo()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        ++busy; ++editing;                   // défaire est un geste du pilote, l'étoile suit
        proc.undoManager().undo();
        --editing; --busy;
        mark (ViewMask::All);
    }

    void Presenter::redo()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        ++busy; ++editing;
        proc.undoManager().redo();
        --editing; --busy;
        mark (ViewMask::All);
    }

    //==========================================================================
    void Presenter::selectSlot (int slot1)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        selection = juce::jlimit (1, kSlots, slot1);
        mark (ViewMask::Session, selection);
    }

    void Presenter::selectSteps (int first1, int last1)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        stepFirst = juce::jlimit (1, kSteps, juce::jmin (first1, last1));
        stepLast  = juce::jlimit (1, kSteps, juce::jmax (first1, last1));
        // Session SEULE : déplacer la sélection ne change pas l'état, donc rien ne
        // justifie de relire les dix lignes du séquenceur à chaque mouvement de souris
        // (mesuré à 2,57 ms par événement avant cette correction, measure/MESURES_J4b.md).
        mark (ViewMask::Session, selection);
    }

    void Presenter::setLineModeB (int slot1, bool modeB)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (! juce::isPositiveAndNotGreaterThan (slot1, kSlots)) return;
        lineModeB[(size_t) (slot1 - 1)] = modeB;
        mark (ViewMask::Session | ViewMask::Line, slot1);
    }

    void Presenter::setShownParam (int slot1, const String& paramName)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (! juce::isPositiveAndNotGreaterThan (slot1, kSlots)) return;
        shownParam[(size_t) (slot1 - 1)] = paramName;
        mark (ViewMask::Session | ViewMask::Line, slot1);
    }

    void Presenter::toggleRatio()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        preferences.setRatioTwoThirds (! preferences.ratioTwoThirds());
        preferences.save();
        mark (ViewMask::Prefs);
    }

    void Presenter::setPrefZoom (double zoom)      { JUCE_ASSERT_MESSAGE_THREAD preferences.setZoom (zoom);           preferences.save(); mark (ViewMask::Prefs); }
    void Presenter::setPrefHoverHelp (bool on)     { JUCE_ASSERT_MESSAGE_THREAD preferences.setHoverHelp (on);        preferences.save(); mark (ViewMask::Prefs); }
    void Presenter::setPrefHelpDelayMs (int ms)    { JUCE_ASSERT_MESSAGE_THREAD preferences.setHelpDelayMs (ms);      preferences.save(); mark (ViewMask::Prefs); }
    void Presenter::setPrefDefaultMixLaw (int law) { JUCE_ASSERT_MESSAGE_THREAD preferences.setDefaultMixLaw (law);   preferences.save(); mark (ViewMask::Prefs); }
    void Presenter::setPrefShownSlots (int count)  { JUCE_ASSERT_MESSAGE_THREAD preferences.setShownSlots (count);    preferences.save(); mark (ViewMask::Prefs | ViewMask::Slots); }
}
