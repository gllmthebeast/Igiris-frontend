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
| 4 | Scanner de ROMs | rapport CLI vert / rouge / noir | 0.5.0 | Opus 5 | **élevé** | ✅ **fait** |
| 5 | IHM QML : liste + recherche | binaire affichant la liste, navigation manette | 0.6.0 | Sonnet 5 | moyen | ✅ **fait** |
| 6 | Filtres statiques puis dynamiques | filtres combinables, interactifs | 0.7.0 | Sonnet 5 | moyen | ✅ **fait** |
| 7 | Fiche de jeu + lancement effectif | **un jeu se lance** | 0.8.0 | Opus 5 | moyen | ✅ **fait** |
| 8 | Badges de langue | badges illuminé/grisé + filtres langue | 0.9.0 | Opus 5 | moyen | 🔒 **bloqué** |
| 9 | Second adaptateur (Recalbox) | Recalbox marche **sans toucher** au reste | 0.10.0 | Opus 5 | faible en code | ⚠️ **fait — flaw corrigé** |
| 10 | Empaquetage + démarrage | archive installable + accroche de démarrage | 1.0.0 | Sonnet 5 | moyen | ✅ **fait** |

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

**Fichiers de référence** : `~/igiris-frontend-refs/`, **hors du dépôt** (§15), avec son
propre README. Le parser est validé sur **deux** fichiers réels :

| Source | Systèmes | Placeholders | Résultat |
|---|---|---|---|
| Batocera 43.1, extrait de l'image (`/usr/share/emulationstation/`) | 224 | **5** | 224/224, 0 avertissement |
| ES-DE, dépôt amont | 195 | 107 | 195/195, 0 avertissement |

Le chemin où Batocera livre réellement le fichier est **exactement celui que l'adaptateur
cherche en second** (lot 1) : l'hypothèse est confirmée sur l'image officielle.

> Le `es_systems.yml` du dépôt Batocera, récupéré à côté, est la **source du générateur** et
> non le format d'exécution — à ne pas confondre.

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

**Mesure de référence** (VM aarch64, 7 581 jeux) : ouverture de l'export < 1 ms, chargement
du modèle **10 ms**. Le filtrage est en mémoire — interroger SQLite à chaque frappe ne
tiendrait pas l'exigence d'interactivité du §6.

> ⚠️ Le rendu de la VM passe par le backend **logiciel** de Qt Quick : il valide la mise en
> page et la logique, **pas** la fluidité du défilement. Le vrai point de référence de
> performance devra être repris sur l'appareil.

**Décision** : `QtQuick` seul, **sans `QtQuick.Controls`**. Chaque module Qt ajouté doit
être cross-compilé dans les images Buildroot des distributions cibles (§12) ; un champ de
saisie ne vaut pas cette dépendance, `TextInput` fait le travail.

**Captures sans écran** : `igiris-frontend --screenshot <fichier.png> [filtre]` force la
plateforme `offscreen` et le backend logiciel, puis saisit la fenêtre. C'est ce qui permet
de juger l'interface depuis une machine de build.

### Lot 6 — Filtres · 0.7.0

