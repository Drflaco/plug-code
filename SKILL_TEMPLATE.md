# SKILL_TEMPLATE — squelette d'un module d'effet (CdC §3.9, contrat)

Ce fichier est ce que lit le prochain qui ajoute un effet. Une skill est un
module source enregistré dans le registre ; l'ajouter = écrire le module,
l'enregistrer, recompiler (§3.9, « mode d'ajout retenu »). Rien ne se charge à
chaud. La grille de paramètres ne change **jamais** : la skill se branche sur les
13 entrées modulables d'un emplacement (`main`, `paramA..F`, `mix`, `gain`,
`stereo`, `res1..3`, voir `src/GridMap.h`), et c'est tout.

Règle de code (contrat J3-7) : en-tête de trois à six lignes, invariants nommés
avec leur raison, décisions renvoyées au CdC (§) ou à l'ETAT (Rév. N), pas de
commentaire qui paraphrase, français.

## Contrat à remplir (§3.9) — cocher chaque ligne avant d'enregistrer

- [ ] **Identité stable** : `id` en minuscules pointées (`famille.nom`), jamais
      réutilisé, même retiré ; `version` entière incrémentée à chaque changement
      de comportement sonore.
- [ ] **Paramètres déclarés** : pour chacun, l'entrée modulable qu'il occupe, son
      libellé, son **aide au survol** (§3.11 : ce qu'il fait, unité, plage ; pour
      un verrou, **pourquoi**), sa **classe de verrou** (§3.3.1 : libre /
      verrouillé par défaut / structurel) et sa **transition par défaut** (saut ou
      glissement). La classe déclarée devient le verrou initial dans l'état à la
      première pose (amendement J3-5).
- [ ] **Latence déclarée** ou zéro, constante entre deux `prepare()`. Un
      paramètre structurel qui la change (taille de FFT, suréchantillonnage)
      n'est jamais modulé et se règle hors passe.
- [ ] **Préallocation** : tout dans `prepare()`, rien dans `process()` — ni
      allocation, ni verrou, ni disque, ni graphique (§4.2).
- [ ] **Blocs de taille variable, irréguliers ou vides** acceptés.
- [ ] **Loi de mélange naturelle** (§3.7) : `Minus6` si le traité est en phase
      avec le sec (filtres, gain), `Minus3` sinon (réverbe, délai, granulaire),
      `Zero` si le sec doit rester entier jusqu'à mi-course.
- [ ] **Cas de test numériques** dans `selfTest()` : au moins un cas par
      paramètre déclaré, vérifiable sans oreille (impulsion, sinus, silence).
- [ ] **Coût par bloc régulier** (annexe A.0) : pas de paquet spectral tous les N
      blocs sans l'étaler ; mesurer avec PlugBench, recalibrer le budget §4.4.
- [ ] `factice = false`. Les modules `factice.*` du J3 ne remplissent pas ce
      contrat et n'entrent pas au catalogue.

## Squelette (copier dans `src/skills/<Nom>.cpp`)

```cpp
// <Nom> — <ce que fait l'effet en une ligne> (CdC §3.8, geste <n>).
// Garantit : <latence>, <loi de mélange>, <ce qui est structurel et pourquoi>.
#include "../Skill.h"

namespace plug::skills
{
    class <Nom> : public Skill
    {
    public:
        const SkillInfo& info() const override
        {
            static const SkillInfo i {
                "<famille>.<nom>",            // id : stable à jamais
                1,                            // version
                "<Libellé>",
                MixLaw::Minus3,               // loi naturelle
                false,                        // factice : non
                {
                    // { entrée modulable, libellé, aide au survol, classe de verrou, glissement par défaut }
                    { 0, "<Main>",   "<Ce qu'il fait, unité, plage.>",                       LockClass::Free,            true  },
                    { 1, "<Param A>","<…>",                                                  LockClass::Free,            false },
                    { 2, "<Pitch>",  "<Verrouillé : chaque saut resynchronise le pitch-shift (§3.3.1).>",
                                                                                            LockClass::LockedByDefault, true  },
                    { 3, "<FFT>",    "<Structurel : change la latence déclarée ; réglé hors passe.>",
                                                                                            LockClass::Structural,      false },
                }
            };
            return i;
        }

        void prepare (double sampleRate, int maxBlockSize) override
        {
            // Toute allocation ici. Dimensionner au pire cas (maxBlockSize, latence max).
        }

        void reset() override
        {
            // Purger les mémoires (lignes à retard, filtres) sans allouer.
        }

        int latencySamples() const override { return 0; }   // ou la valeur mesurée, jamais devinée

        void process (juce::AudioBuffer<float>& wet, const ParamCurves& p, int numSamples) override
        {
            // p[m][i] : valeur composée du paramètre m à l'échantillon i, déjà dans [0,1].
            // Convertir ici dans l'unité DSP (Hz, ms, dB) — c'est la dernière étape de la
            // formule de composition (contrat J3 b, étape 7).
            // Aucune allocation, aucun verrou.
        }

        bool selfTest (juce::String& log) override
        {
            // Cas numériques : impulsion → réponse attendue ; silence → silence ; etc.
            // Écrire chaque cas dans log avec [OK]/[FAIL], renvoyer vrai si tout passe.
            return true;
        }
    };
}
```

## Enregistrement

Dans le registre (voir `src/dummies/DummySkills.cpp` pour la forme) :

```cpp
SkillRegistry::instance().add (<Nom>().info(), [] { return std::make_unique<<Nom>>(); });
```

Puis : recompiler, lancer `PlugRender test` (le socle ne doit pas bouger) et
`PlugBench` (le budget §4.4 se recalibre). Une skill retirée du registre laisse
son `id` réservé et un preset qui la référence passe en « effet inconnu » :
l'emplacement laisse passer l'audio et garde ses données (§3.9).
