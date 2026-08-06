# CLAUDE.md — igiris-frontend

> Fichier de contexte lu automatiquement par Claude Code.
> **Projet frontend embarqué** (Batocera / Raspberry Pi). Le backend est un projet SÉPARÉ :
> `/opt/igiris` sur cette même VM, avec son propre `CLAUDE.md`.

---

## 0. Ce que fait ce projet, et ce qu'il ne fait pas

L'appareil de l'utilisateur (typiquement un Raspberry Pi sous Batocera) contient des ROMs.
Ce frontend doit lui dire **quel jeu il possède**, **sur quelle plateforme**, et **laquelle
est la meilleure version** d'après les votes de la communauté.

**Il fait exactement deux choses :**
1. chercher un jeu par son nom ;
2. retrouver un jeu depuis un fichier local (par CRC, ou par nom de romset pour l'arcade).

**Il ne fait AUCUN rapprochement de titres.** Tout est précalculé côté serveur : la
normalisation des noms, le rapprochement avec les dats, l'arbitrage des versions, les scores
d'émulation. L'appareil ne fait que des *lookups* dans une table précalculée.

> C'est la décision structurante du duo de projets. Rapprocher « Elder Scrolls V, The -
> Skyrim » et « The Elder Scrolls V: Skyrim » demande une normalisation, des noms
> alternatifs, du fuzzy et un arbitrage humain — rien de tout cela n'a sa place sur un Pi.

---

## 1. La ressource : l'export SQLite

Un **artefact de build**, régénéré côté serveur, que l'appareil télécharge et remplace d'un
bloc. Il ne se modifie jamais sur l'appareil.

```
https://igiris.xyz/exports/games.db        ~6,8 Mo
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
remplaçable atomiquement. WAL crée des fichiers `-wal`/`-shm` annexes et exige un `-shm`
inscriptible *même pour lire*. Le serveur livre volontairement en `journal_mode=DELETE`.

**Vérifier `schema_version` au chargement.** Il est dans `exp_meta` ET dans le manifeste.
Refuser une version **MAJEURE** inconnue plutôt que de casser en silence ; les versions
mineures sont additives et rétrocompatibles.

**Vérifier le sha256** du fichier téléchargé contre le manifeste avant de remplacer
l'ancien. Un export tronqué ne doit jamais devenir l'export courant.

---

## 2. Le schéma (version 1.3.0)

```sql
exp_meta(key, value)
    -- schema_version, generated_at, games, platforms, rom_hashes, arcade_romsets, dat_sets

exp_game(game_key, title, search_key, year, cover_ref, rating)
    -- game_key : identifiant stable, = title.id côté serveur (« igdb-3192 »)
    -- search_key : nom NORMALISÉ, à utiliser pour la recherche locale (jamais à afficher)
    -- cover_ref : URL IGDB — voir la limite « hors ligne » plus bas
    -- rating : note IGDB /100

exp_game_platform(game_key, batocera_system, display_name, emu_score, is_preferred)
    -- emu_score : fidélité 0..100 (voir §4)
    -- is_preferred : plateforme élue par les votes de la communauté

exp_rom_hash(crc32, batocera_system, game_key, header_skip)
    -- LA table interrogée à chaud. C'est un lookup, pas une recherche.
    -- header_skip : octets d'en-tête à IGNORER avant de calculer le CRC (voir §3)

exp_romset(romset, batocera_system, game_key, emulators, hardware, driver_status)
    -- ARCADE uniquement, identification par NOM DE FICHIER (voir §3)
```

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

---

## 3. Deux voies d'identification, et pourquoi

**Consoles → par CRC.** Le fichier est hashé, on cherche dans `exp_rom_hash`.

⚠️ **`header_skip`** : les dats No-Intro de la NES, l'Atari 7800 et la Lynx portent
l'en-tête du format (16 o iNES, 128 o A78, 64 o LNX). Si le fichier local n'en a pas, il
faut en tenir compte avant de hasher — sinon tout tombe en rouge à tort.

**Arcade → par NOM DE ROMSET**, jamais par CRC. Le CRC d'un `.zip` d'arcade change dès qu'on
reconstruit le romset (merged / split / non-merged), ce que font couramment les utilisateurs
de MAME. Constaté dans les données du backend : `crusnexod.zip` porte deux CRC différents
selon la source. Le **nom** (`sf2ce`, `mslug3`) est stable, et c'est sous ce nom que Batocera
range les jeux d'arcade.

`exp_romset` porte en plus :
- `hardware` — le matériel réel (`neo`, `cps2`, `naomi`…), qu'IGDB ne connaît pas : il n'a
  qu'une plateforme « Arcade » globale. Reconstruit depuis les drivers MAME.
- `emulators` — quels émulateurs savent le lancer (`mame`, `fbneo`, `fbneo,mame`). Utile
  pour savoir dans quel dossier Batocera le ranger.
- `driver_status` — `good` | `imperfect` | `protection` | `preliminary`.

---

## 4. Comment lire `emu_score`

C'est un **% de fidélité du rendu par rapport à la version d'origine sur matériel réel**,
avec le meilleur émulateur existant, **sans tenir compte de la puissance de l'hôte**.

```
score = plafond_de_la_plateforme × jouabilité × (1 − pénalités de fidélité)
```

Conséquence à comprendre : un jeu PS3 parfaitement jouable plafonne à **42**, parce que le
meilleur émulateur PS3 n'atteint que ~42 % de fidélité. Un jeu SNES parfait fait **95**.
**Ce n'est donc pas une note de qualité du jeu** — c'est une note d'émulation. Ne pas les
présenter côte à côte sans l'expliquer à l'utilisateur.

---

## 5. Limites connues, à ne pas découvrir en route

- **Les jaquettes sont des URL IGDB** (`cover_ref`), donc **le réseau est nécessaire pour les
  images**. Pour un vrai fonctionnement hors ligne, il faudra un pack d'images — le backend
  peut le produire, c'est à demander.
- **~17 % des jeux éligibles ne sont pas encore rattachés** à un dump. Le contenu s'enrichit
  à chaque passage mensuel ; **la structure, elle, ne bouge pas**.
- **`batocera_system` vaut pour toute la famille EmulationStation** (Batocera, Recalbox,
  RetroPie, RetroBat) : ils partagent l'essentiel des noms de dossiers (`megadrive`, `snes`,
  `psx`…). Le nom de la colonne est historique, la portée est plus large.
- **Les CRC ambigus sont volontairement absents.** Un même CRC peut désigner plusieurs jeux
  (pistes audio de CD identiques). Les indexer rendrait la recherche trompeuse : seuls les
  CRC désignant un seul jeu sont exportés.

---

## 6. Rapport avec le projet backend

| | igiris (backend) | igiris-frontend (ce projet) |
|---|---|---|
| Où | `/opt/igiris` | `~/igiris-frontend` |
| Rôle | catalogue, votes, dats, rapprochement, **génération de l'export** | consommation de l'export sur l'appareil |
| Base | `votes.db`, ~200 Mo, en écriture | `games.db`, 6,8 Mo, **lecture seule** |
| Dépôt | `gllmthebeast/igis` | à créer |

**Ne jamais écrire dans la base du backend depuis ce projet.** Elle est en
`journal_mode=delete` : un écrivain bloque tous les lecteurs, et le site de vote est servi
en production depuis cette même base.

**Pour demander une évolution du contrat** (nouveau champ, pack d'images, autre filtrage) :
c'est une modification du **générateur d'export**, côté backend —
`/opt/igiris/scripts/build-export.py`. Toute évolution additive passe en version **mineure**
et ne casse pas ce projet.

---

## 7. Outils fournis

- `tools/fetch-export.sh` — télécharge l'export, **vérifie le sha256** contre le manifeste,
  ne remplace l'ancien qu'en cas de succès.
- `tools/probe.py` — preuve de bout en bout : ouvre l'export en immuable, contrôle la
  version de schéma, et exécute les quatre requêtes types. À lancer en premier pour
  vérifier que le contrat est respecté.

---

## 8. Conventions

- **Aucune ROM, aucun hash de ROM dans ce dépôt** — même règle que le backend. Les hashes
  vivent dans l'export téléchargé, pas dans le code source.
- **Pas de secrets dans le code.**
- L'export est un **artefact** : ne jamais le versionner dans git.
