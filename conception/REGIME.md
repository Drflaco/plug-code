# Régime de travail — note de production

Ce fichier sert à reprendre le geste, pas à raconter ce qui s'est passé. Il se
lit d'une traite avant une session CODE. L'histoire est dans l'ETAT et dans les
commits ; les mesures sont dans `measure/`.

---

## 1. Le geste central : figer la référence avant de toucher

**Avant toute modification dont le résultat s'entend ou se mesure, produire et
geler une référence, puis en faire un test permanent.**

Fait au J4a phase 0 : les rendus du socle ont été écrits dans
`measure/j4a/ref/` **avant** l'optimisation, et le test T13 les compare octet
par octet à chaque exécution. L'optimisation devait rendre le socle trois fois
plus rapide **sans changer un seul octet**. Les deux exigences semblaient
contradictoires ; elles ont forcé à ne retenir que des optimisations exactes —
supprimer du calcul que personne ne lit, poser d'un bloc une valeur qui ne
bouge pas. Une optimisation qui change un bit est fausse, et le test le dit.

Corollaire : **un test qui manque est un défaut**. T14 (passe-tout strict, état
par défaut) n'existait pas parce que toute la matrice partait d'un état chargé.
Écrit sur demande du pilote, il a trouvé en cinq minutes un vrai défaut de
transparence à réglage neutre.

---

## 2. Mesurer, jamais supposer

- Mesurer avant, mesurer après, avec le même instrument et le même nombre de blocs.
- Donner le percentile, pas la moyenne seule. Le p99 est le chiffre utile par
  effet ; au p99,9 on mesure déjà l'ordonnancement de Windows, pas le code.
- Un maximum isolé qui apparaît aussi sur un socle vide n'est pas un coût du code.
- Quand une mesure paraît trop belle, elle est fausse : la chaîne de référence
  affichait le coût d'un socle vide parce que le registre n'était pas peuplé et
  que neuf skills inconnues laissaient passer l'audio — comportement prévu par
  le contrat, qui ressemblait à un succès.

---

## 3. Rendre le contrat exécutable

Le contrat de skill du §3.9 était une liste en français. Il est devenu
`PlugSkillTest` : identité, aide par paramètre, classe de verrou, latence
déclarée contre latence réelle, blocs vides et irréguliers, silence après
remise à zéro, bornes, déterminisme, coût. Un binaire répond oui ou non.

C'est ce qui a permis de juger neuf skills écrites par quatre agents **sans
appréciation personnelle**, et de renvoyer un travail incomplet sans discuter.

Même principe ailleurs : le CMake refuse de configurer si le sous-module JUCE
n'est pas au commit attendu ; le script d'installation vérifie que l'empreinte
posée est celle du build. Une règle qu'on peut oublier doit devenir une règle
qui s'exécute.

---

## 4. Chercher l'invariant qui rend l'erreur impossible

Plutôt que corriger une erreur, supprimer la classe entière.

- `ParamCurves` porte `nullptr` pour toute entrée qu'une skill n'a pas déclarée.
  Lire ce qu'on n'a pas déclaré plante immédiatement, au lieu de lire en silence
  des valeurs périmées.
- L'ordre de la chaîne **est** l'indice de l'emplacement. La règle « l'automation
  ne suit pas l'effet qui déménage » (§3.2) devient une conséquence du schéma,
  pas un comportement à coder et à tester.
- Le registre des skills est tenu par l'intégrateur, jamais par auto-enregistrement
  statique : un catalogue qui dépend de l'ordre d'initialisation n'est pas un
  catalogue.
- La valeur d'un pas est une **fonction pure** de l'état. Aucune horloge, aucun
  état caché, donc le rendu déterministe est vrai par construction.

---

## 5. Quand s'arrêter et demander

Règle : **doute sur un écart assumé ou sur une décision irréversible → neutraliser
et demander.** Ne jamais inventer une convention silencieuse.

Exemple type : exposer les presets comme programmes VST3 ajoutait un paramètre
caché « Program » à ce que voit l'hôte, donc touchait la grille figée. J'ai mis
l'exposition en sommeil, documenté la mesure dans le code, et posé la question.
Le pilote a tranché « jamais ». Coût de l'arrêt : deux lignes. Coût de l'erreur :
irréversible.

À l'inverse, ne pas bloquer sur ce qui est réversible : régler, mesurer, dire
ce qu'on a choisi et pourquoi.

---

## 6. Diagnostiquer par les faits matériels

Incident du 17/09, à garder en tête comme modèle. Symptôme : « trois craquements
réguliers après chaque transitoire, plugin censé être passe-tout ». Les trois
suspects désignés étaient le compresseur, le drive et le socle.

Ce qui a tranché, dans l'ordre, sans toucher au code :
1. la section master n'est pas implémentée du tout — trois suspects éliminés ;
2. l'empreinte du binaire installé ≠ celle du binaire compilé ;
3. un fichier d'échafaudage était resté sur le disque et chargeait, à chaque
   instance, un délai de test à 50 ms et 45 % de réinjection.

Trois répétitions décroissantes espacées de 50 ms : le symptôme exact. Le code
était sain. **Avant de chercher dans le code, vérifier ce qui tourne vraiment.**

