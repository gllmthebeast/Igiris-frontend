# src/platform — l'adaptateur de plateforme

**Le seul endroit du dépôt autorisé à contenir des chaînes spécifiques à une
distribution** : `/userdata/`, `batocera`, `emulatorlauncher.py`, `runcommand.sh`…

Partout ailleurs dans `src/` et `qml/`, ces chaînes font échouer le test
`no-distro-literals` (`tools/check-no-distro-literals.sh`), conformément au §1 du
`CLAUDE.md`.

L'interface expose **exactement quatre** choses, et rien d'autre :

1. localiser le fichier de description des systèmes ;
2. résoudre une `platform_key` du catalogue vers le nom de système local ;
3. localiser les dossiers de ROMs ;
4. lancer — construire et exécuter la commande, substitution de `%ROM%`.

Plus la **déclaration de capacités** : une distribution qui ne sait pas faire quelque chose
le dit, et l'interface s'adapte au lieu de planter.

> Le contenu arrive au **lot 1**. Ce répertoire existe dès le lot 0 parce que le test CI
> s'appuie sur son existence pour délimiter la zone autorisée.

Rappel du §13 : ajouter une seconde distribution (Recalbox, lot 9) **ne doit toucher que ce
répertoire**. Si ça oblige à modifier le chargeur de catalogue, le scanner ou une vue QML,
l'abstraction est fausse et se corrige à ce moment-là.
