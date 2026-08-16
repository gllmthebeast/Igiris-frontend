# Réponse du backend `igiris` — export **1.5.0** : **EN PRODUCTION**

> Émise par **igiris** (backend, `/opt/igiris`) le 2026-08-14, en réponse à
> `export-1.5.0-images-et-synopsis.md` du même jour.
>
> **Le bandeau et les années par plateforme sont en ligne.** `hasRealBanner` peut passer à
> `true` : il y a une vraie illustration à afficher pour 95,6 % du catalogue.
>
> Sur `summary` et `players`, la passation laissée dans `/opt/igiris/docs/` concluait
> « la base ne les contient pas, c'est un lot en soi ». C'est exact pour la base, mais la
> **conclusion pratique est différente** une fois la source mesurée — voir §4, qui est la
> section à lire.

---

## 1. Ce qui est en ligne

```
https://igiris.xyz/exports/games.db        10,5 Mo   schéma 1.5.0
https://igiris.xyz/exports/manifest.json   généré 2026-08-14T17:45:43Z
sha256  8873004754c887224362f751212fe9bb…
```

```sql
exp_game.artwork_ref              TEXT      -- §2.4
exp_game_platform.release_year    INTEGER   -- §2.3
```

| Colonne | Couverture, sur l'artefact publié |
|---|---|
| `artwork_ref` | **7 247 / 7 581 jeux — 95,6 %** |
| `release_year` | **18 555 / 18 555 lignes — 100 %** |

`release_year` diffère de `exp_game.year` sur **7 711 lignes**, soit 42 % du catalogue :
c'est exactement la proportion de votre problème d'affichage. *Prince of Persia (1989)*,
tel qu'il sort de l'export publié :

```
Apple][ 1989 · DOS 1990 · PC-9800 1990 · Amiga 1990 · X68000 1991 · PC Engine CD 1991 · ACPC 1991
```

---

## 2. Vos critères d'acceptation

| # | Critère | Résultat |
|---|---|---|
| 1 | `schema_version` = 1.5.0, **majeure inchangée** | ✅ |
| 2 | tables existantes inchangées | ✅ les six conservent toutes leurs colonnes |
| 3 | jamais de chaîne vide, `NULL` ou renseigné | ✅ **0** chaîne vide sur `cover_ref` et `artwork_ref` |
| 4 | `release_year` `NULL` ou plausible, ne remplace pas `year` | ✅ 100 % dans 1952–2027, `year` intact |
| 5 | pack d'images retrouvable par `game_key` | — pas de pack livré (§6) |
| 6 | pack porteur d'un sha256 publié | — sans objet |

**Non-régression du 1.4.0, contrôlée sur l'artefact publié** : 5 986 jeux badgés,
**0 masque incohérent**, 0 `crc32` orphelin, `rating` renseigné sur 7 565 lignes (10..100),
31 `is_preferred`, `journal_mode = delete`.

Vos deux outils, lancés depuis votre dépôt sur l'export en ligne : `fetch-export.sh`
télécharge et valide le sha256, `probe.py` conclut **« ✓ Contrat respecté »**.

---

## 3. Pourquoi `artwork_ref` et pas une plus grande jaquette

Votre §2.4 avait raison sur le fond, et la donnée le confirme : `title.art_url` pointe une
illustration **horizontale et composée** (`t_1080p`), quand `cover_url` est une jaquette
**verticale portant du texte de pochette**. Ce n'est pas une question de résolution — c'est
ce texte qui se retrouvait tronqué en travers de votre bandeau.

Les deux colonnes coexistent, aucune ne remplace l'autre : vignette en liste, bandeau en
fiche.

---

## 4. `summary` et `players` — la source mesurée, pas supposée

La base de votes ne porte ni l'un ni l'autre : `title` a neuf colonnes, aucune description.
Mais la question qui décide n'est pas « est-ce en base », c'est **« qu'y a-t-il chez IGDB, et
à quel coût »**. L'import IGDB existe, tourne toutes les nuits, et ses identifiants sont en
place ; interroger la source était donc à portée de main.

**Mesure réelle sur 1 000 jeux échantillonnés à pas régulier sur les 7 581 exportés** :

| Champ IGDB | Couverture | Verdict |
|---|---|---|
| `summary` | **99,6 %** | disponible, quasi universel |
| `game_modes` | **97,5 %** | disponible (solo / multi / coop / MMO…) |
| `multiplayer_modes` | 21,4 % | marginal |
| dont un `offlinemax` exploitable | **12,1 %** | **insuffisant** |

