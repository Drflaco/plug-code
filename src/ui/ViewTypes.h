// ViewTypes — ce qu'une vue a besoin de savoir, et rien de plus (J4b a), ETAT Rév. 9).
// Invariants et leur raison :
//   · des VALEURS, jamais une référence à l'arbre d'état : un widget qui tient un
//     ValueTree finit par l'éditer, et la séparation du §5 (l'interface édite une
//     représentation, elle ne calcule rien) redeviendrait un principe au lieu d'une
//     contrainte — l'exigence pilote du 17/09 est qu'une v2 remplace la v1 sans
//     toucher au moteur ni à l'état ;
//   · aucun juce::Component ici : ce fichier survit à toutes les versions de l'interface ;
//   · le vocabulaire est celui de l'écran (libellé, unité, texte, raison d'un verrou),
//     jamais celui de la grille (slot03.paramB) ni du contrat de skill (LockClass).
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace plug::ui::literals
{
    // L'UNIQUE porte d'entrée d'un littéral français vers une juce::String.
    //
    // juce::String(const char*) décode OCTET PAR OCTET (CharPointer_ASCII,
    // juce_String.cpp:307) : « Chargé » devient « ChargÃ© », en silence, en Release.
    // C'est le défaut que le pilote a vu dans Live le 17/09. /utf-8 garantit que les
    // octets du littéral SONT de l'UTF-8 ; ce suffixe garantit qu'on les DÉCODE comme
    // tels. Les deux moitiés sont nécessaires, aucune ne suffit seule.
    //
    // Règle de src/ui/ : tout littéral portant un caractère non ASCII s'écrit "…"_fr.
    inline juce::String operator"" _fr (const char* bytes, size_t length)
    {
        return juce::String::fromUTF8 (bytes, (int) length);
    }

    // Même porte, pour les octets déclarés par une skill (SkillInfo::label,
    // SkillParamDecl::label / help / unit) : des const char*, jamais des String.
    inline juce::String fr (const char* bytes)
    {
        return bytes != nullptr ? juce::String::fromUTF8 (bytes) : juce::String();
    }
}

namespace plug::ui
{
    constexpr int kSlots = 16;
    constexpr int kSteps = 32;
    constexpr int kSlotParams = 13;   // main, paramA..F, mix, gain, stereo, res1..3
    constexpr int kMacros = 8;

    // Mode d'un pas tel qu'il s'affiche : base · généré · figé (§3.3).
    enum class StepModeView { Base, Generated, Explicit };

    // Un paramètre d'emplacement, prêt à dessiner.
    struct ParamView
    {
        juce::String name;              // nom interne du paramètre (main, paramA…), pour renvoyer une commande
        int index = 0;                  // 0..12, sa place parmi les entrées modulables : porte sa COULEUR (6a)
        bool modulable = true;          // une valeur par pas est possible
        bool declared = false;          // la skill en place l'a déclaré : libellé, aide et unité sont les siens
        juce::String label;             // « Rapport », ou le générique de Format.h
        juce::String unit;
        juce::String help;
        juce::String valueText;         // texte prêt à afficher (display + unité, ou 0,00–1,00)
        float raw = 0.0f;               // valeur affichée : celle du pas sélectionné (c-6)
        float base = 0.0f;              // base PARAM de l'emplacement
        float min = 0.0f, max = 1.0f;   // plage de génération
        float effLo = 0.0f, effHi = 1.0f;   // plage rétrécie par la densité : le halo
        float prob = 1.0f;
        bool locked = false;
        bool structural = false;        // jamais par pas, jamais modulé : cadenas non cliquable
        bool lockedByDefault = false;   // classe déclarée par la skill
        juce::String lockReason;        // le « pourquoi » du cadenas (§3.3.1, §3.11)
        bool glide = false;             // transition : glissement (vrai) ou saut
        StepModeView stepMode = StepModeView::Base;
        bool inert = false;             // réserve qu'aucune skill n'occupe : grisé, sans halo
    };

