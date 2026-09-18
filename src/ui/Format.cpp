#include "Format.h"
#include "ViewTypes.h"

namespace plug::ui::Format
{
    using juce::String;
    using namespace plug::ui::literals;   // "…"_fr : l'unique porte UTF-8 (ViewTypes.h)

    String rawText (float raw)
    {
        return String (juce::jlimit (0.0f, 1.0f, raw), 2).replaceCharacter ('.', ',');
    }

    String valueText (float raw, const String& displayed, const String& unit)
    {
        const String body = displayed.isNotEmpty() ? displayed : rawText (raw);
        return unit.isNotEmpty() ? body + " " + unit : body;
    }

    //==========================================================================
    String lockReason (bool locked, bool lockedByDefault, bool structural, const String& help)
    {
        if (structural)
            return "Structurel, jamais par pas ni modulé"_fr + (help.isNotEmpty() ? " : " + help : String());
        if (locked && lockedByDefault)
            return "Verrouillé par la skill"_fr + (help.isNotEmpty() ? " : " + help : String());
        if (locked)
            return "Verrouillé par toi — la skill le laisse libre"_fr;
        if (lockedByDefault)
            return "Déverrouillé par toi — la skill le verrouille par défaut"_fr
                       + (help.isNotEmpty() ? " : " + help : String());
        return {};
    }

    String lockWord (bool locked, bool lockedByDefault, bool structural)
    {
        if (structural) return "structurel"_fr;
        if (locked) return lockedByDefault ? "skill"_fr : "toi"_fr;
        if (lockedByDefault) return "toi"_fr;
        return {};
    }

    //==========================================================================
    bool isReserve (const String& paramName)
    {
        return paramName == "stereo" || paramName == "res1" || paramName == "res2" || paramName == "res3";
    }

    String genericLabel (const String& paramName)
    {
        if (paramName == "mix")  return "Mix"_fr;
        if (paramName == "gain") return "Gain"_fr;
        if (paramName == "main") return "Principal"_fr;
        if (paramName.startsWith ("param") && paramName.length() == 6)
            return "Paramètre "_fr + paramName.substring (5).toUpperCase();
        if (isReserve (paramName)) return "—"_fr;   // tiret cadratin : présent dans Segoe UI et Verdana
        return paramName;
    }

    String genericHelp (const String& paramName)
    {
        if (paramName == "mix")  return "Mélange local de cet emplacement : le socle le compose toujours, "
                                        "quelle que soit la skill en place (§3.7)."_fr;
        if (paramName == "gain") return "Gain de sortie de l'emplacement : 0 = silence, 0,5 = unité, 1 = +6 dB "
                                        "(échelle linéaire de la grille)."_fr;
        if (isReserve (paramName)) return "Réserve : aucune skill ne l'occupe."_fr;
        return "Entrée générique de l'emplacement : la skill en place ne l'a pas déclarée."_fr;
    }

    bool isInertWhenUndeclared (const String& paramName)
    {
        return paramName != "main" && paramName != "mix" && paramName != "gain";
    }

    String inertLabel()
    {
        return "—"_fr;   // tiret cadratin : présent dans Segoe UI et Verdana
    }

    String inertHelp (const String& paramName)
    {
        if (isReserve (paramName)) return "Réserve : aucune skill ne l'occupe."_fr;
        if (paramName.startsWith ("param") && paramName.length() == 6)
            return "Paramètre "_fr + paramName.substring (5).toUpperCase()
                   + " : la skill en place ne le déclare pas — inerte, la bouger n'a aucun effet sur le son."_fr;
        return "Entrée non déclarée par la skill en place : inerte."_fr;
    }

    //==========================================================================
    String slotSettingLabel (const String& which)
    {
        if (which == "active") return "Actif"_fr;
        if (which == "glide")  return "Glissement"_fr;
        if (which == "fade")   return "Fondu"_fr;
        if (which == "tail")   return "Queue"_fr;
        return which;
    }

    String slotSettingHelp (const String& which)
    {
        if (which == "active") return "Contournement doux de l'emplacement : la latence ne change pas, "
                                      "le fondu adoucit l'entrée et la sortie (§3.3.2)."_fr;
        if (which == "glide")  return "Durée de glissement entre deux valeurs de pas, de 0 à 4 pas. "
                                      "Au-delà d'un pas, la valeur n'atteint plus sa cible avant la suivante."_fr;
        if (which == "fade")   return "Fondu d'activation, de 0 à 500 ms, course quadratique : "
                                      "il retire le clic à l'allumage comme à l'extinction (§3.3.2)."_fr;
        if (which == "tail")   return "Ce que devient la queue quand l'emplacement s'éteint : "
                                      "laissée mourir, ou coupée net (§3.3.2)."_fr;
        return {};
    }

    //==========================================================================
    String masterInertLabel (const String& entryId)
    {
        String label = entryId;
        if (entryId == "master.drive")             label = "Drive"_fr;
        else if (entryId == "master.tone")         label = "Tonalité"_fr;
        else if (entryId == "master.comp")         label = "Compression"_fr;
        else if (entryId == "master.lowFreq")      label = "Grave préservé"_fr;
        else if (entryId == "master.driveRouting") label = "Routage du drive"_fr;
        else if (entryId == "master.quality")      label = "Qualité"_fr;
        return label + " (J4c)"_fr;
    }

    String masterInertHelp()
    {
        return "Pas encore implémenté — J4c. L'entrée existe dans la grille, le moteur ne la lit pas : "
               "la bouger n'a aucun effet sur le son."_fr;
    }

    //==========================================================================
    String emptySlotText()
    {
        // Pas de « ▾ » ici : U+25BE n'est pas garanti dans la police par défaut de Windows,
        // et un chevron manquant se voit (défaut du 17/09). Les widgets qui en veulent un
        // le DESSINENT (PlugGlyphs.h).
        return "Emplacement vide — choisir un effet"_fr;
    }

    String unknownSkillHelp (const String& skillId, int version)
    {
        return "Effet inconnu (" + skillId + " v" + String (version) + ") : l'audio traverse, "
               "les données sont conservées (§3.9)."_fr;
    }

    //==========================================================================
    String divisionLabel (int index)
    {
        static const char* const base[5] = { "1/4", "1/8", "1/16", "1/32", "1/64" };
        static const char* const kind[3] = { "", "T", "D" };
        const int k = juce::jlimit (0, 14, index);
        return String (base[k / 3]) + kind[k % 3];
    }

    String swingText (float raw)
    {
        return String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, raw) * 100.0f)) + " %";
    }

    String seqSettingHelp (const String& which)
    {
        if (which == "length")   return "Longueur de la boucle, de 2 à 32 pas (§3.3.3). Les pas au-delà "
                                        "restent dans la grille, grisés : ils ne jouent pas."_fr;
        if (which == "division") return "Durée d'un pas : de la noire (1/4) à la quadruple croche (1/64), "
                                        "binaire, ternaire (T) ou pointée (D)."_fr;
        if (which == "swing")    return "Swing : le second pas de chaque paire recule, jusqu'au triolet "
                                        "à 100 %. Il s'applique à toute la grille (§3.3.3)."_fr;
        return {};
    }
}