### 4.1 `summary` — faisable, et bien moins cher que prévu

99,6 % de couverture. Médiane 488 caractères, maximum 3 652. **Poids ajouté à l'export :
≈ 4 Mo**, soit +39 % sur les 10,5 Mo actuels — le seul vrai arbitrage, et il vous revient
en partie puisque c'est votre appareil qui le télécharge.

**Langue : l'anglais, systématiquement.** IGDB ne fournit pas de synopsis traduits. Votre
§2.1 proposait une colonne `summary_lang` — elle porterait `'en'` sur 100 % des lignes.
**Recommandation : pas de colonne, une règle documentée** — ce qui est fait ici. Si un jour
une seconde langue apparaît, elle justifiera la colonne, et ce sera une mineure de plus.

### 4.2 `players` — votre format n'est pas atteignable, votre besoin l'est

C'est le point important, et il vous concerne directement.

**Le format demandé — « 1-4 » — n'existe que pour 12,1 % du catalogue.** `offlinemax` est
renseigné sur une petite minorité des titres rétro. Livrer `players TEXT` remplirait donc une
colonne vide sur près de neuf jeux sur dix, ce qui est pire qu'une absence : votre fiche
afficherait « nombre de joueurs : inconnu » comme cas dominant.

**Mais le besoin que vous exposiez — le filtre « jouable à plusieurs » — est servi à 97,5 %**
par `game_modes` : *Single player*, *Multiplayer*, *Co-operative*, *Split screen*, *MMO*.

D'où la contre-proposition, qui suppose votre accord parce qu'elle change la forme :

```sql
exp_game.game_modes  INTEGER   -- masque de bits : solo, multi, coop, splitscreen…
```

Un masque, comme `lang_mask`, avec sa table `exp_game_mode(mode, label, bit_index)` sur le
même patron que `exp_language` — donc filtrable par un ET binaire, sans jointure, exactement
comme les langues. Vous savez déjà lire cette structure.

**Ce que vous perdez** : afficher « 1-4 » sur une fiche. **Ce que vous gagnez** : le filtre
« jeux à deux » sur 97,5 % du catalogue au lieu de 12 %.

Si vous préférez malgré tout `players TEXT`, il est livrable — mais avec cette couverture
connue d'avance, et c'est votre décision, pas la nôtre.

### 4.3 Ce que ça coûte, et pourquoi ce n'est pas encore fait

Deux colonnes sur `title`, deux champs de plus dans `GAME_FIELDS`, un passage de backfill
sur 17 260 titres — qu'IGDB sert par lots de 500, donc **quelques dizaines de requêtes**, pas
une campagne.

Le vrai coût n'est pas là : c'est une **écriture sur la base de production**, qui est en
`journal_mode=delete` — un écrivain y bloque tous les lecteurs, et le site de vote est servi
depuis cette même base. Ça se fait dans une fenêtre choisie, pas au fil de l'eau.

**C'est donc prêt à être décidé, pas prêt à être livré.** Rien n'a été écrit en base.

---

## 5. Captures **par système** (§2.5) — non, et c'est structurel

Votre intuition était juste : **IGDB indexe ses médias par jeu, pas par plateforme.** Il n'y
a rien à extraire de la source actuelle, quel que soit l'effort.

Obtenir cette donnée imposerait d'ajouter une source spécialisée (ScreenScraper et
consorts), avec ses propres questions de licence, de volume et de rapprochement des
identifiants. Ce n'est pas un refus de principe, c'est un changement de source — à formuler
comme une demande à part entière si le besoin se confirme.

En attendant, `artwork_ref` couvre l'essentiel de l'usage, comme vous l'aviez vous-même
anticipé.

---

## 6. Pack d'images hors ligne (§2.6) — la question juridique reste ouverte

Vos mesures sont retenues : 27 Mo en vignettes 90×120, ce qui est raisonnable à côté de
l'export.

**Mais votre §2.7 pose le vrai point, et il n'est pas technique.** Redistribuer des jaquettes
IGDB dans un pack que nous hébergeons nous fait passer d'« afficheur d'URL » à
« distributeur ». Le backend ne tranchera pas ça par une décision d'implémentation.

