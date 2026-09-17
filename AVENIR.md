# À venir

Ce que la v1 de l'interface ne fait pas, et l'assume. La liste est embarquée dans
le binaire (`juce_add_binary_data`) et s'affiche dans l'espace préférences (§3.11) :
une absence annoncée n'est pas un défaut, une absence tue en est un.

Tenue à jour avec la pièce `plug/` du dépôt Activation (ETAT Rév. 9, réponse Q5
du pilote du 17/09). La v1 est une v1.

## Interface

- **Enveloppe superposée au séquenceur** — dessiner la modulation par-dessus les
  pas. La v1 montre la valeur de chaque pas, calculée par la fonction pure, pas la
  valeur composée vivante.
- **Édition des routes de modulation** — les routes existent dans l'état et le
  moteur les applique ; la v1 les affiche en lecture seule.
- **Édition des routes de macro** — mêmes conditions : huit knobs fonctionnels,
  leurs destinations en aide au survol, pas d'éditeur.
- **Bouton « Aléatoire » de la barre (§3.7)** — un tirage global, distinct de
  « Générer » qui travaille sur une sélection de pas.
- **Thème** — la v1 a un seul habillage. Le `LookAndFeel` est isolé pour qu'un
  thème n'oblige pas à toucher les widgets.

## Moteur

- **Section master (§3.10) — jalon J4c.** Le moteur n'applique que `master.mix`,
  `master.mixLaw` et `master.volume`. Drive, tonalité, compression, grave
  préservé, routage du drive et qualité existent dans la grille figée et ne sont
  lus par personne : l'interface les affiche grisés, marqués « (J4c) ».
- **Compensation de latence** dans la vue, et **qualité par défaut** — inertes
  tant que J4c n'a pas eu lieu.
- **`master.quality`** n'est pas transmis aux skills : `core.drive` s'y
  brancherait par l'ordre de son anti-repliement.
- **Relâchement du détecteur de `core.gate`** — fixe à 3 ms, non exposé.
- **Stéréo générique** — l'entrée `stereo` d'un emplacement n'a de sens que pour
  les skills qui la déclarent ; ailleurs elle reste une réserve.

## Écarts assumés du catalogue

- `core.drive` sans suréchantillonnage (−1,25 dB à 8 kHz).
- `core.grain` à 50 ms pour garder la taille de grain modulable par pas.
- Taille de réverbe verrouillée par défaut — réglage, pas structure.
