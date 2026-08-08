# Lots de développement — igiris-frontend

> Traduction du §17 « Ordre de travail » du `CLAUDE.md` en lots livrables.
> Chaque lot a un **livrable vérifiable** : quelque chose qui se lance et se constate, pas
> « du code écrit ». Versionnement SemVer strict (§15), livraison en archive zip.
>
> Établi le 2026-08-07, au démarrage du projet.

---

## Tableau récapitulatif

| # | Lot | Livrable vérifiable | Version | Modèle | Effort | État |
|---|---|---|---|---|---|---|
| 0 | Socle de projet | `cmake --build` passe, binaire vide qui démarre | 0.1.0 | Sonnet 5 | faible | ✅ **fait** |
| 1 | Adaptateur de plateforme + Batocera | tests sur arborescence factice | 0.2.0 | Opus 5 | **élevé** | ✅ **fait** |
| 2 | Parser `es_systems` | CLI : liste systèmes + commandes de lancement | 0.3.0 | Sonnet 5 | moyen | ✅ **fait** |
| 3 | Chargeur d'export + façade `platform_key` | les 4 requêtes du §3 rejouées en C++ | 0.4.0 | Opus 5 | moyen | ✅ **fait** |
| 4 | Scanner de ROMs | rapport CLI vert / rouge / noir | 0.5.0 | Opus 5 | **élevé** | à faire |
| 5 | IHM QML : liste + recherche | binaire affichant la liste, navigation manette | 0.6.0 | Sonnet 5 | moyen | à faire |
| 6 | Filtres statiques puis dynamiques | filtres combinables, interactifs | 0.7.0 | Sonnet 5 | moyen | à faire |
| 7 | Fiche de jeu + lancement effectif | **un jeu se lance** | 0.8.0 | Opus 5 | moyen | à faire |
| 8 | Badges de langue | badges illuminé/grisé + filtres langue | 0.9.0 | Opus 5 | moyen | 🔒 **bloqué** |
| 9 | Second adaptateur (Recalbox) | Recalbox marche **sans toucher** au reste | 0.10.0 | Opus 5 | faible en code | à faire |
| 10 | Empaquetage + démarrage | image qui boote sur le frontend | 1.0.0 | Sonnet 5 | moyen | à faire |

---

## Détail des lots

### Lot 0 — Socle de projet · 0.1.0

Chaîne de build (`build-essential`, `cmake`, `ninja`, Qt6 base + declarative, modules QML,
`libsqlite3-dev`), squelette CMake, arborescence des sources.

**Le test CI du §1 est écrit ici, pas plus tard** : un `grep` qui échoue si `/userdata/`,
`batocera` ou `emulatorlauncher.py` apparaît **hors de l'adaptateur**. Écrit au lot 0, il
protège tous les lots suivants ; écrit après, il arrive après les dégâts.

> **Choix technique** : accès à l'export via l'**API C de SQLite** (`libsqlite3-dev`), pas
> via le pilote Qt SQL. Le §2 impose `file:games.db?immutable=1` avec
> `SQLITE_OPEN_READONLY | SQLITE_OPEN_URI` ; c'est direct et sans ambiguïté avec l'API C.

### Lot 1 — Adaptateur de plateforme + implémentation Batocera · 0.2.0

Les **quatre** méthodes du §1, rien de plus, plus la déclaration de capacités.

C'est le lot où se joue le §13 : si l'interface est fausse ici, on ne le découvrira qu'au
lot 9, et la correction sera chère. D'où l'effort élevé sur peu de code.

### Lot 2 — Parser du fichier de description des systèmes · 0.3.0

`es_systems.cfg` → systèmes disponibles + **commandes de lancement lues, jamais
hardcodées** (§1). C'est ce parser qui décide du statut *noir* (§7).

**Fichiers de référence** : `~/igiris-frontend-refs/`, **hors du dépôt** (§15). Contient un
`es_systems.xml` réel de 195 systèmes, seul échantillon obtenu au format d'exécution.

> ⚠️ Le `es_systems.yml` de Batocera récupéré à côté est la **source du générateur**, pas le
> fichier lu à l'exécution : Batocera fabrique son `es_systems.cfg` sur l'appareil. Aucun
> `es_systems.cfg` Batocera authentique n'a pu être récupéré ; à confirmer sur du matériel
> réel avant de considérer le parser validé pour cette distribution.

### Lot 3 — Chargeur d'export + façade `platform_key` · 0.4.0

Ouverture immuable, contrôle de `schema_version`, **refus explicite** d'une majeure inconnue.

