// StateSchema — la forme de l'état sauvegardé, version 2 (CdC §3.6, contrat J3 a).
// Garantit : un seul arbre PlugState, les 284 PARAM de la grille plus les nœuds
// Generation, Slots/Slot/Param/Line/Step/Mod et Macros ; migration 1→2 par
// ajout des défauts ; tout nœud ou attribut inconnu est conservé (une skill
// absente garde ses données, §3.9). La base d'un paramètre n'est jamais
// dupliquée : c'est son PARAM. L'ordre de la chaîne est l'indice de Slot (§3.2).
// Toutes les fonctions d'édition passent par l'UndoManager fourni (§3.6).
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "StepValue.h"
#include <optional>
#include <utility>
#include <vector>

namespace plug::state
{
    constexpr int kSchemaVersion = 2;   // 1 = J2 (PARAM seuls). Ne décroît jamais.
    constexpr int kSlots = 16;
    constexpr int kSteps = 32;

    namespace id
    {
        #define PLUG_ID(name) inline const juce::Identifier name { #name };
        PLUG_ID (PlugState) PLUG_ID (schemaVersion) PLUG_ID (pluginVersion)
        PLUG_ID (PARAM) PLUG_ID (value)
        PLUG_ID (Generation) PLUG_ID (masterSeed) PLUG_ID (counter) PLUG_ID (density)
        PLUG_ID (Slots) PLUG_ID (Slot) PLUG_ID (index) PLUG_ID (skill) PLUG_ID (skillVersion) PLUG_ID (tail) PLUG_ID (wet)
        PLUG_ID (Param) PLUG_ID (name) PLUG_ID (min) PLUG_ID (max) PLUG_ID (prob) PLUG_ID (locked) PLUG_ID (transition)
        PLUG_ID (Line) PLUG_ID (Step) PLUG_ID (i) PLUG_ID (on) PLUG_ID (mode) PLUG_ID (seed) PLUG_ID (V)
        PLUG_ID (Mod) PLUG_ID (Env1) PLUG_ID (Source2) PLUG_ID (Point) PLUG_ID (Route)
        PLUG_ID (src) PLUG_ID (dst) PLUG_ID (depth) PLUG_ID (rate) PLUG_ID (sync) PLUG_ID (loop) PLUG_ID (trigger)
        PLUG_ID (kind) PLUG_ID (attack) PLUG_ID (release) PLUG_ID (t) PLUG_ID (v) PLUG_ID (curve)
        PLUG_ID (Macros) PLUG_ID (Macro) PLUG_ID (slot) PLUG_ID (param) PLUG_ID (lo) PLUG_ID (hi)
        #undef PLUG_ID
        inline const juce::Identifier paramId { "id" };
    }

    //==========================================================================
    // Construction et migration
    juce::ValueTree createDefault();                       // PlugState v2 complet, sans PARAM
    void ensureParams (juce::ValueTree& plugState);        // ajoute les PARAM manquants à leur défaut (outils hors hôte)
    void ensureSchema (juce::ValueTree& plugState);        // migration : complète, n'enlève rien

    //==========================================================================
    // Accès (slot1, step1 : numérotation humaine 1..16, 1..32)
    juce::ValueTree slot (const juce::ValueTree& plugState, int slot1);
    juce::ValueTree step (const juce::ValueTree& slotTree, int step1);
    juce::ValueTree param (const juce::ValueTree& slotTree, const juce::String& name);

    ParamSpec readParamSpec (const juce::ValueTree& slotTree, const juce::String& name);
    StepSpec  readStepSpec  (const juce::ValueTree& stepTree);
    std::optional<float> readExplicit (const juce::ValueTree& stepTree, const juce::String& name);
    float readParam (const juce::ValueTree& plugState, const juce::String& gridId);   // PARAM brut

    //==========================================================================
    // Édition — message thread, annulable
    using Undo = juce::UndoManager*;

    void setParam       (juce::ValueTree& s, const juce::String& gridId, float raw, Undo um = nullptr);
    void setSkill       (juce::ValueTree& s, int slot1, const juce::String& skillId, Undo um);   // J3-5 : pose les verrous déclarés
    void setTail        (juce::ValueTree& s, int slot1, bool ring, Undo um);
    // Phase 3 (pilote, 20/09) : le Dry / Wet GÉNÉRAL de l'emplacement — une surcouche,
    // attribut du nœud Slot comme `tail`, hors grille (aucun PARAM, non automatisable
    // par l'hôte), 0..1, défaut 1 (transparent). Le moteur en multiplie la courbe de
    // slotNN.mix ; il ne remplace ni ne copie le mix séquencé pas à pas. Complété par
    // ensureSlot à son défaut : un état antérieur se lit sans migration.
    void setWet         (juce::ValueTree& s, int slot1, float wet, Undo um);
    float readWet       (const juce::ValueTree& slotTree);
    void setRange       (juce::ValueTree& s, int slot1, const juce::String& name, float min, float max, Undo um);
    void setProb        (juce::ValueTree& s, int slot1, const juce::String& name, float prob, Undo um);
    void setLocked      (juce::ValueTree& s, int slot1, const juce::String& name, bool locked, Undo um);
    void setTransition  (juce::ValueTree& s, int slot1, const juce::String& name, bool glide, Undo um);
    void setStepOn      (juce::ValueTree& s, int slot1, int step1, bool on, Undo um);
    void setStepMode    (juce::ValueTree& s, int slot1, int step1, StepMode mode, Undo um);
    void setExplicit    (juce::ValueTree& s, int slot1, int step1, const juce::String& name, float value, Undo um);
    void setMasterSeed  (juce::ValueTree& s, uint32_t masterSeed, Undo um);

    // Générer sur une sélection : chaque pas reçoit une graine tirée de (masterSeed, counter++)
    // et la densité ; passe en mode generated. Ne touche rien hors sélection.
    void generate (juce::ValueTree& s, int slot1, int firstStep1, int lastStep1, float density, Undo um);

    // Figer : les pas generated de la sélection deviennent explicit, valeurs matérialisées
    // par la fonction pure (un V par paramètre qui portait une valeur).
    void capture (juce::ValueTree& s, int slot1, int firstStep1, int lastStep1, Undo um);

    // Modulation (§3.4) et macros (§3.5)
    void setEnvelope    (juce::ValueTree& s, int slot1, const std::vector<std::pair<float, float>>& points,
                         float rate, bool sync, bool loop, const juce::String& trigger, Undo um);
    void setSource2     (juce::ValueTree& s, int slot1, const juce::String& kind, float attack, float release, Undo um);
    void addModRoute    (juce::ValueTree& s, int slot1, const juce::String& src, const juce::String& dstParam, float depth, Undo um);
    void clearModRoutes (juce::ValueTree& s, int slot1, Undo um);
    void addMacroRoute  (juce::ValueTree& s, int macro1, int slot1, const juce::String& dstParam, float lo, float hi, float curve, Undo um);
}