Position d'attente, explicite : **`cover_ref` reste une URL en ligne**, et le mode hors ligne
garde cette limite, documentée des deux côtés. Le jour où la question des droits est réglée,
la production du pack est un travail court et la forme que vous préférez — fichier séparé,
indexé par `game_key`, avec sha256 publié — est celle qui sera retenue.

---

## 7. Ce qui a été ajouté sans que vous le demandiez

**Le générateur refuse désormais de publier un export dont les invariants ne tiennent pas.**

C'est la suite directe de la régression signalée dans la passation : insérer `artwork_ref` au
milieu d'`exp_game` a décalé un indice positionnel, `lang_mask` est parti dans `rating` et
valait **0 sur tout le catalogue**. Sans exception, sans avertissement — et `probe.py` a
conclu « Contrat respecté », parce qu'il ne vérifie pas les masques. **C'est votre contrôle de
cohérence qui l'a rattrapé, après génération.**

La passation suggérait d'ajouter ce contrôle à `probe.py`. Ce n'est pas le bon endroit :
`probe.py` vit dans **votre** dépôt et s'exécute **après** publication — au mieux il constate
les dégâts. Le contrôle appartient au générateur, avant la bascule.

`verify_export()` tourne donc sur le fichier `.new`, et la bascule atomique n'a lieu que si
tout passe : reconstruction de `lang_mask` depuis `exp_game_language` et comparaison jeu par
jeu, intégrité référentielle (`lang_code`, `crc32`, `game_key`), unicité des `bit_index`,
absence de chaîne vide là où le contrat promet `NULL`, plage des `release_year`, non-vacuité
des six tables, `journal_mode`, version de schéma.

Vérifié en contre-épreuve : muet sur un export sain ; sur une copie dont `lang_mask` est remis
à 0, il refuse et nomme les 5 986 jeux touchés. En cas d'échec, **l'export de la veille reste
en ligne** — l'URL publique ne peut plus servir un artefact faux.

Gardez votre contrôle de cohérence : deux vérifications indépendantes valent mieux qu'une, et
c'est la vôtre qui a trouvé celle-ci.

---

## 8. Sur la branche écrite depuis votre dépôt

`export-1.5.0-visuels` a été implémentée depuis la session frontend, ce que la passation
signale elle-même comme une entorse au partage des rôles. Le travail était juste — vérifié
ligne à ligne, et toutes ses mesures se confirment sur la base de production. Il est **relu,
complété du garde-fou ci-dessus, fusionné dans `tests` et déployé**.

Rien à refaire de votre côté. Pour la suite, le canal habituel — une demande ici, une réponse
à côté — reste le bon : c'est ce qui a permis de mesurer §4 plutôt que de le supposer.

---

## 9. Ce qui vous attend maintenant

1. **Câbler `artwork_ref`** — `hasRealBanner` a de quoi valoir `true` sur 95,6 % des fiches ;
2. **afficher `release_year`** par ligne de plateforme ;
3. **répondre sur §4.2** : masque `game_modes` à 97,5 %, ou `players TEXT` à 12 % ;
4. **arbitrer les 4 Mo** de `summary` sur un export qui en fait 10,5.

Les points 3 et 4 sont les seuls qui attendent une décision. Le reste est en ligne.

---

*Backend : `6036a0a` (bandeau + année), `f90f0b7` (vérification avant publication),
`06a9c8d` (fusion), branche `tests`, poussés. Export régénéré depuis la production le
2026-08-14 à 17:45 UTC.*

---

# Addendum du 2026-08-14 — **1.6.0 : `summary` et les modes de jeu sont prêts**

> **⚡ Mise à jour du 2026-08-16 : le 1.6.0 est EN PRODUCTION.** Tout ce que cette section
> annonçait comme « prêt mais non publié » est désormais servi à l'URL habituelle :
>
> ```
> https://igiris.xyz/exports/games.db   15,6 Mo   schéma 1.6.0
> généré 2026-08-16T11:43:43Z · sha256 ada63ecb…
> ```
>
> Chiffres constatés sur l'artefact publié : **7 562 synopsis (99,7 %)**, **7 359 jeux avec
> `mode_mask` (97,1 %)**, filtre « jouable à plusieurs » = **3 333 jeux**. Non-régression
> exacte — 5 986 jeux badgés, **0 masque incohérent**, 7 247 `artwork_ref`, 18 555
> `release_year`, 31 `is_preferred`. Vos trois outils valident : `fetch-export.sh`,
> `probe.py` (« Contrat respecté »), binaire `--export` (masques 20/20, CRC 23/23,
> romsets 87/87).
>
> Un `bash tools/fetch-export.sh` suffit à récupérer le nouvel export. **Votre binaire
> actuel continue de fonctionner sans modification** — la majeure n'a pas bougé.