    struct SlotView
    {
        int slot1 = 1;
        juce::String skillId;
        juce::String skillLabel;
        int skillVersion = 0;           // celle enregistrée dans l'état, pas celle du registre
        juce::String mixLaw;            // loi naturelle de la skill, en toutes lettres : « -3 dB »
        bool present = false;           // un effet est posé
        bool unknown = false;           // posé mais absent du registre (§3.9) : l'audio traverse
        bool active = true;
        bool tailRing = true;           // queue laissée mourir (vrai) ou coupée
        float glide = 0.0f;
        float fade = 0.0f;
        std::array<ParamView, kSlotParams> params {};
        bool hasPattern = false;        // la ligne porte un motif (c-8)
        bool hostDriven = false;        // des valeurs sont arrivées de l'hôte (heuristique d-1)
        // Pas de `latency` ici : aucun widget des sept étapes n'en a besoin, et ni le
        // registre ni le moteur ne déclarent la latence PAR emplacement (décision
        // pilote du 17/09). Un champ qui vaudrait toujours 0 mentirait.
    };

    struct StepView
    {
        bool on = true;
        StepModeView mode = StepModeView::Base;
        float value = 0.0f;             // valeur affichée du pas pour le paramètre montré
        bool hasValue = false;          // le pas porte une valeur propre (généré ou figé)
    };

    struct LineView
    {
        int slot1 = 1;
        std::array<StepView, kSteps> steps {};
        int length = 16;                // longueur de boucle courante (seq.length)
        bool modeB = false;             // mode A/B de CETTE ligne : mémoire d'instance, hors preset (Q7)
        juce::String shownParam { "main" };   // le paramètre dessiné en mode B
        int shownIndex = 0;             // le même, en indice 0..12 : sa couleur (6a)
    };

    struct TransportView
    {
        int step = -1;
        bool playing = false;
        bool freeRunning = false;       // bandeau « 120 BPM de secours » (§3.3.3)
    };

    // Les trois réglages de la grille (§3.3.3 : longueur, division, swing) — des
    // paramètres de l'état, automatisables, que le bandeau du séquenceur règle
    // (correction 3 de la phase 2, 18/09). Le widget reçoit des valeurs LISIBLES
    // (pas, indice de division, libellés) ; les conversions vers la grille 0..1
    // restent dans la couche de présentation.
    struct SequencerView
    {
        int length = 16;                // 2..32 pas
        int lengthMin = 2, lengthMax = 32;
        int division = 6;               // indice dans divisionChoices (6 = 1/16)
        juce::StringArray divisionChoices;   // « 1/4 », « 1/4T », « 1/4D », … « 1/64D »
        float swing = 0.0f;             // 0..1, échelle de la grille
        juce::String swingText;         // « 0 % » … « 100 % »
    };

    struct MasterEntryView
    {
        juce::String id;
        juce::String label;
        float raw = 0.0f;
        bool inert = false;             // §3.10 non implémenté : libellé + « (J4c) », grisé
        juce::String help;
        // Les libellés d'une entrée à CHOIX, tels que la grille les déclare à l'hôte.
        // Ils traversent la couche au lieu d'être redits par un widget : la vérité
        // reste dans ParameterGrid.cpp, que la frontière v1/v2 interdit d'inclure.
        juce::StringArray choices;
    };

    struct MasterView
    {
        float volume = 0.5f;
        float mix = 1.0f;
        int law = 1;                    // 0 = -6 dB, 1 = -3 dB, 2 = 0 dB
        // Textes composés par la couche de présentation : un widget ne convertit rien,
        // et surtout pas une valeur de grille en décibels (il ne connaît pas GridMap).
        juce::String volumeText, mixText;
        juce::StringArray lawChoices;   // « -6 dB », « -3 dB », « 0 dB » — depuis la grille
        std::vector<MasterEntryView> inertEntries;   // drive, tone, comp, grave, routage, qualité
    };