---

## 7. Travailler avec des agents

- Un agent n'écrit que dans son dossier. Le moteur, le registre, le schéma d'état
  et le CMake racine restent à l'intégrateur.
- Lui donner un dossier de build à lui, préchauffé, et un filtre de compilation
  pour qu'il ne dépende pas du travail en cours des autres.
- Lui donner comme modèle une skill **déjà intégrée et validée**, pas une
  description.
- Exiger un rapport en état : fait, testé avec les chiffres, ouvert, questions
  non tranchées. Les meilleures remontées sont venues de là — c'est un agent qui
  a vu que l'épreuve de latence ne savait pas mesurer un effet à phase linéaire.
- Intégrer un par un : contrat, build, matrice, coût, validation hôte. Ne jamais
  rustiner le travail d'un agent ; lui renvoyer les cases manquantes.

---

## 8. Échafaudages

Tout échafaudage porte sa date de péremption dans son nom, son commentaire et
l'ETAT. Il n'y a pas de honte à en poser ; il y a un danger à les oublier.

Celui encore debout : le chronomètre `PLUG_J2_TIMING`. S'y ajoute, du côté de
l'interface, l'option `PLUG_UI_TIMING` (OFF par défaut, jamais livrée) qui
chronomètre le rendu du séquenceur.

Ceux retirés : le crochet d'état J3, le crochet de latence J2 — dont l'un a
causé l'incident ci-dessus en survivant sur le disque après sa mort dans le code —
et, **au J4b étape 7, l'éditeur générique et ses deux boutons de preset**
(`PLUG_J2_GENERIC_EDITOR`), remplacés par l'interface du §3.7. Sa date de
péremption était écrite ici depuis le J2 ; elle est arrivée, il est parti.

---

## 9. Décisions à ne pas rediscuter

| Décision | Pourquoi elle est close |
| --- | --- |
| Grille de 284 paramètres, identifiants, types, drapeaux | des projets Live l'ont vue ; toute automation existante en dépend |
| Identité VST3 (Plug / Lascaux Lab / Lscx / Plug) | Live la mémorise dans ses projets |
| Aucun programme exposé à l'hôte | JUCE ajouterait un paramètre caché à la grille |
| Schéma d'état v2 : on complète, on ne purge jamais | une skill absente doit retrouver ses données si elle revient |
| Un identifiant de skill n'est jamais réutilisé, même retiré | un preset ancien doit savoir ce qu'il référence |
| Formule de composition et ses sept amendements | contrat J3 b, validé ligne à ligne |
| Les rendus de référence de `measure/j4a/ref/` | ils sont le juge de toute optimisation future |

---

## 10. Pièges d'outillage, mesurés

- **Heredoc bash pour écrire du C++** : casse les chaînes contenant `\n` et les
  accents. Utiliser les outils d'écriture de fichier, pas le shell.
- **PowerShell 5.1** lit les scripts en ANSI : tout `.ps1` accentué doit avoir
  un BOM UTF-8, sinon erreur de parsing incompréhensible.
- **Python du Microsoft Store** virtualise `%APPDATA%` : lire la copie du dépôt.
- **MSBuild** met tous les objets dans un même dossier : deux `Skill.cpp` dans
  des dossiers différents entrent en collision. Nommer par la skill.
- **CMake `CONFIGURE_DEPENDS`** ramasse les dossiers qu'un agent vient de créer,
  y compris vides : filtrer explicitement pendant qu'ils travaillent.
- **`git add -A` pendant qu'un agent écrit** produit un commit mixte. Chemins
  explicites, toujours.
- **Live verrouille le module VST3** tant qu'il tourne : fermer avant d'installer.
- **MSVC** n'ordonne pas l'évaluation des arguments : composer un message de
  journal avant l'appel, pas dedans.

---

## 11. Le rituel, à chaque clôture

1. `PlugRender test` — la matrice, y compris la non-régression du rendu.
2. `PlugSkillTest` — le contrat, si une skill a bougé.
3. `pluginval` niveau 5.
4. `scripts/install_plugin.ps1` — installe **et vérifie** l'empreinte.
5. `scripts/install_presets.ps1` — les deux formats.
6. Réécrire `plug/ETAT.md` : où on en est, fils ouverts, ne pas toucher.
7. Un commit « Rév. N — … » côté Activation, un commit côté plug-code, **push des deux**.

Un binaire non installé ne s'écoute pas. Une mesure faite sur un binaire
inconnu ne vaut rien.

---

## 12. Ce qui vient

- **J4b** — l'interface dense du §3.7, d'après `SCHEMA_INTERFACE_v0.1`. Elle
  reprend l'identité du build dans son à-propos et retire l'éditeur générique.
- **J4c** — la section master du §3.10, jamais implémentée, jamais assignée
  jusqu'au 17/09.
- **J5** — l'effet distinctif, à décider après écoute. Signalsmith Stretch est
  en sous-module, inutilisé, et c'est là qu'il se justifierait.

Le reste appartient à l'oreille du pilote : le caractère des trois gestes se
décide en écoutant, jamais sur le papier.
