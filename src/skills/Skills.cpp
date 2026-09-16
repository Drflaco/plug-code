#include "Skills.h"
#include "../Skill.h"

// Chaque famille expose une seule fonction d'enregistrement, déclarée ici.
// L'intégrateur ajoute la ligne ; l'auteur de la skill ne touche pas à ce fichier.
namespace plug::skills
{
   #if PLUG_SKILL_filter
    void registerFilter();
   #endif
   #if PLUG_SKILL_gain
    void registerGain();
   #endif
   #if PLUG_SKILL_gate
    void registerGate();
   #endif
   #if PLUG_SKILL_fm
    void registerFm();
   #endif
   #if PLUG_SKILL_drive
    void registerDrive();
   #endif
   #if PLUG_SKILL_delay
    void registerDelay();
   #endif
   #if PLUG_SKILL_reverb
    void registerReverb();
   #endif
   #if PLUG_SKILL_grain
    void registerGrain();
   #endif
   #if PLUG_SKILL_repitch
    void registerRepitch();
   #endif
}

namespace plug
{
    void registerAllSkills()
    {
        static bool done = false;
        if (done) return;
        done = true;

       #if PLUG_SKILL_filter
        skills::registerFilter();
       #endif
       #if PLUG_SKILL_gain
        skills::registerGain();
       #endif
       #if PLUG_SKILL_gate
        skills::registerGate();
       #endif
       #if PLUG_SKILL_fm
        skills::registerFm();
       #endif
       #if PLUG_SKILL_drive
        skills::registerDrive();
       #endif
       #if PLUG_SKILL_delay
        skills::registerDelay();
       #endif
       #if PLUG_SKILL_reverb
        skills::registerReverb();
       #endif
       #if PLUG_SKILL_grain
        skills::registerGrain();
       #endif
       #if PLUG_SKILL_repitch
        skills::registerRepitch();
       #endif
    }
}
