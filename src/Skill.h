// Skill — le contrat qu'un module d'effet doit remplir (CdC §3.9, « contrat
// qu'un module d'effet doit remplir »), et le registre qui les connaît.
// Garantit : une skill déclare son identité stable, sa version, sa loi de mélange
// naturelle, sa latence, ses paramètres (entrée de grille, classe de verrou,
// transition par défaut, aide) et ses cas de test ; elle préalloue dans prepare()
// et n'alloue jamais dans process(). Un identifiant n'est jamais réutilisé.
// Le squelette à copier est plug-code/SKILL_TEMPLATE.md.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "GridMap.h"
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace plug::display
{
    // Mise en forme des valeurs déclarées par les skills (J4b c-2, étape 3).
    // Un seul endroit : neuf skills qui écriraient chacune leur virgule finiraient par
    // en écrire neuf façons. Français : la virgule est le séparateur décimal.

    // L'unique porte d'un littéral non ASCII d'une skill vers une juce::String.
    // juce::String(const char*) décode octet par octet (CharPointer_ASCII) : « ×2 »
    // y devient « Ã—2 ». Mesuré le 17/09 par PlugBench, qui affichait « Ã3 ».
    inline juce::String text (const char* utf8Bytes) noexcept
    {
        return juce::String::fromUTF8 (utf8Bytes);
    }

    inline juce::String num (double v, int decimals) noexcept
    {
        return juce::String (v, decimals).replaceCharacter ('.', ',');
    }

    // Deux à trois chiffres significatifs, choisis sur l'ordre de grandeur : « 12400 Hz »
    // et « 0,125 ms » se lisent ; « 12400,00 » et « 0,13 » non.
    inline juce::String sig (double v) noexcept
    {
        const double a = std::abs (v);
        if (a >= 100.0) return num (v, 0);
        if (a >= 10.0)  return num (v, 1);
        if (a >= 1.0)   return num (v, 2);
        return num (v, 3);
    }

    // Gain linéaire vers décibels, avec le seul cas que la formule ne couvre pas.
    // « -inf » et non « -∞ » : aucun caractère qui dépende d'une police absente
    // (défaut vu dans Live le 17/09).
    inline juce::String dB (double linear) noexcept
    {
        if (linear <= 0.0) return "-inf";
        return sig (20.0 * std::log10 (linear));
    }
}

namespace plug
{
    // §3.3.1 : trois classes. La classe déclarée devient le verrou initial dans l'état
    // à la première pose de la skill dans un emplacement (amendement J3-5) ; l'état
    // porte ensuite le verrou courant. Structurel : jamais par pas, jamais modulé.
    enum class LockClass { Free, LockedByDefault, Structural };

    struct SkillParamDecl
    {
        int modulable;                  // 0..12, voir grid::kModulable
        const char* label;              // nom affiché
        const char* help;               // aide au survol (§3.11) ; pour un verrou, dit pourquoi
        LockClass lockClass;
        bool glideByDefault;            // transition initiale : glissement (true) ou saut

        // J4b c-2 (ETAT Rév. 9) : la lisibilité d'un paramètre appartient à la skill,
        // elle seule sait que 0,62 se dit « 3,00 » et que l'unité est un rapport.
        // Les deux champs sont OPTIONNELS et À DÉFAUT : les neuf skills du catalogue
        // compilent sans y toucher (leur initialisation par accolades tient cinq
        // membres), et se remplissent une par une à l'étape 3. display == nullptr
        // signifie « aucun texte déclaré » : l'interface retombe sur 0,00–1,00 brut,
        // échelle des enveloppes de Live, donc corrélable à l'écran (Q3).
        const char* unit = "";                              // unité affichée après la valeur ; "" = aucune
        juce::String (*display) (float raw) = nullptr;      // texte d'une valeur brute 0..1 ; pure, sans état
    };

    struct SkillInfo
    {
        juce::String id;                // stable à jamais, jamais réutilisé (§3.9) ; ASCII
        int version = 1;
        // ENCODAGE — `label` est des OCTETS, pas une juce::String, et c'est volontaire
        // (défaut vu dans Live le 17/09). juce::String(const char*) décode octet par
        // octet (CharPointer_ASCII, juce_String.cpp:307) : « Délai » y devient « DÃ©lai »
        // en silence. En gardant const char*, la conversion ne peut plus se faire toute
        // seule — qui lit ce champ DOIT choisir juce::String::fromUTF8. C'est l'invariant
        // qui supprime la classe d'erreur au lieu de corriger ses instances (REGIME §4).
        // Même raison pour SkillParamDecl::label, ::help et ::unit.
        const char* label = "";
        MixLaw mixLaw = MixLaw::Minus3; // loi naturelle du mélange local (§3.7)
        bool factice = false;           // vrai pour les modules de test J3 : hors catalogue, hors contrat
        std::vector<SkillParamDecl> params;
    };

    // Courbes de paramètres composées par le socle, une valeur par échantillon et par
    // entrée modulable (0..12), déjà bornées dans [0,1]. La skill convertit dans son unité.
    // INVARIANT : seules les entrées que la skill DÉCLARE ont une courbe ; les autres
    // valent nullptr et ne sont pas composées (J4a phase 0). Lire une entrée non déclarée
    // est une faute de programme, pas un cas limite.
    struct ParamCurves
    {
        std::array<const float*, grid::kModulableCount> v {};
        const float* operator[] (int modulable) const noexcept { return v[(size_t) modulable]; }
    };

    class Skill
    {
    public:
        virtual ~Skill() = default;
        virtual const SkillInfo& info() const = 0;
        virtual void prepare (double sampleRate, int maxBlockSize) = 0;   // toute allocation ici
        virtual void reset() = 0;                                          // purge des lignes à retard, sans allocation
        virtual int latencySamples() const = 0;                            // déclarée, constante entre deux prepare()
        // Traite le signal traité en place. Aucune allocation, aucun verrou (§4.2).
        virtual void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int numSamples) = 0;
        // Cas numériques de la skill (§3.9) ; écrit dans log, renvoie vrai si tout passe.
        virtual bool selfTest (juce::String& log) = 0;
    };

    class SkillRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<Skill>()>;

        static SkillRegistry& instance();

        void add (const SkillInfo& info, Factory factory);
        const SkillInfo* info (const juce::String& id) const;
        std::unique_ptr<Skill> create (const juce::String& id) const;   // nullptr si inconnue (l'emplacement laisse passer, §3.9)
        std::vector<juce::String> ids() const;

    private:
        struct Entry { SkillInfo info; Factory factory; };
        std::vector<Entry> entries;
    };
}
