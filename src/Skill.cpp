#include "Skill.h"

namespace plug
{
    SkillRegistry& SkillRegistry::instance()
    {
        static SkillRegistry r;   // construit au premier appel : pas d'ordre d'initialisation à gérer
        return r;
    }

    void SkillRegistry::add (const SkillInfo& info, Factory factory)
    {
        // Un identifiant n'est jamais réutilisé (§3.9) : une seconde déclaration est une erreur de programme.
        for (const auto& e : entries)
            if (e.info.id == info.id) { jassertfalse; return; }
        entries.push_back ({ info, std::move (factory) });
    }

    const SkillInfo* SkillRegistry::info (const juce::String& id) const
    {
        for (const auto& e : entries)
            if (e.info.id == id) return &e.info;
        return nullptr;
    }

    std::unique_ptr<Skill> SkillRegistry::create (const juce::String& id) const
    {
        for (const auto& e : entries)
            if (e.info.id == id) return e.factory();
        return nullptr;
    }

    std::vector<juce::String> SkillRegistry::ids() const
    {
        std::vector<juce::String> out;
        for (const auto& e : entries) out.push_back (e.info.id);
        return out;
    }
}
