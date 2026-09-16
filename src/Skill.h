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
#include <functional>
#include <memory>
#include <vector>

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
    };

    struct SkillInfo
    {
        juce::String id;                // stable à jamais, jamais réutilisé (§3.9)
        int version = 1;
        juce::String label;
        MixLaw mixLaw = MixLaw::Minus3; // loi naturelle du mélange local (§3.7)
        bool factice = false;           // vrai pour les modules de test J3 : hors catalogue, hors contrat
        std::vector<SkillParamDecl> params;
    };

    // Courbes de paramètres composées par le socle, une valeur par échantillon et par
    // entrée modulable (0..12), déjà bornées dans [0,1]. La skill convertit dans son unité.
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
