# CLAUDE.md — igiris-frontend

> Fichier de contexte lu automatiquement par Claude Code.
> **Frontend embarqué** (Batocera / Raspberry Pi en premier, mais pas uniquement — §1).
> Le backend est un projet **SÉPARÉ** : `/opt/igiris` sur cette même VM, avec son propre
> `CLAUDE.md`. Ce projet ne démarre vraiment qu'une fois l'export générable de bout en bout
> — ce qui est le cas aujourd'hui (schéma **1.8.0**, cf. `data/`).

---

## 0. Ce que fait ce projet, et ce qu'il ne fait pas

### L'objectif produit

Remplacer l'écran d'accueil des distributions de rétrogaming par un frontend custom
présentant **une liste unique de jeux, tous systèmes confondus**. L'appareil de
l'utilisateur (typiquement un Raspberry Pi sous Batocera) contient des ROMs ; le frontend
doit lui dire **quel jeu il possède**, **sur quelle plateforme**, et **laquelle est la
meilleure version** d'après les votes de la communauté et les scores d'émulation.

### Principes non négociables

- **Pas de liste de systèmes à l'accueil.** L'entité de premier niveau est le **jeu**, pas
  la plateforme.
- **Tous les jeux du catalogue sont affichés, ROM présente ou non.** L'absence de ROM est
  une *information affichée*, pas un filtre.
- **Le moteur d'émulation n'est jamais modifié.** Aucun patch des cores, de RetroArch, ni
  des scripts de lancement de la distribution hôte.
- **Pas d'addons, pas de store.** Interface volontairement minimale.
- **Multi-distribution dès le premier jour** (§1).

### Le contrat de calcul

Techniquement, l'appareil ne fait que deux choses :

