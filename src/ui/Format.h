// Format — le texte de l'interface, pur (J4b b), ETAT Rév. 9).
// Invariants et leur raison :
//   · aucune dépendance au moteur, à l'état ni au contrat de skill : ces fonctions
//     ne prennent que des valeurs déjà lues, elles sont donc testables seules et
//     survivent à une v2 de l'interface ;
//   · repli d'affichage 0,00–1,00 à la VIRGULE française : c'est l'échelle que Live
//     affiche dans ses enveloppes, donc la seule qui reste corrélable à l'écran (Q3) ;
//   · les libellés génériques vivent ici et nulle part ailleurs : un widget n'écrit
//     jamais « Mix » en dur, sinon la v2 les réécrirait un par un.
#pragma once
#include <juce_core/juce_core.h>

namespace plug::ui::Format
{
    // Repli : deux décimales, virgule française. « 0,62 ».
    juce::String rawText (float raw);

    // Texte complet d'une valeur : ce que la skill déclare (display + unit), ou le repli.
    juce::String valueText (float raw, const juce::String& displayed, const juce::String& unit);

    // Le « pourquoi » du cadenas (§3.3.1, §3.11), quatre cas et pas un de plus.
    // `help` est l'aide déclarée par la skill — le contrat exige qu'elle dise la raison.
    juce::String lockReason (bool locked, bool lockedByDefault, bool structural, const juce::String& help);

    // Le mot court posé à côté du cadenas : « skill », « toi », « structurel », ou rien.
    juce::String lockWord (bool locked, bool lockedByDefault, bool structural);

    // Libellé d'un paramètre d'emplacement qu'aucune skill ne déclare.
    // mix / gain : le socle les compose toujours, ils ont un sens partout.
    // stereo / res1..3 : réserve, « — », inerte.
    // paramA..F : « Paramètre A » … « Paramètre F » — jamais l'identifiant de grille.
    juce::String genericLabel (const juce::String& paramName);
    juce::String genericHelp  (const juce::String& paramName);
    bool isReserve (const juce::String& paramName);   // vrai pour stereo, res1..3 non déclarés

    // Réglages d'emplacement, jamais des valeurs de pas (§3.3.2).
    juce::String slotSettingLabel (const juce::String& which);   // active, glide, fade, tail
    juce::String slotSettingHelp  (const juce::String& which);

    // Entrée du master que le moteur ne lit pas encore (§3.10) : libellé + « (J4c) ».
    juce::String masterInertLabel (const juce::String& entryId);
    juce::String masterInertHelp();

    // Emplacement vide, et skill absente du registre (§3.9).
    juce::String emptySlotText();
    juce::String unknownSkillHelp (const juce::String& skillId, int version);
}
