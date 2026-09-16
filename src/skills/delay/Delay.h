// Delay — point d'entrée d'enregistrement de la skill « core.delay » (CdC §3.9).
// Garantit : une famille = une fonction exposée, appelée par src/skills/Skills.cpp.
// Invariant : l'auteur de la skill ne touche pas à la liste du catalogue ; il ne
// fournit que cette déclaration, pour que l'ordre d'initialisation statique n'entre
// jamais en jeu (raison donnée dans src/skills/Skills.h).
#pragma once

namespace plug::skills
{
    void registerDelay();
}