1. chercher un jeu par son nom ;
2. retrouver un jeu depuis un fichier local (par CRC, ou par nom de romset pour l'arcade).

**Il ne fait AUCUN rapprochement de titres.** Tout est précalculé côté serveur : la
normalisation des noms, le rapprochement avec les dats, l'arbitrage des versions, les
scores d'émulation, la résolution région → langue. L'appareil ne fait que des *lookups*
dans une table précalculée.

> C'est la décision structurante du duo de projets. Rapprocher « Elder Scrolls V, The -
> Skyrim » et « The Elder Scrolls V: Skyrim » demande une normalisation, des noms
> alternatifs, du fuzzy et un arbitrage humain — rien de tout cela n'a sa place sur un Pi.

---

## 1. Contrainte de premier rang : compatibilité multi-OS

### Le constat qui rend ça possible

`es_systems.cfg` est une **convention EmulationStation**, pas une invention Batocera. Ce
fichier décrit pour chaque système l'emplacement des ROMs et la **ligne de commande exacte**
de lancement. Presque toute la famille des distributions de rétrogaming l'utilise. Ce qui
change d'une distribution à l'autre, ce ne sont pas les concepts, ce sont **les chemins et
la commande de lancement**.

### Matrice de compatibilité

| Distribution | Base | Statut |
|---|---|---|
| Batocera | Buildroot | Cible de référence |
| Recalbox | Buildroot | Cible de validation (§13) |
| RetroPie | surcouche Raspberry Pi OS | Supporté (lance via `runcommand.sh`) |
| Retrobat | Windows, dérivé Batocera | Supporté |
| EmuELEC | LibreELEC | Supporté |
| Knulli | dérivé Batocera | Supporté |
| ArkOS / ROCKNIX | Linux + ES | À vérifier au cas par cas |
| ES-DE | format `es_systems.xml` | Adaptable, format proche |
| Lakka | RetroArch seul, pas d'ES | **Hors périmètre** |
| muOS | frontend propriétaire | **Hors périmètre** |

Les deux exclusions ne sont pas des oublis : ces systèmes n'ont **aucune couche
EmulationStation à remplacer**. Les supporter demanderait un pilote de lancement
entièrement différent, ce qui ne se justifie qu'après validation du reste.

### L'adaptateur de plateforme

**Décision à appliquer avant la première ligne de code.** Tout ce qui est spécifique à une
distribution est isolé derrière une interface unique, qui expose exactement **six** choses :

1. fournir les systèmes déclarés (les localiser **et** lire leur format) ;
2. résoudre une clé de plateforme du catalogue vers le nom de système local ;
3. localiser les dossiers de ROMs ;
4. lancer — construire et exécuter la commande, substitution de `%ROM%` ;
5. localiser l'export sur cet appareil ;
6. **rendre la main à l'interface de réglages de l'hôte** (§7.1).

Elles étaient quatre au départ. Les deux dernières sont arrivées après coup, et chaque
oubli avait un coût visible : sans la cinquième, l'appareil affichait « 0 jeux » ; sans la
sixième, la machine devenait inconfigurable. Rien d'autre.

Tout le reste du code — chargement du catalogue, matching par hash, calcul des statuts,
interface QML — **ne sait pas sur quelle distribution il tourne**.

### Règles de contrôle

- **Aucune chaîne littérale spécifique à une distribution en dehors de l'adaptateur.** Pas
  de `/userdata/`, pas de `batocera`, pas de `emulatorlauncher.py` ailleurs. C'est
  vérifiable par un simple `grep`, et **ça doit l'être en CI**.
- **Ne jamais hardcoder le chemin du script de lancement.** On lit toujours la commande
  dans le fichier de description des systèmes. C'est ce qui fait survivre l'application aux
  changements de chemin du launcher (la version de Python embarquée a déjà changé plusieurs
  fois chez Batocera), à l'ajout ou au retrait de systèmes, et aux changements d'options.
- **Le fichier de description des systèmes est la source de vérité des systèmes présents**
  → c'est lui qui décide du statut *noir* (§7).
- **L'adaptateur déclare ses capacités.** Une distribution qui ne sait pas faire quelque
  chose le dit ; l'interface s'adapte au lieu de planter.

---

## 2. La ressource : l'export SQLite

Un **artefact de build**, régénéré côté serveur, que l'appareil télécharge et remplace d'un
bloc. Il ne se modifie jamais sur l'appareil.

```
https://igiris.xyz/exports/games.db        ~9,6 Mo
https://igiris.xyz/exports/manifest.json   version, sha256, horodatage
```

### Règles d'usage — non négociables

**Ouverture en lecture seule immuable :**

```
file:games.db?immutable=1     avec SQLITE_OPEN_READONLY | SQLITE_OPEN_URI
```

`immutable=1` fait sauter tout le verrouillage à SQLite : c'est le plus rapide, et le seul
mode qui fonctionne sur une partition montée en lecture seule.

**Ne jamais passer l'export en WAL.** Il doit rester UN fichier, vérifiable par empreinte et
remplaçable atomiquement. WAL crée des fichiers `-wal`/`-shm` annexes, exige un `-shm`
inscriptible *même pour lire*, et n'apporte rien sans concurrence. Le serveur livre
volontairement en `journal_mode=DELETE`.

**Vérifier `schema_version` au chargement.** Il est dans `exp_meta` ET dans le manifeste.
Refuser une version **MAJEURE** inconnue, avec un message explicite, plutôt que de casser en
silence ; les versions mineures sont additives et rétrocompatibles.

**Vérifier le sha256** du fichier téléchargé contre le manifeste avant de remplacer
l'ancien. Un export tronqué ne doit jamais devenir l'export courant.

---

## 3. Le schéma **réellement livré** — version 1.8.0

⚠️ Schéma vérifié dans `data/games.db` au 2026-08-17, APRÈS l'élargissement du catalogue
(§3.1). Pour ce qui est *demandé* au backend mais **pas encore livré** (`platform_key`),
voir §9 — et ne pas coder contre ces champs avant qu'ils existent.

```sql
exp_meta(key, value)
    -- schema_version, generated_at, games, platforms, rom_hashes, arcade_romsets, dat_sets
    -- valeurs actuelles : 17 260 jeux · 59 066 plateformes · 71 006 hashes
    --                     2 697 romsets arcade · 96 dats · 71 systèmes émulables
    -- ⚠ les hashes n'ont PAS bougé en 1.7.0 : les dats ne couvrent que l'émulable, donc
    --   tout ce qui s'est ajouté est du catalogue et jamais de l'identification.

exp_game(game_key, title, search_key, year, cover_ref, artwork_ref, rating,
         summary, mode_mask, lang_catalog_mask, lang_mask)
    -- game_key : identifiant stable, = title.id côté serveur (« igdb-3192 »)
    -- search_key : nom NORMALISÉ, à utiliser pour la recherche locale (jamais à afficher)
    -- cover_ref : URL IGDB — voir la limite « hors ligne » en §10
    -- rating : note IGDB /100
    -- ⚠ lang_mask EST ET RESTE LE DERNIER CHAMP : c'est l'invariant que le backend s'est
    --   donné après avoir décalé un indice positionnel en insérant artwork_ref au milieu.
    --   Ce chargeur lit par NOM et pas par position, donc il y survit — mais l'ordre
    --   ci-dessus est celui du fichier, pas une convention d'écriture.

exp_game_platform(game_key, batocera_system, display_name, emu_score, is_preferred,
                  release_year)
    -- PRIMARY KEY (game_key, display_name)
    -- batocera_system : NULL = plateforme d'origine non émulée (pas une cible).
    --   ⚠ C'était 0 ligne avant le 1.7.0, c'est 40 511 sur 59 066 aujourd'hui — 69 %.
    --   Le code existait, il n'avait jamais été exercé une seule fois.
    -- emu_score : fidélité 0..100 (voir §5), ou NULL sur une plateforme non émulée —
    --   un taux d'émulation n'a aucun sens pour une PS4. Le chargeur le rend en -1, et
    --   surtout pas en 0 : « 0 » se lirait « émulation catastrophique ».
    -- is_preferred : plateforme élue par les votes. CORRIGÉ en 1.4.0 : 31 lignes sur
    --   18 555, sans jeu à double élection. Il ne marque plus « jamais comparé ».

exp_rom_hash(crc32, batocera_system, game_key, header_skip)
    -- PRIMARY KEY (crc32, batocera_system) — LA table interrogée à chaud.
    -- C'est un lookup, pas une recherche.
    -- header_skip : octets d'en-tête à IGNORER avant de calculer le CRC (voir §4)

exp_romset(romset, batocera_system, game_key, emulators, hardware, driver_status)
    -- PRIMARY KEY (romset, batocera_system)
    -- ARCADE uniquement, identification par NOM DE FICHIER (voir §4)

-- ------------------------------------------------------------------ ajouts du 1.4.0 (§8)

exp_language(lang_code, label, badge_asset, bit_index)
    -- 25 codes. bit_index attribué À VIE, et NULL sur 3 d'entre eux :
    -- NULL = PAS DE BIT, jamais le bit 0. La langue reste dans exp_game_language,
    -- donc visible en fiche, mais elle est absente de lang_mask.

exp_game_language(game_key, lang_code, batocera_system, crc32)
    -- PRIMARY KEY sur les quatre colonnes, WITHOUT ROWID. 92 850 lignes.
    -- QUELLE ROM fournit QUELLE langue — même granularité que exp_rom_hash, c'est ce qui
    -- rend l'illumination du §8 calculable sans rien recalculer.
    -- ⚠ la clé commence par game_key : chercher par crc32 y BALAIE la table.

exp_game.lang_mask
    -- masque de bits des langues au catalogue. 5 986 jeux sur 7 581 (79 %).

-- --------------------------------------------------- ajouts du 1.5.0 : visuels et dates

exp_game.artwork_ref
    -- ILLUSTRATION de bandeau : horizontale, composée, SANS texte de pochette. Ce n'est
    -- PAS une jaquette en plus grand — une jaquette est verticale et porte logo, mentions
    -- et code-barres, qui se retrouvaient tronqués en travers du bandeau de la fiche.
    -- 16 811 / 17 260 (97,4 %). Vide → la fiche retombe sur cover_ref, en le sachant.

exp_game_platform.release_year
    -- Année de sortie SUR CETTE PLATEFORME, et non celle du jeu. 59 066 / 59 066.
    -- Pas un doublon de exp_game.year : 22 266 lignes (38 %) en diffèrent. C'est ce qui
    -- permet à la fiche de dater chaque système qu'elle aligne côte à côte.

-- ------------------------------------------------- ajouts du 1.6.0 : ce qui raconte le jeu

exp_game.summary
    -- Synopsis IGDB. 17 220 / 17 260 (99,8 %). ⚠ TOUJOURS EN ANGLAIS, y compris sur une
    -- interface française : IGDB n'en fournit pas de traduit. Pas de colonne summary_lang,
    -- elle porterait 'en' sur 100 % des lignes — la règle est ici, et c'est voulu.

exp_game.mode_mask + exp_game_mode(mode_key, label, bit_index)
    -- Modes de jeu, MÊME PATRON qu'exp_language : bit_index à vie, filtrage par ET
    -- binaire. 16 853 jeux (97,6 %) · 6 modes au référentiel, livré ENTIER (le menu de
    -- filtres ne dépend donc pas du contenu du catalogue).
    --
    -- C'est la réponse au « nombre de joueurs » demandé, PAS ce qui avait été demandé :
    -- le format « 1-4 » n'existe chez IGDB que pour 12 % du catalogue rétro, quand les
    -- modes en couvrent 97 %. On perd d'afficher « 1-4 », on gagne le filtre
    -- « jouable à plusieurs » — 3 333 jeux au lieu de ~900.

-- ------------------------- ajouts du 1.7.0 : le catalogue entier, et sa seconde source

exp_game.lang_catalog_mask
    -- ⚠ LE PIÈGE DE CETTE VERSION. Il PARTAGE le registre de bits d'exp_language avec
    -- lang_mask, donc les confondre est facile ET silencieux. Ils ne disent pas la même
    -- chose :
    --
    --   lang_mask          « une ROM du catalogue fournit cette langue » → illuminable
    --   lang_catalog_mask  « le jeu EXISTE dans cette langue » (IGDB)    → JAMAIS
    --
    -- IGDB ne connaît ni release ni CRC : aucun hash auquel rattacher la langue. D'où :
    --   filtre « existe en fr »   → lang_mask | lang_catalog_mask
    --   filtre « jouable en fr »  → exp_game_language ∩ CRC possédés   (INCHANGÉ)
    --   badges de la vue liste    → lang_mask SEUL (voir §8)
    --
    -- 8 121 jeux, dont 6 879 où les dats sont muets. Couverture du filtre « existe » :
    -- 35 % → 74,5 % sur le catalogue élargi.

-- --------------------------------------- ajouts du 1.8.0 : les autres noms des jeux

exp_game_alias(game_key, alias_key, alias_name)
    -- PRIMARY KEY (game_key, alias_key), WITHOUT ROWID. 18 839 lignes, 9 212 jeux (53 %).
    --
    -- alias_key   NORMALISÉ par le serveur, avec EXACTEMENT la même fonction que
    --             exp_game.search_key — vérifié dans le code du backend, un seul
    --             `norm_key()` appelé aux deux endroits. C'est ce qui rend la recherche
    --             par alias possible sans que l'appareil normalise quoi que ce soit (§0).
    -- alias_name  LISIBLE, et il n'existe que pour être AFFICHÉ.
    --
    -- ⚠ Les deux colonnes sont dans la MÊME ligne, et c'est le point de conception : deux
    --   listes parallèles à tenir alignées auraient rejoué le décalage silencieux qui a
    --   déjà frappé ce contrat deux fois.
    --
    -- Exclus à l'export : les 2 027 alias qui normalisent comme leur propre titre (ils ne
    -- peuvent rien changer). Conservés : les 228 alias que plusieurs jeux partagent — un
    -- CRC ambigu casse une IDENTIFICATION, un alias ambigu enrichit une RECHERCHE.
    --
    -- Départage des collisions de clé : le nom le plus COURT, puis l'ordre alphabétique.
    -- Sans règle, le survivant dépendait de l'ordre d'insertion, donc changeait d'un
    -- export à l'autre sans raison.
```

### 3.1 — L'élargissement du 1.7.0, et ce qu'il exerce

Le catalogue ne se limite plus à l'émulable : le frontend a vocation à devenir un
**launcher**. Trois situations étaient **structurellement impossibles** avant, et sont
désormais courantes — donc trois chemins de code qui n'avaient jamais servi :

| Situation | Volume | Ce que la fiche doit faire |
|---|---|---|
| Plateforme non émulable | 40 511 lignes (69 %) | l'exclure de la liste des systèmes, mais la **nommer** |
| Jeu sans aucun système lançable | 9 679 jeux | dire « sorti sur X — aucun ne s'émule ici » |
| Jeu sans aucune ligne plateforme | 100 jeux | tolérer une liste **vide** sans rien casser |

Le §0 tranche : « l'absence de ROM est une *information affichée*, pas un filtre ». Une
zone vide et muette se lirait comme une panne, pas comme une information.

**Coût mesuré** sur la VM de développement, catalogue complet : chargement 97 ms (55 ms
avant), recherche **1,9 ms par frappe** (0,8 ms avant), bascule de filtre 0,3 ms. Linéaire
en taille de catalogue, et très en dessous d'une image à 60 Hz — rien à optimiser.

> **Ce que l'export ne dit pas, et ne dira pas.** Le backend a tranché : aucune langue
> n'est déduite pour les régions `Europe`, `World` et `Asia`, qui sont multilingues. Un jeu
> européen sans balise de langues dans son dat n'aura donc **aucun badge**. Ce n'est pas
> une donnée manquante à compenser sur l'appareil — c'est une information que le dat ne
> contient pas, et un badge faux serait invérifiable par l'utilisateur.

### Requêtes types

```sql
-- 1. Identifier un fichier de console (CRC calculé localement)
SELECT g.title, h.game_key, h.header_skip
FROM exp_rom_hash h JOIN exp_game g ON g.game_key = h.game_key
WHERE h.crc32 = ? AND h.batocera_system = ?;

-- 2. Identifier un jeu d'arcade (par nom de fichier, sans extension, en minuscules)
SELECT g.title, r.hardware, r.emulators, r.driver_status
FROM exp_romset r JOIN exp_game g ON g.game_key = r.game_key
WHERE r.romset = ? AND r.batocera_system = ?;

-- 3. Recherche par nom
SELECT title, year, rating FROM exp_game
WHERE search_key LIKE '%' || ? || '%' ORDER BY rating DESC LIMIT 50;

-- 4. Meilleure version d'un jeu, et sa fidélité d'émulation
SELECT display_name, batocera_system, emu_score, is_preferred
FROM exp_game_platform WHERE game_key = ? ORDER BY is_preferred DESC, emu_score DESC;
```

`tools/probe.py` exécute exactement ces quatre requêtes : c'est la preuve que le contrat
est respecté, à lancer avant toute autre chose.

---

## 4. Identification des ROMs locales

### Deux voies, et pourquoi

**Consoles → par CRC32.** Le fichier est hashé, on cherche dans `exp_rom_hash`.

**Arcade → par NOM DE ROMSET**, jamais par CRC. Le CRC d'un `.zip` d'arcade change dès
qu'on reconstruit le romset (merged / split / non-merged), ce que font couramment les
utilisateurs de MAME. Constaté dans les données du backend : `crusnexod.zip` porte deux CRC
différents selon la source. Le **nom** (`sf2ce`, `mslug3`) est stable, et c'est sous ce nom
que Batocera range les jeux d'arcade.

`exp_romset` porte en plus :

- `hardware` — le matériel réel (`neogeo`, `cps2`, `naomi`…), qu'IGDB ne connaît pas : il
  n'a qu'une plateforme « Arcade » globale. Reconstruit depuis les drivers MAME.
- `emulators` — quels émulateurs savent le lancer (`mame`, `fbneo`, `fbneo,mame`). Utile
  pour savoir dans quel dossier Batocera le ranger.
- `driver_status` — `good` | `imperfect` | `protection` | `preliminary`.

### Pièges du scan local

**En-têtes de ROM.** Les dats No-Intro sont *sans* en-tête, alors que les ROMs en
circulation en ont souvent un. Hasher le fichier brut fait **tout tomber en rouge à tort**.
`header_skip` dit combien d'octets ignorer. Valeurs réellement présentes dans l'export :

| Système | `header_skip` | Format |
|---|---|---|
| `nes` | 16 | iNES |
| `atari7800` | 128 | A78 |
| `lynx` | 64 | LNX |

> ⚠️ **Le cas SNES n'est PAS couvert par `header_skip`** (il vaut 0 pour `snes`), alors que
> le header de copieur SMC de **512 octets** circule largement. À traiter côté appareil —
> l'heuristique usuelle est `taille_fichier % 1024 == 512` → ignorer les 512 premiers
> octets et réessayer le lookup — ou à demander au backend (§9). Ne pas supposer que
> `header_skip = 0` veut dire « fichier propre ».

**Archives.** Beaucoup de ROMs sont zippées : **hasher le contenu, pas le zip.** (Sauf
arcade, qui ne se hashe pas du tout — voir plus haut.)

**Coût.** Le scan doit être **incrémental** : pas de rehash complet à chaque démarrage.
Cache indexé par chemin + taille + date de modification.

---

## 5. Comment lire `emu_score`

C'est un **% de fidélité du rendu par rapport à la version d'origine sur matériel réel**,
avec le meilleur émulateur existant, **sans tenir compte de la puissance de l'hôte**.

```
score = plafond_de_la_plateforme × jouabilité × (1 − pénalités de fidélité)
```

Conséquence à comprendre : un jeu PS3 parfaitement jouable plafonne à **42**, parce que le
meilleur émulateur PS3 n'atteint que ~42 % de fidélité. Un jeu SNES parfait fait **95**.
**Ce n'est donc pas une note de qualité du jeu** — c'est une note d'émulation. Ne jamais la
présenter à côté de `rating` (note IGDB /100) sans l'expliquer à l'utilisateur.

---

## 6. Écran d'accueil

Liste de jeux + recherche. Rien d'autre.

### Composition d'une ligne

| Élément | Rôle |
|---|---|
| Jaquette (vignette) | identification visuelle |
| Titre | canonique, issu du catalogue |
| Badges de langue | voir §8 |

**Pas d'indicateur de système en vue liste** : le détail par plateforme est l'affaire de la
fiche de jeu (§7). La liste reste dense et lisible à distance, sur un écran de télévision.

### Filtres

| Filtre | Nature | Résolution |
|---|---|---|
| Langue — existe au catalogue | statique | index de l'export, **deux sources** (§3) |
| Langue — possédée | dynamique | export × index local |
| Plateforme | statique | index de l'export |
| Possédé / manquant | dynamique | index local |
| Année, arcade | statique | index de l'export |
| Mode de jeu — solo, multi, coop… | statique | index de l'export (1.6.0) |

### La recherche, et les autres noms des jeux

Depuis le 1.8.0, la recherche porte sur `search_key` **puis**, si le titre ne correspond
pas, sur les `alias_key` du jeu. Taper `lttp`, `ff7` ou un titre japonais fonctionne — sur
53 % du catalogue, qui porte au moins un autre nom.

**La ligne DIT par quel alias elle a été trouvée.** Sans ça, taper `lttp` fait apparaître
« The Legend of Zelda: A Link to the Past » — un titre qui ne contient aucun des caractères
tapés, ce qui se lit comme un bug. C'est la même règle que pour les langues de catalogue du
§8 : un résultat élargi doit pouvoir s'expliquer.

Deux détails qui comptent :

- les alias ne sont testés que **si le titre ne mord pas** — le cas courant ne coûte rien ;
- un alias **exact** l'emporte sur un alias qui contient seulement la saisie. Un jeu porte
  souvent `LTTP` et `TLoZ: ALttP` : taper l'un doit montrer l'un.

**Coût mesuré** sur le catalogue complet : **1,85 → 3,7 ms par frappe**. Toujours très en
dessous des 16 ms d'une image.

> ⚠️ **L'ordre reste alphabétique**, comme partout ailleurs dans cette liste : `lttp` remonte
> *A Link Between Worlds* avant *A Link to the Past*, parce que le titre du premier vient
> avant. Cette liste **filtre**, elle ne classe pas — c'est le §6 tel qu'il est écrit. Y
> introduire un classement par pertinence serait une décision de conception à prendre
> explicitement, pas un effet de bord des alias.

La distinction statique / dynamique n'est pas cosmétique : un filtre **statique** est un
index précalculé par igiris, un filtre **dynamique** impose un croisement avec le résultat
du scan local.

Les filtres sont combinables, et la combinaison doit rester **interactive à la manette** —
pas de temps d'attente perceptible entre l'appui et le résultat.

---

## 7. Fiche de jeu

Jaquette, métadonnées, et la liste des systèmes sur lesquels le jeu existe, avec icône et
code couleur :

| Couleur | Signification |
|---|---|
| **Vert** | Système présent sur cette installation **et** ROM présente localement |
| **Rouge** | Système présent, ROM absente |
| **Noir** | Système absent de cette installation |

Le statut *noir* est décidé par le fichier de description des systèmes, pas par le
catalogue (§1).

Le lancement se fait depuis cette fiche, sur un système **en vert**. Le système marqué
`is_preferred` dans l'export est proposé par défaut ; les autres sont en options
secondaires.

C'est également ici — et pas en vue liste — qu'on détaille **quelle release apporte quelles
langues** : chaque entrée de système porte ses propres badges, avec la même sémantique
illuminé/grisé que §8 mais restreinte à cette plateforme.

### 7.1 — Rendre la main aux réglages de l'hôte

**Remplacer l'écran d'accueil, c'est remplacer la seule interface de configuration de la
machine.** Sur les distributions visées, les manettes, le wifi, le bluetooth, l'audio, la
résolution et la mise à jour du système vivent tous dans l'écran d'accueil d'origine. Le
masquer sans rien proposer rend l'appareil inconfigurable — et sur une borne sans clavier,
irrécupérable.

L'accueil porte donc une entrée **« Paramètres <hôte> »**, qui relance l'écran d'accueil
d'origine. Il se met par-dessus, et sa fermeture nous rend l'écran : les deux sont des
clientes du même compositeur, sur les deux chaînes de démarrage.

**On ne réimplémente rien.** Le §0 interdit les addons, le §12 interdit de forker ES, et
refaire le seul mappage de manettes serait des mois pour un résultat inférieur.

Le nom du binaire, sa localisation et le libellé viennent tous de l'adaptateur. La capacité
`HostSettings` n'est déclarée que si le binaire est **réellement présent** : sinon l'entrée
reste visible mais grisée, avec la raison — une entrée qui ne fait rien serait pire.

**Où elle est, et pourquoi c'est là.** En haut à droite de la barre de recherche, encadrée,
avec son raccourci **F1** écrit à côté — et joignable par ce raccourci depuis n'importe où,
sans navigation.

Elle était d'abord en bout de rangée de filtres. Elle y ressemblait à un filtre, se repliait
sur une seconde ligne, et demandait de traverser six cellules pour l'atteindre : **elle est
passée inaperçue, et il a fallu la redemander alors qu'elle venait d'être livrée.** C'est la
seule fonctionnalité de ce projet dont l'emplacement ait dû être corrigé après coup, et la
leçon vaut au-delà d'elle : un geste de secours ne se cherche pas.

Ce bouton suppose que l'application **tourne**. Quand elle ne tourne pas, c'est le lanceur
du §7.2 qui prend le relais — les deux ensemble couvrent le sujet, aucun ne suffit seul.

#### ⚠️ Le lanceur de l'hôte BOUCLE — et sans ça on n'en revient pas

`emulationstation-standalone` n'est **pas** le binaire : c'est un script bash qui relance
EmulationStation en boucle, lu dans l'image 43.1 :

```sh
touch "${REBOOT_FLAG}"
while [ -e "${REBOOT_FLAG}" ]; do
    dbus-run-session -- emulationstation …
    [ -e /tmp/shutdown.please ] || [ -e /tmp/reboot.please ] && break
done
```

Il existe pour rendre l'écran d'accueil **inquittable** : quoi qu'on choisisse dans son menu,
il le relance. **Ses deux seules sorties sont l'extinction et le redémarrage.**

Le lancer tel quel enferme l'utilisateur — il retrouve ses réglages et perd notre interface.
C'est exactement ce qui est arrivé au premier essai sur appareil, en 1.9.1.

**Le désarmement fait donc partie de l'ouverture.** Le wrapper expose lui-même de quoi le
faire, c'est son propre mécanisme :

```sh
if [ "$1" = "--stop-rebooting" ]; then rm -f "${REBOOT_FLAG}"; exit 0; fi
```

Pas de course : le wrapper pose son drapeau dans ses premières millisecondes et ne le
**relit qu'après** la fermeture d'ES. On a toute la session pour le retirer ; le délai de 3 s
laisse simplement passer le `touch`. Vérifié sur une réplique de la boucle — sans
désarmement elle tourne indéfiniment, avec lui elle rend la main **au premier retour**.

On garde malgré tout le wrapper plutôt que le binaire nu : il pose `HOME`, la langue, le
répertoire courant, attend le compositeur et enveloppe dans `dbus-run-session`. Refaire tout
ça serait précisément la connaissance de distribution que le §1 veut voir rester chez l'hôte.

**Et le chemin du retour se DIT.** L'entrée de menu qui ramène ici s'appelle « Redémarrer
EmulationStation », ce que personne ne devinerait. L'adaptateur fournit la phrase exacte
(`hostSettingsReturnHint()`), affichée au moment où on rend l'écran — le seul instant où
elle sert.

### 7.2 — La porte de sortie, au démarrage

**Si le frontend ne démarre pas, l'utilisateur n'a plus aucune interface.** Écran noir,
machine d'apparence morte, et rien pour aller réparer.

La chaîne de démarrage n'appelle donc pas le binaire mais un **lanceur**
(`/userdata/system/igiris/start.sh`, écrit par l'installateur) qui rend la main à l'écran
d'accueil d'origine dès que le frontend échoue **ou s'arrête trop vite pour avoir affiché
quoi que ce soit** — code de sortie non nul, ou moins de cinq secondes.

Le seuil de durée n'est pas de la précaution : un frontend qui rend 0 en trois secondes n'a
rien affiché, et sans ce test le repli ne se déclencherait pas. Les deux sens sont vérifiés
— repli sur échec, et **pas** de repli sur un fonctionnement normal.

Le lanceur vit dans la partition inscriptible : il se corrige à la main, sans remonter la
racine en écriture.

---

## 8. Badges de langue

> ✅ **Livré au lot 8** (frontend 1.1.0), sur l'export 1.4.0. Cette section décrit
> désormais ce qui EST implémenté ; les décisions prises en chemin sont notées en fin de
> section.

### Sémantique — deux états, un seul axe

| État | Signification |
|---|---|
| **Illuminé** | Au moins une ROM présente localement fournit cette langue |
| **Grisé** | La langue existe pour ce jeu au catalogue, mais aucune ROM possédée ne la fournit |

Une langue qui n'existe dans **aucune** release du jeu n'est pas affichée du tout — ni
grisée, ni illuminée. **Il n'y a pas de troisième état.**

Cette sémantique est volontairement alignée sur le code couleur des systèmes (§7) :
illuminé ≈ vert (possédé), grisé ≈ rouge (existe, pas possédé), absent ≈ le sujet ne se
pose pas.

### Portée : le jeu, pas la ROM

La ligne de liste représente **un jeu**, qui couvre N plateformes et M ROMs. Les badges
affichés sont donc **l'union des langues de toutes les releases** du jeu, et l'état
illuminé/grisé est calculé sur **l'union des ROMs possédées, tous systèmes confondus**. Le
détail « quelle ROM sur quelle plateforme apporte quelle langue » appartient à la fiche
(§7).

### Ce qu'il faut savoir avant d'implémenter

**Une langue n'est pas un pays.** C'est le piège principal de cette fonctionnalité.
L'anglais n'a pas de drapeau évident (Royaume-Uni ? États-Unis ?), l'espagnol non plus
(Espagne ? Mexique ?), l'arabe encore moins.
→ **Décision : les icônes sont des badges de code langue ISO 639-1** (EN, FR, DE, ES, IT,
JA…), stylisés — **pas des drapeaux nationaux**. Le mot « drapeau » reste employé dans
l'équipe par commodité, mais l'asset est un badge de langue. Ça évite une classe entière de
bugs de représentation et reste lisible à distance.

**Langue ≠ région.** Une ROM `(Europe)` n'est pas « en européen » : elle porte souvent
`(En,Fr,De,Es,It)`. Inversement une ROM `(Japan)` sans balise de langue est implicitement
japonaise. La donnée vient de la balise de langues du dat, avec repli sur une table région
→ langue implicite. **Cette résolution se fait côté igiris, jamais sur l'appareil.**

**Volume d'affichage.** Certains jeux dépassent dix langues. La ligne de liste en affiche un
nombre **borné** — ordre : langues possédées d'abord, puis langue de l'interface, puis ordre
stable du catalogue — avec un indicateur `+N` pour le reste.

### La langue comme filtre

Le badge et le filtre sont **la même règle appliquée à deux échelles** : ce qui illumine un
badge est exactement ce qui fait passer un jeu à travers le filtre « possédé dans cette
langue ». Une seule implémentation, deux points d'appel.

Deux filtres distincts, à ne pas confondre dans l'interface :

- **« Existe en français »** — le jeu a une release francophone au catalogue, possédée ou
  non. *Statique*, index de l'export. Sert la découverte.
- **« Jouable en français »** — une ROM possédée fournit le français. *Dynamique*,
  croisement avec l'index local de hashes. **C'est celui qui a une valeur d'usage réelle.**

Les deux doivent être proposés, avec des libellés sans ambiguïté.

**Combinaison multi-langues** : passer par `exp_game.lang_mask` et un ET binaire, **pas**
par une jointure répétée sur `exp_game_language`. Le masque est fourni précisément pour ça.

### ⚠️ Les langues de catalogue ne sont PAS des badges

Depuis le 1.7.0, `lang_catalog_mask` élargit le filtre « existe en… ». Il ne doit **jamais**
alimenter les badges de la vue liste.

Un badge gris promet quelque chose : « télécharge la bonne ROM et il s'allumera ». Une
langue venue d'IGDB ne peut être rattachée à aucun CRC, donc ce badge resterait gris quoi
que fasse l'utilisateur. Ce serait un **troisième état déguisé en second**, alors que cette
section n'en promet que deux.

La **fiche** les montre, séparément et sans l'apparence d'un badge : « le catalogue annonce
aussi : PL — aucune ROM connue ne les fournit ». Dit comme ça, c'est une information ; mis
en badge, c'était une promesse invérifiable.

**Règle de bit** : chaque langue occupe une position de bit fixe, attribuée **à vie** par
igiris. Le frontend ne déduit **jamais** une position depuis l'ordre alphabétique ou
l'ordre d'affichage : il lit `exp_language.bit_index`. Un décalage sur ce point produit des
badges faux, **silencieusement**.

### Contrainte de rendu

Sur Raspberry Pi, une liste qui défile avec N badges par ligne est un piège à performance.

- Badges servis depuis **un atlas de sprites unique**, pas N fichiers image individuels.
- L'état grisé est obtenu par **shader ou opacité sur le même sprite**, jamais par un second
  jeu d'assets.
- Le nombre de badges par ligne est **borné et connu à la génération de l'export**, pour
  éviter tout calcul de layout variable pendant le défilement.

### Règle d'illumination, dans son intégralité

> Une langue est **illuminée** si au moins un `crc32` de `exp_game_language` pour ce
> `(game_key, lang_code)` figure dans l'index local de hashes ; sinon **grisée**.

Il n'y a rien d'autre à calculer. Pas de parsing, pas de normalisation, pas de
correspondance région → langue : tout est résolu en amont.

### Ce qui a été décidé à l'implémentation

| Sujet | Décision | Pourquoi |
|---|---|---|
| Asset des badges | **Texte**, pas d'atlas d'images | Qt rend les glyphes depuis un atlas de texture unique : la propriété recherchée par la règle « atlas » est déjà là, sans le moindre fichier à redistribuer (§16) |
| Borne d'affichage | **6 badges** + `+N` | Mesuré : la moitié du catalogue badgé tient dans 5 langues, mais un jeu monte à 22. La zone est à largeur **réservée**, donc le titre s'élide toujours au même endroit |
| Langues sans `bit_index` | **Absentes de la vue liste, présentes en fiche** | Un masque ne peut pas porter ce qu'il ne contient pas. La fiche lit `exp_game_language` directement, elle n'a donc pas cette limite. Concerne **7 lignes sur 92 850** |
| Filtre sur une langue non masquable | Le modèle **ne filtre pas, et le dit** (`unfilterableLanguages`) | Ignorer en silence renverrait le catalogue entier, qui se lirait comme un résultat de recherche : le filtre *paraîtrait* marcher |
| Masque possédé | Toujours **intersecté** avec le masque du catalogue | Un badge illuminé sans badge correspondant serait faux et invérifiable ; c'est le symptôme d'un décalage de bits, et il doit disparaître à l'affichage plutôt que mentir |

**Coût mesuré**, comme l'exige la note d'ordonnancement du §17 : **1,1 µs par ligne** pour
construire ses badges (20 965 badges sur les 7 581 lignes du catalogue, 8 ms au total sur
la VM de développement). Les badges ne sont construits que pour les lignes **visibles** —
une quinzaine à l'écran — donc le coût réel au défilement est très en dessous de ce chiffre.

---

## 9. Écarts entre le contrat livré et le contrat cible

**Section la plus importante de ce fichier.** Deux évolutions sont *spécifiées* mais **pas
livrées**. Ne pas coder contre elles ; les demander explicitement au backend.

### 9.1 — `batocera_system` → `platform_key` (rupture **MAJEURE**, 2.0.0)

Le catalogue doit être **agnostique de la distribution** : l'export ne devrait contenir
aucune colonne nommée d'après un système Batocera. Il devrait porter une `platform_key`
neutre, l'adaptateur résolvant `platform_key` → nom de système local au démarrage, en
lisant le fichier de description.

**État réel : les trois tables portent `batocera_system`.** Deux choses à savoir :

- La valeur est en pratique **déjà quasi neutre** : `batocera_system` vaut pour toute la
  famille EmulationStation (Batocera, Recalbox, RetroPie, RetroBat), qui partage l'essentiel
  des noms de dossiers (`megadrive`, `snes`, `psx`…). **Le nom de la colonne est historique,
  la portée est plus large.**
- Donc l'écart est un **problème de nommage et de discipline**, pas de sémantique. Il n'y a
  pas d'urgence fonctionnelle, mais il y a une urgence de conception : si le code lit
  `batocera_system` partout, la règle « aucune chaîne Batocera hors adaptateur » (§1) est
  morte avant d'être née.

**Décision d'attente** : le **chargeur de catalogue est le seul endroit** autorisé à
connaître le nom `batocera_system`. Il expose `platform_key` au reste du code, dès
maintenant. Le renommage côté backend deviendra alors un changement d'une ligne.

### 9.2 — Tables de langues — ✅ **LIVRÉ** en 1.4.0

Demandé le 2026-08-08 (`docs/demandes-backend/export-1.4.0-langues.md`), livré le
2026-08-09 (`…-REPONSE.md`). Les trois objets existent, au schéma spécifié — voir §3.

Deux écarts avec la demande, tous deux **assumés et documentés** :

- la troisième colonne d'`exp_game_language` s'appelle `batocera_system` et non
  `platform_key`, **délibérément** : elle porte le même nom que dans les trois tables
  existantes, plutôt que d'introduire deux conventions dans un même export. Le renommage
  se fera d'un bloc en 2.0.0 (§9.1), et la façade du chargeur l'absorbe déjà ;
- `bit_index` n'est attribué qu'aux langues **affichables** (option 1 de la demande) : 22
  des 25 codes en portent un, 39 bits restent libres. Le débordement silencieux redouté ne
  peut plus arriver.

**Livré au passage, sans que ce soit demandé** : `is_preferred` est corrigé. Il valait 1
sur 97,6 % des lignes parce que le marquage testait `elo = MAX(elo)`, et qu'un variant
jamais comparé reste à l'elo de départ — le champ décrivait donc **l'absence de vote**.
Il ne marque plus que 31 lignes, avec unicité garantie. Conséquence pour le §7 : le repli
« premier système jouable, puis meilleur `emu_score` » n'est plus un pis-aller, **c'est le
cas normal** — et `is_preferred = 1` signifie désormais « la communauté a réellement
tranché ».

### 9.3 — Header SNES

`header_skip` ne couvre pas le header de copieur SMC (512 o). Soit l'appareil applique
l'heuristique `taille % 1024 == 512` (§4), soit le backend indexe les deux CRC. **À
trancher** — l'heuristique locale est moins chère et n'attend personne.

### Comment demander une évolution

Toute évolution du contrat est une modification du **générateur d'export**, côté backend :
`/opt/igiris/scripts/build-export.py` (`SCHEMA_VERSION` y est en dur, ligne 40). Additif →
**mineure**. Renommage/suppression → **majeure**, et `tools/probe.py` doit refuser de
charger (`SUPPORTED_MAJOR`).

---

## 10. Ce que fait l'appareil, et surtout ce qu'il ne fait JAMAIS

### Ce qu'il fait

- résolution `platform_key` → système local, via l'adaptateur ;
- calcul de CRC32 des fichiers ROM locaux (incrémental, avec cache) ;
- lookup dans `exp_rom_hash` / `exp_romset` (et `exp_game_language` quand il existera) ;
- application des filtres : statiques par index, dynamiques par croisement avec l'index
  local ;
- rendu de l'interface.

C'est tout. **Déterministe, rapide, hors ligne.**

### Ce qu'il ne fait jamais

- matching de titres, fuzzy, normalisation → **serveur uniquement** ;
- résolution région → langue, normalisation ISO des langues → **serveur uniquement** ;
- requête réseau pour afficher une fiche ou un badge (exception connue : les jaquettes,
  §11) ;
- écriture dans une base, quelle qu'elle soit.

---

## 11. Limites connues, à ne pas découvrir en route

- **Les images sont des URL IGDB** (`cover_ref` pour la jaquette, `artwork_ref` pour le
  bandeau), donc **le réseau est nécessaire pour les afficher**. C'est la seule entorse au
  « hors ligne », et elle reste entière. Pour un vrai fonctionnement déconnecté il faudra un
  **pack d'images** : demandé, chiffré (27 Mo en vignettes 90×120), et **en attente d'une
  décision qui n'est pas technique** — redistribuer des jaquettes IGDB depuis notre propre
  hébergement nous fait passer d'afficheur d'URL à distributeur. Tant que ce point n'est pas
  tranché, la limite est assumée et documentée des deux côtés.
- **Le synopsis est en anglais**, sur une interface française. IGDB n'en fournit pas de
  traduit ; le backend l'a vérifié plutôt que supposé. Mieux vaut le texte réel que rien,
  mais la fiche ne le présente pas comme localisé.
- **9 679 jeux sur 17 260 n'ont aucun système émulable** depuis le 1.7.0 — PC, PS4,
  Switch… Ils s'affichent, se cherchent, ont jaquette, bandeau et synopsis, mais aucun
  n'est lançable. C'est l'objet du chantier launcher ; en attendant, la fiche le **dit**.
- **~17 % des jeux éligibles ne sont pas encore rattachés à un dump.** Le contenu s'enrichit
  à chaque passage mensuel ; **la structure, elle, ne bouge pas**.
- **Les CRC ambigus sont volontairement absents.** Un même CRC peut désigner plusieurs jeux
  (pistes audio de CD identiques). Les indexer rendrait la recherche trompeuse : seuls les
  CRC désignant un seul jeu sont exportés. Conséquence : un fichier légitime peut ne rien
  matcher — c'est *attendu*, pas un bug à corriger sur l'appareil.
- `exp_game_platform.batocera_system` peut être **NULL** : plateforme d'origine non émulée.
  Ces lignes existent pour l'affichage (le jeu est sorti sur cette machine), pas pour le
  lancement. Ne pas les traiter comme des cibles.
- **`is_preferred` ne veut pas dire « lançable ».** Depuis le 1.7.0, **38 des 69 lignes
  élues** portent sur une plateforme non émulable — la communauté a voté pour la version
  X360 d'un jeu qu'on ne sait pas émuler. Les deux notions coïncidaient jusque-là ; elles ne
  coïncident plus. La fiche exclut ces lignes avant de choisir son défaut, et un test le
  verrouille.

---

## 12. Architecture et stack

### Ne PAS forker EmulationStation

`batocera-emulationstation` est en C++/SDL. Le forker imposerait un rebase à chaque release
et lierait le projet à une seule distribution — ce qui contredit §1. **Interdit dans ce
projet.**

Le frontend est une **application autonome**, packagée à part, qui est lancée au démarrage à
la place d'EmulationStation, affiche sa propre interface, et passe par l'adaptateur pour
lancer un jeu.

### Qt6 + QML

Trois contraintes déterminent ce choix :

1. **Pilotage à la manette, pas à la souris.** Élimine tout ce qui suppose un curseur ; met
   la navigation par focus au centre.
2. **Buildroot.** Plusieurs distributions cibles sont construites avec Buildroot : tout
   runtime ajouté doit être **cross-compilable vers ARM et x86_64**. C'est là que la plupart
   des options meurent.
3. **Matériel cible contraint.** Sur Raspberry Pi, ~1–2 Go de RAM utilisable et un GPU
   faible, **pendant que les émulateurs tournent**.

Qt est aussi le seul choix qui satisfait §1 sans effort : la même application tourne sous
Linux embarqué, Linux de bureau et **Windows** — ce dernier point étant nécessaire pour
Retrobat.

**Répartition :**

- **C++** : chargement de l'export, adaptateurs de plateforme, scan des ROMs, exécution du
  lancement.
- **QML** : toute l'interface — liste, recherche, badges, fiche, pastilles de statut.
  `GridView` / `ListView` gèrent nativement la navigation par focus.

Concrètement, **très peu de C++** : la couche qui expose les données à QML, plus les
adaptateurs.

### Pourquoi pas .NET malgré l'expérience C# disponible

Faire entrer un runtime .NET dans une image Buildroot cross-compilée ARM, pour un produit
distribué sur plusieurs OS hôtes, est un chantier permanent. Le coût dépasse le gain. En
revanche, les **outils côté serveur** (import IGDB, génération d'export) peuvent
parfaitement rester en C# — ils ne tournent pas sur l'appareil.

### Références à étudier

- **Pegasus Frontend** — natif C++/Qt cross-platform, orienté embarqué, navigation 100 %
  manette, tourne sur Raspberry Pi et Odroid. À utiliser comme **référence d'architecture**
  (pont C++/QML, gestion manette), **pas comme base à forker** : GPLv3 (copyleft,
  contraignant pour une distribution commerciale) et il liste **les jeux possédés**, en
  désaccord de fond avec l'affichage rouge/noir.
- **RomM** — gestionnaire de collection auto-hébergé, AGPL-3.0, exports ES-DE et Pegasus,
  API REST, écosystème d'applications compagnons. Structurellement, ces compagnons jouent
  vis-à-vis de RomM le rôle que ce frontend joue vis-à-vis d'igiris. **Bon modèle de
  découpage serveur / client.**
- **romman** (ryanm101) — matching par hash d'abord (SHA1 puis CRC32), DAT comme source de
  vérité, identification des jeux manquants, règles déterministes de sélection de la
  meilleure release. Très proche du backend igiris : **à lire avant d'écrire les règles de
  préférence de version.**

---

## 13. Rapport avec le projet backend

### ⚠️ La boîte aux lettres — `~/igiris-echange/`

**À lire au début de toute session qui touche au contrat d'export.**

```
~/igiris-echange/INDEX.md      ← CE FICHIER FAIT FOI
~/igiris-echange/demandes/     frontend → backend
~/igiris-echange/reponses/     backend  → frontend
~/igiris-echange/notes/        dans les deux sens
```

C'est le **seul** mécanisme de notification entre les deux projets. Un document déposé sans
ligne dans `INDEX.md` sera manqué.

Avant le 2026-08-17, chacun déposait dans le dépôt de l'autre : la réponse du backend sur
les alias est restée **non suivie par git** dans `docs/demandes-backend/`, trouvée par
hasard. Un `git status` chez soi ne montre jamais ce que l'autre a déposé chez soi.

Le dossier est **hors des deux dépôts** — pas de push, pas d'accès croisé. `docs/demandes-backend/`
reste l'**archive** de ce projet : on y copie et on y commite ce qui nous concerne, une fois
lu. La boîte aux lettres porte l'état courant, le dépôt porte l'historique.

| | igiris (backend) | igiris-frontend (ce projet) |
|---|---|---|
| Où | `/opt/igiris` | `~/igiris-frontend` |
| Rôle | catalogue, votes, dats, rapprochement, **génération de l'export** | consommation de l'export sur l'appareil |
| Base | `votes.db`, ~200 Mo, en écriture | `games.db`, 9,6 Mo, **lecture seule** |
| Dépôt | `gllmthebeast/igis` | `gllmthebeast/igiris-frontend` |

**Ne jamais écrire dans la base du backend depuis ce projet.** Elle est en
`journal_mode=delete` : un écrivain bloque tous les lecteurs, et le site de vote est servi
en production depuis cette même base.

---

## 14. Outils fournis

- `tools/fetch-export.sh` — télécharge l'export, **vérifie le sha256** contre le manifeste,
  ne remplace l'ancien qu'en cas de succès (téléchargement à côté, bascule par un seul
  `mv`). Ne fait rien si le fichier local est déjà le bon. Base surchargeable par
  `IGIRIS_EXPORT_BASE`.
- `tools/probe.py` — preuve de bout en bout : ouvre l'export en immuable, contrôle la
  version de schéma contre `SUPPORTED_MAJOR`, et exécute les quatre requêtes types (§3).
  **À lancer en premier**, et après chaque mise à jour de l'export.

---

## 15. Environnement de développement et conventions

- **VM Ubuntu** — développement et build. **Claude Code** — outil de travail principal.
- Le **matériel cible est distinct de la VM** : prévoir un cycle build → déploiement → test
  qui ne suppose pas de développer sur la machine d'émulation.
- Des **clones en lecture seule des distributions cibles**, hors du dépôt projet, servent de
  référence (vrais fichiers de description des systèmes, structure des paquets, scripts de
  lancement).

**Conventions :**

- **Versionnement SemVer strict** sur tous les livrables. *Priorité absolue.*
- Itérations livrées sous forme d'**archives zip**.
- **Erreurs remontées verbatim** : messages complets et lisibles, jamais avalés ni
  reformulés par le code.
- **Aucune ROM, aucun hash de ROM dans ce dépôt** — même règle que le backend. Les hashes
  vivent dans l'export téléchargé, pas dans le code source.
- **Pas de secrets dans le code.**
- **L'export est un artefact** : jamais versionné (`data/` est dans `.gitignore`).

---

## 16. Stratégie Git et licences

**Phase 1 — ce dépôt, aucun fork.** Code neuf, dépôt neuf. Le frontend n'est une
modification d'aucune distribution ; c'est une application qui s'y branche.

**Phase 2 — un fork par distribution empaquetée.** Utile uniquement pour l'intégration
système. Chaque fork reste le plus mince possible :

- un paquet pointant vers ce dépôt ;
- la modification du service de démarrage pour lancer le frontend au lieu d'ES ;
- **rien d'autre.**

Plus le diff avec l'upstream est petit, moins le rebase coûte cher — et plus il est réaliste
de maintenir plusieurs cibles en parallèle. **C'est le critère de conception de ces forks.**

**Licences — à trancher avant toute diffusion.** Chaque distribution hôte embarque des
centaines de projets tiers aux licences hétérogènes (GPL, LGPL, BSD, MIT, et clauses
explicitement **non commerciales**, notamment sur des cores libretro). Une image
pré-installée **ne peut pas être vendue en l'état**. Un audit licence par licence est un
prérequis à toute distribution, a fortiori commerciale, et il est **à refaire pour chaque
distribution hôte**. Les assets de badges de langue suivent la même règle : n'embarquer que
des icônes redistribuables commercialement, ou les produire en propre.

---

## 17. Ordre de travail

1. Interface d'adaptateur de plateforme + implémentation Batocera.
2. Parser du fichier de description des systèmes → systèmes disponibles + commandes de
   lancement.
3. Chargement de l'export SQLite + vérification de version + **façade `platform_key`**
   (§9.1).
4. Scanner de ROMs incrémental par hash → statuts vert / rouge / noir.
5. Interface QML : liste + recherche.
6. Badges de langue en vue liste — ✅ **fait** (lot 8, sur l'export 1.4.0).
7. Filtres — statiques d'abord, dynamiques ensuite.
8. Fiche de jeu + lancement effectif + détail des langues par plateforme.
9. **Second adaptateur (Recalbox)** — voir ci-dessous.
10. Empaquetage et intégration au démarrage (déclenche la phase 2 Git).

> **Note d'ordonnancement** : l'étape 6 est placée après la liste nue *volontairement*. Une
> liste qui défile de façon fluide **sans** badges est le point de référence de performance ;
> on mesure ensuite le coût réel des badges par rapport à cette base.

### Le test qui valide l'abstraction

Le second adaptateur (étape 9) n'est **pas une fonctionnalité, c'est un test de
conception**, et il arrive volontairement **avant** l'empaquetage.

> **Critère de réussite : ajouter le support Recalbox ne doit toucher que l'adaptateur.**
> Si l'ajout impose de modifier le chargeur de catalogue, le scanner ou une vue QML,
> l'abstraction est fausse — il faut la corriger **à ce moment-là**, pas après avoir
> empaqueté.

Tant que ce test n'est pas passé, la compatibilité multi-OS reste **une intention, pas une
propriété**.
