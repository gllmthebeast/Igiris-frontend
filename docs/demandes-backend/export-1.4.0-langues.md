# Demande au backend `igiris` — export **1.4.0** : les langues

> Émise par **igiris-frontend** le 2026-08-08, depuis la version 1.0.0.
> Débloque le **lot 8** (badges de langue), seul lot du plan encore à l'arrêt.
>
> Ajout **strictement additif** → version **mineure**. Le frontend actuel continue de lire
> un export 1.4.0 sans modification : il contrôle la majeure, pas la mineure (§2).

---

## 1. Ce qui est demandé

Trois objets, tels que le `CLAUDE.md` du frontend les spécifie déjà (§8, §9.2) :

```sql
exp_language(lang_code, label, badge_asset, bit_index)
    -- référentiel des langues. bit_index attribué À VIE (voir §4).

exp_game_language(game_key, lang_code, platform_key, crc32)
    -- QUELLE ROM fournit QUELLE langue. Même granularité que exp_rom_hash.

exp_game.lang_mask   INTEGER
    -- masque de bits des langues existant au catalogue pour ce jeu.
```

---

## 2. C'est réalisable sans nouvelle collecte

Vérifié le 2026-08-08 sur `backups/votes-20260808-041503.db`, en lecture seule.
La donnée **est déjà là** : `src_dat_rom` porte `region` et `languages`.

```
src_dat_rom                          448 071 lignes
  avec balise de langues                41 906   ( 9,4 %)
  sans langue mais région renseignée   272 800   (60,9 %)  → repli région → langue
  ni l'un ni l'autre                   133 365   (29,8 %)
```

58 codes distincts. Les plus fréquents :
`En` 39 441 · `Fr` 28 073 · `Es` 24 178 · `De` 23 514 · `It` 18 479 · `Ja` 7 422 · `Nl` 4 619

Le chaînage jusqu'au jeu existe déjà : c'est celui qu'emprunte `exp_rom_hash`
(`cur_rom_match` → `title` / `cur_platform` → `src_dat_rom.crc32`).

---

## 3. Le vrai travail : le repli région → langue

**91 % des lignes n'ont pas de balise de langues.** Sans repli, les badges seraient
absents sur la quasi-totalité du catalogue et la fonctionnalité n'aurait aucun intérêt.

Le `CLAUDE.md` du frontend le prévoit explicitement (§8) : *« la donnée de langue vient de
la balise du dat, avec repli sur une table région → langue implicite lorsque la balise est
absente. Cette résolution se fait côté igiris, jamais sur l'appareil. »*

Régions les plus fréquentes à couvrir en priorité :

| Région | Lignes sans balise | Langue implicite attendue |
|---|---|---|
| Japan | 122 125 | `ja` |
| USA | 54 229 | `en` |
| Europe | 35 903 | *plusieurs* — voir ci-dessous |
| World | 13 518 | *plusieurs* |
| Spain | 9 941 | `es` |
| Germany | 7 279 | `de` |
| France | 6 620 | `fr` |
| Russia | 4 385 | `ru` |

⚠️ **`Europe` et `World` n'ont pas de langue unique.** Une ROM `(Europe)` sans balise porte
en général plusieurs langues, sans qu'on sache lesquelles. Deux options, à trancher **côté
backend** :

- **prudente** — ne rien déduire pour ces régions ; le badge reste absent plutôt que faux ;
- **large** — déduire `en` seul, qui est le plus sûr des paris pour `Europe` / `World`.

Le frontend n'a pas d'avis, mais il a besoin que **le choix soit documenté** : un badge faux
est pire qu'un badge manquant, parce qu'il est invérifiable par l'utilisateur.

---

## 4. Le point qui casse en silence : `bit_index`

Le frontend lit `exp_language.bit_index` et n'en déduit **jamais** la position depuis
l'ordre alphabétique ou d'affichage (§8). En contrepartie, deux exigences :

