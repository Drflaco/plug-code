# À venir

Ce que la v1 de l'interface ne fait pas, et l'assume. La liste est embarquée dans
le binaire (`juce_add_binary_data`) et s'affiche dans l'espace préférences (§3.11) :
une absence annoncée n'est pas un défaut, une absence tue en est un.

Tenue à jour à chaque version, avec la pièce `plug/` du dépôt Activation
(ETAT Rév. 9, réponse Q5 du pilote du 17/09). La v1 est une v1.

## Interface

- **Édition des routes de macro** — les huit macros sont fonctionnelles et leurs
  routes s'affichent en aide au survol ; les poser et les régler demande une
  interface à part. *J5.*
- **Éditeur d'enveloppe superposé au séquenceur** — dessiner la modulation
  par-dessus les pas. La v1 montre la valeur de chaque pas, calculée par la
  fonction pure, jamais la valeur composée vivante. *J5.*
- **Édition des routes de modulation** — les routes existent dans l'état et le
  moteur les applique ; la v1 les affiche en lecture seule. *J5.*
- **Bouton « Aléatoire » de la barre (§3.7)** — un tirage global, distinct de
  « Générer » qui travaille sur une sélection de pas. *J5.*
- **Thème** — la v1 a un seul habillage. Le `LookAndFeel` est isolé pour qu'un
  thème n'oblige pas à toucher les widgets. *Décision ouverte.*
- **Partage contrôles / édition réglable** — le partage séquenceur / inspecteur
  bascule (préférences) ; celui des contrôles est figé à 34 %. *Décision ouverte.*
- **Nom du preset conservé par le projet** — il vit dans la session, donc un Set
  rouvert affiche « — ». Le conserver demanderait un attribut dans `PlugState`,
  et le schéma v2 ne se retouche pas pour un confort d'affichage.
  *Question portée au CdC 0.4.*

## Moteur

- **Section master (§3.10)** — le moteur n'applique que `master.mix`,
  `master.mixLaw` et `master.volume`. Drive, tonalité, compression, grave
  préservé, routage du drive et qualité existent dans la grille figée et ne sont
  lus par personne : l'interface les affiche grisés, marqués « (J4c) ». *J4c.*
- **Compensation de latence** dans la vue — affichée, inerte. *J4c.*
- **Qualité par défaut** pour les nouveaux presets — affichée, inerte. *J4c.*
- **`master.quality` transmis aux skills** — `core.drive` s'y brancherait par
  l'ordre de son anti-repliement. *J4c.*
- **Relâchement du détecteur de `core.gate`** — fixe à 3 ms, non exposé. *J5.*
- **Stéréo générique** — l'entrée `stereo` d'un emplacement n'a de sens que pour
  les skills qui la déclarent ; ailleurs elle reste une réserve. *Décision ouverte.*
- **Loi de mélange par défaut** (préférence) — réservée : en v1 chaque preset
  porte sa loi, la préférence ne s'applique nulle part. *J4c.*

## Écarts assumés du catalogue

- `core.drive` sans suréchantillonnage (−1,25 dB à 8 kHz).
- `core.grain` à 50 ms pour garder la taille de grain modulable par pas.
- Taille de réverbe verrouillée par défaut — réglage, pas structure.