    struct MacroView
    {
        int macro1 = 1;
        float value = 0.0f;
        juce::String routes;            // routes en texte, lecture seule (Q2) ; édition : « À venir »
    };

    struct AboutSkillView
    {
        juce::String id, label;
        int version = 1;
        juce::String mixLaw;            // loi naturelle déclarée par la skill (§3.7)
        int latency = 0;                // latence déclarée à 48 kHz
        bool latencyKnown = false;      // faux = on ne l'a pas su : on n'écrit rien plutôt que zéro
    };

    struct AboutView
    {
        juce::String buildStamp;    // la ligne complète : version, commit, date de compilation
        juce::String shortStamp;    // « Plug 0.3.0 · commit 484061a » — ce qui tient dans la barre
        juce::String juceVersion;
        std::vector<AboutSkillView> skills;
    };

    struct PrefsView
    {
        bool ratioTwoThirds = true;     // partage de la fenêtre : 2/3 (vrai) ou 1/3
        double zoom = 1.0;
        bool hoverHelp = true;
        int helpDelayMs = 700;
        int defaultMixLaw = 1;
        int shownSlots = 10;            // 10 par défaut, 16 au plus (Q6)
    };

    struct PresetView
    {
        juce::String currentName;   // vide tant que rien n'a été chargé ni enregistré
        bool modified = false;      // l'état a bougé depuis : la barre affiche « geste1 * »
        juce::String folder;
        juce::StringArray names;    // la bibliothèque : dossier utilisateur d'abord, livré ensuite
        int currentIndex = -1;      // entrée de la bibliothèque correspondant au nom courant, -1 sinon
    };

    // De quoi est faite une sélection de pas (c-7) : c'est ce compte qui arme
    // « Figer » et qui écrit l'avertissement de re-dérivation AVANT le clic (d-2).
    struct StepCountsView
    {
        int generated = 0;
        int explicitCount = 0;
        int off = 0;
        int total = 0;          // pas dans la sélection, bornes comprises
    };

    // L'état de génération : graine maîtresse, compteur de tirages, densité courante.
    struct GenerationView
    {
        juce::int64 masterSeed = 0;
        int counter = 0;
        float density = 1.0f;
    };

    // Ce qu'un bouton Annuler / Refaire a besoin de savoir : s'il est vivant, et le
    // NOM de la transaction — c'est ce nom qui rend la granularité visible (§3.11).
    struct UndoView
    {
        bool canUndo = false;
        bool canRedo = false;
        juce::String undoName;
        juce::String redoName;
    };

    //==========================================================================
    // Ce qui a bougé depuis la dernière notification. Le Presenter coalesce tout
    // un tour de boucle en UN masque : un widget relit sa View, il ne suit pas
    // les propriétés une à une.
    struct ViewMask
    {
        enum : juce::uint32
        {
            None       = 0,
            Slots      = 1u << 0,   // identité, présence, ordre des effets
            Params     = 1u << 1,   // bases, plages, verrous, transitions
            Line       = 1u << 2,   // pas, modes, valeurs figées
            Master     = 1u << 3,
            Macros     = 1u << 4,
            Generation = 1u << 5,   // graine maîtresse, compteur, densité
            Sequencer  = 1u << 6,   // longueur, division, swing
            Presets    = 1u << 7,
            Prefs      = 1u << 8,
            Session    = 1u << 9,   // sélection, mode A/B, paramètre montré : hors état
            Hover      = 1u << 10,  // case survolée dans le séquenceur (6b) : l'inspecteur seul la suit
            All        = 0xffffffffu
        };

        juce::uint32 bits = None;
        juce::uint16 slots = 0;     // emplacements concernés, bit 0 = emplacement 1

        bool has (juce::uint32 b) const noexcept { return (bits & b) != 0; }
        bool hasSlot (int slot1) const noexcept
        {
            return slot1 >= 1 && slot1 <= kSlots && (slots & (juce::uint16) (1u << (slot1 - 1))) != 0;
        }
    };
}