Statiques d'abord (index de l'export), dynamiques ensuite (croisement avec l'index local).

| Filtre | Nature | État |
|---|---|---|
| Plateforme | statique | ✅ 71 clés, issues de l'export |
| Décennie | statique | ✅ |
| Arcade | statique | ✅ clés d'arcade lues dans l'export, pas codées |
| Possédé / manquant | **dynamique** | ✅ après `--roms <dossier>` |
| Langue (existe / jouable) | statique + dynamique | 🔒 export 1.4.0 requis (§9.2) |

**Le filtre dynamique se grise tant qu'aucun scan local n'a eu lieu**, et l'indique
(« aucun scan local »). Le masquer laisserait croire qu'il n'existe pas ; l'activer sans
scan afficherait « tout est manquant », ce qui serait faux et invérifiable. C'est la
déclaration de capacités du §1 appliquée à l'interface.

Un scan qui ne trouve rien reste un scan : l'information est « tu ne possèdes rien », pas
« le filtre est indisponible ».

**Coût mesuré** : l'index plateformes-par-jeu porte le chargement de 10 à **25 ms** pour
7 581 jeux et 18 555 lignes de plateformes. Payé une fois au démarrage, il rend toutes les
combinaisons de filtres instantanées — ce qu'exige le §6.

> ⚠️ **`--platform` est réservé par Qt.** `QGuiApplication` capte cette option pour choisir
> son plugin de plateforme (`offscreen`, `xcb`…) : l'application refusait de démarrer avec
> « Could not find the Qt platform plugin "snes" ». L'option du projet s'appelle
> `--platform-key`.

### Lot 7 — Fiche de jeu + lancement effectif · 0.8.0

**Le vrai jalon** : à la fin de ce lot, la boucle est bouclée — on voit ses jeux, on en
choisit un, il se lance. Tout ce qui suit est de l'enrichissement.

**Bloquant du lot 3 : levé.** La sémantique des trois placeholders manquants a été établie
dans les sources livrées, pas devinée :

| Placeholder | Source | Valeur |
|---|---|---|
| `%SYSTEMNAME%` | `FileData.cpp:571` | le **fullname** du système, pas sa clé |
| `%GAMEINFOXML%` | `FileData.cpp:580-585` | chemin d'un XML temporaire, **chaîne vide** s'il n'est pas produit |
| `%CONTROLLERSCONFIG%` | `InputManager.cpp:1297` | `-p1index N -p1guid … -p1name "…"` par manette, **vide** si aucune |

Et surtout, la preuve que la commande produite est acceptée, lue dans l'`emulatorlauncher`
de l'image 43.1 (`configgen/emulatorlauncher.py:638-651`) :

```python
parser.add_argument("-system", required=True)
parser.add_argument("-rom",    required=True)
parser.add_argument("-gameinfoxml", nargs='?', default='/dev/null', required=False)
parser.add_argument("-systemname",  required=False)
```

`nargs='?'` : un `-gameinfoxml` **sans valeur est prévu par conception**. Seuls `-system` et
`-rom` sont obligatoires ; tous les arguments de manettes sont optionnels.

**Différence assumée avec l'amont** : EmulationStation construit UNE chaîne confiée à un
shell, et échappe donc ses valeurs. Ici les arguments restent séparés jusqu'à `QProcess` :
reproduire cet échappement ferait entrer les guillemets DANS l'argument. Corollaire, un
jeton réduit à un placeholder vide est **supprimé** — le garder passerait un argument vide,
ce que l'amont n'a jamais puisque le vide disparaît dans sa chaîne.

> ⚠️ **`%CONTROLLERSCONFIG%` reste vide** : la capacité `ControllerMapping` n'est pas
> déclarée, faute d'énumération des manettes (Qt6 ne fournit plus QtGamepad ; il faudra
> SDL). L'émulateur retombera sur sa configuration par défaut. La fiche **le dit**, comme
> l'exige le §1 — elle ne laisse pas croire que tout est transmis.

> ⚠️ **`is_preferred` ne discrimine pas.** L'export le marque sur **18 116 des 18 555
> lignes** (97,6 %), et **3 932 jeux** ont plusieurs plateformes « élues ». Le §7 prévoit
> de proposer par défaut le système `is_preferred` : c'est impossible en l'état. Le défaut
> retenu est le **premier système jouable** (vert, puis meilleur `emu_score`). **À signaler
> au backend** — c'est une anomalie de données, pas un choix d'affichage.

### Lot 8 — Badges de langue · 0.9.0 · 🔒 BLOQUÉ

Bloqué par une **livraison du backend**, pas par du travail à faire ici :
`exp_language`, `exp_game_language`, `exp_game.lang_mask` (§9.2). Ajout **additif** →
version mineure 1.4.0 de l'export.

C'est une **demande adressée au projet `igiris`**, qui n'est pas un lot de ce projet-ci.
Si le backend livre tôt, ce lot se replace juste après le lot 6.

### Lot 9 — Second adaptateur (Recalbox) · 0.10.0

**Ce n'est pas une fonctionnalité, c'est le test qui valide l'abstraction du lot 1.**
Il a fait son travail : **il a échoué, et il a révélé un vrai défaut de conception.**

#### Ce que Recalbox a démenti

Le format n'est **pas** celui d'EmulationStation. Tout est en **attributs XML** :

```xml
<system name="snes" fullname="Super Nintendo">
  <descriptor path="…" extensions="…" theme="…" command="…"/>
```

