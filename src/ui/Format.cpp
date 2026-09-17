#include "Format.h"

namespace plug::ui::Format
{
    using juce::String;

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
            return "Structurel, jamais par pas ni modulé" + (help.isNotEmpty() ? " : " + help : String());
        if (locked && lockedByDefault)
            return "Verrouillé par la skill" + (help.isNotEmpty() ? " : " + help : String());
        if (locked)
            return "Verrouillé par toi — la skill le laisse libre";
        if (lockedByDefault)
            return "Déverrouillé par toi — la skill le verrouille par défaut"
                       + (help.isNotEmpty() ? " : " + help : String());
        return {};
    }

    String lockWord (bool locked, bool lockedByDefault, bool structural)
    {
        if (structural) return "structurel";
        if (locked) return lockedByDefault ? "skill" : "toi";
        if (lockedByDefault) return "toi";
        return {};
    }

    //==========================================================================
    bool isReserve (const String& paramName)
    {
        return paramName == "stereo" || paramName == "res1" || paramName == "res2" || paramName == "res3";
    }

    String genericLabel (const String& paramName)
    {
        if (paramName == "mix")  return "Mix";
        if (paramName == "gain") return "Gain";
        if (paramName == "main") return "Principal";
        if (paramName.startsWith ("param") && paramName.length() == 6)
            return "Paramètre " + paramName.substring (5).toUpperCase();
        if (isReserve (paramName)) return "—";
        return paramName;
    }

    String genericHelp (const String& paramName)
    {
        if (paramName == "mix")  return "Mélange local de cet emplacement : le socle le compose toujours, "
                                        "quelle que soit la skill en place (§3.7).";
        if (paramName == "gain") return "Gain de sortie de l'emplacement : 0 = silence, 0,5 = unité, 1 = +6 dB "
                                        "(échelle linéaire de la grille).";
        if (isReserve (paramName)) return "Réserve : aucune skill ne l'occupe.";
        return "Entrée générique de l'emplacement : la skill en place ne l'a pas déclarée.";
    }

    //==========================================================================
    String slotSettingLabel (const String& which)
    {
        if (which == "active") return "Actif";
        if (which == "glide")  return "Glissement";
        if (which == "fade")   return "Fondu";
        if (which == "tail")   return "Queue";
        return which;
    }

    String slotSettingHelp (const String& which)
    {
        if (which == "active") return "Contournement doux de l'emplacement : la latence ne change pas, "
                                      "le fondu adoucit l'entrée et la sortie (§3.3.2).";
        if (which == "glide")  return "Durée de glissement entre deux valeurs de pas, de 0 à 4 pas. "
                                      "Au-delà d'un pas, la valeur n'atteint plus sa cible avant la suivante.";
        if (which == "fade")   return "Fondu d'activation, de 0 à 500 ms, course quadratique : "
                                      "il retire le clic à l'allumage comme à l'extinction (§3.3.2).";
        if (which == "tail")   return "Ce que devient la queue quand l'emplacement s'éteint : "
                                      "laissée mourir, ou coupée net (§3.3.2).";
        return {};
    }

    //==========================================================================
    String masterInertLabel (const String& entryId)
    {
        String label = entryId;
        if (entryId == "master.drive")             label = "Drive";
        else if (entryId == "master.tone")         label = "Tonalité";
        else if (entryId == "master.comp")         label = "Compression";
        else if (entryId == "master.lowFreq")      label = "Grave préservé";
        else if (entryId == "master.driveRouting") label = "Routage du drive";
        else if (entryId == "master.quality")      label = "Qualité";
        return label + " (J4c)";
    }

    String masterInertHelp()
    {
        return "Pas encore implémenté — J4c. L'entrée existe dans la grille, le moteur ne la lit pas : "
               "la bouger n'a aucun effet sur le son.";
    }

    //==========================================================================
    String emptySlotText()
    {
        return "Emplacement vide — choisir un effet \xe2\x96\xbe";
    }

    String unknownSkillHelp (const String& skillId, int version)
    {
        return "Effet inconnu (" + skillId + " v" + String (version) + ") : l'audio traverse, "
               "les données sont conservées (§3.9).";
    }
}