**a) Attribution à vie.** Un `bit_index` réattribué change le sens de tous les `lang_mask`
déjà distribués. Les badges deviennent faux **sans qu'aucune erreur ne se déclenche**.
Une langue retirée doit voir son bit **retiré du service, jamais recyclé**.

**b) ⚠️ Le budget est presque épuisé.**

```
codes distincts aujourd'hui : 58
INTEGER SQLite : 64 bits signés → 63 utilisables sans toucher au bit de signe
marge restante : 5 bits
```

Attribuer un bit à chacun des 58 codes ne laisse que **5** langues de marge. Le 64ᵉ code
déborderait, et le débordement serait **silencieux**.

Trois issues possibles, par ordre de préférence du frontend :

1. **N'attribuer un `bit_index` qu'aux langues réellement affichables** (une vingtaine
   suffit largement) et laisser les autres sans bit — elles resteraient présentes dans
   `exp_game_language`, donc filtrables, simplement pas dans le masque ;
2. stocker `lang_mask` en **BLOB** plutôt qu'en INTEGER, sans limite de largeur ;
3. prévoir `lang_mask_hi` dès maintenant, ce qui reporte le problème de 63 langues.

**L'option 1 est la moins coûteuse et la plus lisible.** Ce qu'il faut absolument éviter,
c'est d'attribuer 58 bits sans le documenter et de découvrir le débordement en production.

---

## 5. Normalisation ISO 639-1

Les dats écrivent `En`, `Fr`, `Ja`. Le frontend affiche des **badges ISO 639-1**, en
minuscules : `en`, `fr`, `ja` (§8). La conversion appartient au backend — l'appareil ne
fait aucune normalisation.

Les 58 codes doivent être passés en revue une fois : la mise en minuscules suffit pour la
grande majorité, mais pas nécessairement pour les codes composés ou régionaux.

---

## 6. Critères d'acceptation, côté frontend

Ce que `tools/probe.py` et le frontend vérifieront :

1. `exp_meta.schema_version` = `1.4.0` — **majeure inchangée**, sinon le frontend refuse
   de charger, à raison (§2) ;
2. `exp_language` non vide, `bit_index` **unique** et **stable** entre deux générations ;
3. tout `lang_code` de `exp_game_language` existe dans `exp_language` ;
4. tout `crc32` de `exp_game_language` existe dans `exp_rom_hash` — sinon aucune ROM locale
   ne pourra jamais allumer ce badge ;
5. `lang_mask` d'un jeu = OU binaire des `bit_index` de ses langues ;
6. les tables existantes sont **inchangées** : c'est ce qui fait la mineure.

---

## 7. Ce que ça débloque

Le lot 8 du frontend : badges illuminé / grisé en vue liste, détail par plateforme en fiche
de jeu, et les deux filtres du §6 — « existe en français » (statique) et « jouable en
français » (dynamique). Tout le reste est écrit et livré en 1.0.0.

La règle d'illumination est déjà spécifiée et n'attend que la donnée :

> une langue est **illuminée** si au moins un `crc32` de `exp_game_language` pour ce
> `(game_key, lang_code)` figure dans l'index local de hashes ; sinon **grisée**.

---

## 8. Anomalie signalée au passage — `is_preferred`

Sans rapport avec les langues, mais constaté en implémentant le lot 7 :

```
is_preferred = 1 sur 18 116 des 18 555 lignes de exp_game_platform  (97,6 %)
3 932 jeux ont PLUSIEURS plateformes marquées « élue »
```

Le champ ne discrimine rien. Or le §7 prévoit de proposer par défaut le système
`is_preferred`. Le frontend a dû le contourner — il retient le premier système **jouable**
(vert, puis meilleur `emu_score`), ce qui est un pis-aller, pas une solution.

À corriger indépendamment de cette demande : ça ne nécessite pas de changement de schéma,
seulement de la génération.