Applique la décision d'attente du §9.1 : le chargeur est **le seul endroit du code qui
connaît le nom `batocera_system`**, et il expose `platform_key` à tout le reste. Le jour où
le backend renomme, c'est une ligne à changer.

### Lot 4 — Scanner de ROMs incrémental · 0.5.0

Le lot le plus piégeux du projet, d'où l'effort élevé :

- `header_skip` (nes 16 / atari7800 128 / lynx 64) ;
- **heuristique SNES 512 o** du §4, non couverte par l'export ;
- archives : hasher **le contenu**, pas le zip ;
- arcade : **jamais** de CRC, identification par nom de romset ;
- cache incrémental chemin + taille + mtime.

Les erreurs y sont **silencieuses** : ça ne plante pas, ça affiche du rouge à tort.

### Lot 5 — IHM QML : liste + recherche · 0.6.0

Navigation par focus, pilotage manette. **Point de référence de performance** : une liste
qui défile sans badges, mesurée avant d'ajouter quoi que ce soit (note d'ordonnancement du
§17).

### Lot 6 — Filtres · 0.7.0

Statiques d'abord (index de l'export), dynamiques ensuite (croisement avec l'index local).

### Lot 7 — Fiche de jeu + lancement effectif · 0.8.0

**Le vrai jalon** : à la fin de ce lot, la boucle est bouclée — on voit ses jeux, on en
choisit un, il se lance. Tout ce qui suit est de l'enrichissement.

> ⚠️ **Prérequis découvert au lot 3, sur le vrai `es_systems.cfg` de Batocera 43.1.**
>
> La commande de lancement réelle est la même pour tous les systèmes :
> ```
> emulatorlauncher %CONTROLLERSCONFIG% -system %SYSTEM% -rom %ROM% \
>                  -gameinfoxml %GAMEINFOXML% -systemname %SYSTEMNAME%
> ```
> Sur 5 placeholders, le lot 1 n'en gère que **2** (`%ROM%`, `%SYSTEM%`). Comme `launch()`
> refuse de partir sur un placeholder inconnu, **aucun jeu ne se lancerait** : le refus
> porterait sur 100 % des systèmes.
>
> Les trois manquants sont produits **à l'exécution par EmulationStation**, que ce projet
> remplace — c'est donc à nous de les fabriquer :
> - `%CONTROLLERSCONFIG%` — description des manettes branchées ;
> - `%GAMEINFOXML%` — fiche du jeu passée à l'émulateur ;
> - `%SYSTEMNAME%` — nom de système transmis au launcher.
>
> Leur sémantique exacte est **à établir depuis les sources de `batocera-emulationstation`
> avant d'écrire quoi que ce soit** : les inventer produirait des lancements qui échouent
> de façon obscure. Le refus actuel est le bon comportement en attendant — il est bruyant,
> pas silencieux.

### Lot 8 — Badges de langue · 0.9.0 · 🔒 BLOQUÉ

Bloqué par une **livraison du backend**, pas par du travail à faire ici :
`exp_language`, `exp_game_language`, `exp_game.lang_mask` (§9.2). Ajout **additif** →
version mineure 1.4.0 de l'export.

C'est une **demande adressée au projet `igiris`**, qui n'est pas un lot de ce projet-ci.
Si le backend livre tôt, ce lot se replace juste après le lot 6.

### Lot 9 — Second adaptateur (Recalbox) · 0.10.0

**Ce n'est pas une fonctionnalité, c'est le test qui valide l'abstraction du lot 1**, et il
reste **avant** l'empaquetage comme le §13 l'exige.

> Critère de réussite : ajouter Recalbox ne doit toucher **que** l'adaptateur.

### Lot 10 — Empaquetage et intégration au démarrage · 1.0.0

Déclenche la **phase 2** de la stratégie git (§16) : un fork mince par distribution
empaquetée.

---

## Écarts assumés par rapport au §17

**1. Étapes 6 et 7 inversées** (filtres avant badges). Non par confort : les badges
dépendent de `exp_game_language`, qui n'existe pas dans l'export 1.3.0. Les filtres, eux, ne
dépendent de rien. La note d'ordonnancement du §17 reste respectée — la liste nue du lot 5
demeure le point de référence de performance.

**2. Ajout d'un lot 0** absent du §17. Le projet part d'une machine sans aucune chaîne de
build : le socle est un préalable, pas une étape de conception.

---

## Rappel de périmètre

Le backend `igiris` (`/opt/igiris`) est **un autre projet, en production**. Aucun lot listé
ici n'y touche. Les besoins d'évolution du contrat d'export sont des **demandes** formulées
au backend (§9), jamais du travail réalisé depuis ce dépôt.