Le §4 ci-dessus vous laissait deux décisions. **Elles ont été tranchées ici**, sur demande
du responsable du projet, et le schéma **1.6.0** est implémenté et validé. Ce n'est pas une
manière de court-circuiter le canal : c'est une décision produit qui vous est rapportée
telle quelle, et rien n'est irréversible tant que ce n'est pas déployé.

## Ce qui est prêt

```sql
exp_game.summary    TEXT      -- 7 562 / 7 581 (99,7 %), ANGLAIS, pas de summary_lang
exp_game.mode_mask  INTEGER   -- 7 358 / 7 581 (97,1 %)
exp_game_mode(mode_key, label, bit_index)   -- 6 lignes, registre figé à vie
```

**`players TEXT` n'est pas livré, et la mesure explique pourquoi** : sur les 17 260 titres,
`offlinemax` n'existe que pour 12,1 %. Vous auriez affiché « inconnu » comme cas dominant.

`mode_mask` suit **exactement le patron d'`exp_language`** : table de référence avec
`bit_index`, masque sur `exp_game`, filtrage par ET binaire. Votre implémentation de filtre
de langues se réutilise telle quelle.

```
bit 0 solo 7 316 · bit 1 multi 3 126 · bit 2 coop 968
bit 3 splitscreen 1 205 · bit 4 mmo 25 · bit 5 battleroyale 2
```

Votre filtre « jouable à plusieurs » (`mode_mask & 0b110`) ramène **3 333 jeux**. À comparer
aux ~900 qu'aurait permis `offlinemax`.

**La table est livrée complète, pas seulement les modes observés** — vos six entrées existent
toujours, donc votre menu de filtres se construit sans dépendre du contenu du catalogue.

## Le coût, qui vous concerne directement

**L'export passe de 10,5 à 14,9 Mo** (+42 %). Les synopsis pèsent ~4,5 Mo à eux seuls, et ils
ne servent qu'à l'affichage — ils n'entrent dans aucun *lookup*. Si ce poids vous gêne sur
l'appareil, dites-le : `summary` est le seul champ de l'export qu'on peut retirer sans rien
casser d'autre, et un export « léger » séparé reste possible.

## Ce qui n'est PAS encore en ligne

**Rien de tout cela n'est publié.** `https://igiris.xyz/exports/` sert toujours le **1.5.0**.

La chaîne complète a été validée sur une **copie de travail** de la base : 17 220 synopsis et
16 852 `game_modes` récupérés chez IGDB, export produit, et **vos deux outils le valident** —
`probe.py` conclut « Contrat respecté », et votre binaire `--export` confirme masques 20/20,
CRC 23/23, romsets 87/87. Ce qui manque est une écriture sur la base de production, qui
attend son créneau.

Le déploiement ne changera **ni la majeure, ni aucune colonne existante** : quand le 1.6.0
arrivera, votre binaire actuel continuera de fonctionner sans modification.

## Vérifié au passage — l'invariant tient

Les deux colonnes ont été insérées **avant `lang_mask`**, conformément à la règle posée après
la régression de la 1.5.0. Résultat sur l'export produit : **0 masque incohérent**, 5 986 jeux
badgés, identiques au 1.5.0. Le garde-fou du §7 couvre désormais aussi les modes (aucun bit
de `mode_mask` ne peut sortir du registre).

## Ce que ça vous demande

Deux propriétés à exposer, et vous avez déjà tout le reste :

- `summary` → la fiche a enfin un texte ;
- `mode_mask` → un filtre de plus, avec le code que vous avez déjà écrit pour les langues.

Et le rappel du §9 : `hasRealBanner` est toujours codé en dur à `false`
(`src/ui/GameDetailModel.h:123`), et `bannerRef()` retourne encore `m_coverRef` — donc
**`artwork_ref`, livré depuis cet après-midi, n'est pas encore affiché**. C'est la
modification la plus rentable de votre côté : deux lignes pour 95,6 % du catalogue.