là où la convention ES écrit `<name>snes</name>`. Schéma établi depuis
`SystemDeserializer.cpp` du dépôt Recalbox.

Son lanceur diffère aussi : `-emulator` et `-core` y sont **obligatoires**
(`configgen/emulatorlauncher.py`), et ni `-gameinfoxml` ni `-systemname` n'existent.

#### Le défaut, et sa correction

L'interface exposait `systemsFilePath()` : elle **supposait un format unique**, parsé une
fois pour toutes par `src/systems/`. Conséquence, `main.cpp` appelait lui-même
`parseEsSystemsFile()` — le point d'entrée connaissait un format de distribution.

L'interface expose désormais **`readSystems()`** : chaque adaptateur lit le sien.
Le §13 exige de corriger « à ce moment-là, pas après avoir empaqueté » — c'est fait.

#### Mesure du critère

| Zone | Lignes touchées |
|---|---|
| `src/platform/` (l'adaptateur) | +45 / −5, plus 2 fichiers neufs |
| `src/main.cpp` (composition) | +20 / −13 — **le défaut corrigé** |
| `CMakeLists.txt` | +13 / −3 |
| **`src/catalog/`** | **0** |
| **`src/scan/`** | **0** |
| **`src/ui/`** | **0** |
| **`src/systems/`** | **0** |
| **`qml/`** | **0** |

Le §13 nomme explicitement le **chargeur de catalogue**, le **scanner** et les **vues
QML** : ces trois-là sont à **zéro ligne**. L'écart porte sur le point d'entrée, qui
faisait le travail de l'adaptateur — et c'est précisément ce que le test devait exposer.

> ⚠️ Contrairement à Batocera, **aucun `systemlist.xml` réel n'a pu être récupéré** :
> l'adaptateur est conforme au schéma publié, pas encore confronté à un fichier de
> production. À vérifier sur une image Recalbox avant de considérer la cible validée.

### Lot 10 — Empaquetage et intégration au démarrage · 1.0.0

Déclenche la **phase 2** de la stratégie git (§16) : un fork mince par distribution
empaquetée.

#### La chaîne de démarrage réelle de Batocera 43.1

Établie en montant l'image, pas d'après la documentation :

```
/etc/init.d/S31emulationstation  ->  labwc-launch (si system.es.atstartup != 0)
    labwc (compositeur Wayland)
        /usr/share/labwc/autostart
            derniere ligne : /usr/bin/emulationstation-standalone > /dev/null 2>&1
```

#### Le fork de la phase 2 : **une ligne**

```diff
-/usr/bin/emulationstation-standalone > /dev/null 2>&1
+/usr/bin/igiris-frontend > /userdata/system/logs/igiris.log 2>&1
```

Le compositeur Wayland reste en place : Qt en a besoin pour afficher quoi que ce soit.
C'est le critere du §16 — un diff d'une ligne se rebase sans douleur a chaque version de
l'hote.

> ⚠️ Ne PAS mettre `system.es.atstartup` a `0` : `labwc` ne demarrerait plus, et le
> frontend n'aurait plus de compositeur, donc plus d'affichage.

#### Livrable

`cpack -G ZIP` produit `igiris-frontend-1.0.0-aarch64.zip` (0,3 Mo) : le binaire, les
outils, la documentation et `packaging/`. **Pas l'export** — c'est un artefact (§15).

Le QML est compile DANS le binaire : il n'y a rien a installer a cote, ce qui est
precisement ce qui rend l'integration si mince.

Verifie : le binaire extrait tourne **hors du depot** (`--version`, `--systems` sur les
224 systemes reels), et signale explicitement l'export manquant au lieu de demarrer vide.
Le `sed` de `install-on-device.sh` a ete essaye sur le contenu reel d'`autostart` — une
ligne remplacee, `batocera-mouse` preserve.

#### Ce qui reste hors de portee d'ici

Construire une **image** demande Buildroot et l'activation, dans le `defconfig` de chaque
cible, de Qt6 (Core/Gui/Qml/Quick), du plugin de plateforme **wayland** et de `libsqlite3`.
C'est le vrai travail d'integration systeme, propre a chaque distribution, et il se fait
dans le fork — pas dans ce depot.

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
