// Skills — le point d'enregistrement du catalogue (CdC §3.8, §3.9).
// Garantit : une seule liste, tenue par l'intégrateur ; une skill s'ajoute en
// écrivant son module dans src/skills/<nom>/ puis une ligne ici. Aucune skill
// ne s'enregistre toute seule : l'ordre d'initialisation statique ne se
// contrôle pas, et un catalogue qui dépend de l'éditeur de liens n'est pas un
// catalogue. Les identifiants retirés restent réservés à jamais (§3.9).
#pragma once

namespace plug
{
    // Enregistre tout le catalogue dans le registre global. Idempotent.
    void registerAllSkills();
}
