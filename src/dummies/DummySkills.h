// DummySkills — modules FACTICES du socle J3 (contrat J3, « effets factices »).
// Garantit : trois modules juste assez réels pour prouver la chaîne, le
// séquenceur, la latence et l'état : un gain, un délai court, un passe-tout à
// latence non nulle. Ils sont marqués factice=true, n'entrent pas au catalogue
// (§3.8) et ne remplissent pas le contrat de skill (§3.9) : pas d'aide au
// survol soignée, pas de qualité sonore. Ils disparaissent quand les vraies
// skills arrivent (J4) ; leurs identifiants restent réservés à jamais.
#pragma once
#include "../Skill.h"

namespace plug::dummies
{
    inline const juce::String kGainId   { "factice.gain" };
    inline const juce::String kDelayId  { "factice.delay" };
    inline const juce::String kLatentId { "factice.latent" };

    constexpr int kMaxDummyDelay = 4800;   // 100 ms à 48 kHz : borne du délai factice
    constexpr int kLatentLatency = 256;    // latence déclarée ET réelle du passe-tout latent

    void registerAll();                     // idempotent
    bool selfTestAll (juce::String& log);   // cas numériques des trois modules
}
