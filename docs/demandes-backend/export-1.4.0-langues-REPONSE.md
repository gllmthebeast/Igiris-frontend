# Réponse du backend `igiris` — export **1.4.0** : **LIVRÉ**

> Émise par **igiris** (backend, `/opt/igiris`) le 2026-08-09, en réponse à
> `export-1.4.0-langues.md` du 2026-08-08.
>
> **Le lot 8 est débloqué.** L'export 1.4.0 est en production, et la seconde anomalie
> signalée (`is_preferred`, §8 de la demande) est corrigée dans la même livraison.

---

## 1. Ce qui est en ligne

```
https://igiris.xyz/exports/games.db        9,6 Mo   schéma 1.4.0
https://igiris.xyz/exports/manifest.json   généré 2026-08-09T14:02:16Z
sha256  225dc089b54a027ac63cce530b4f3a9b517f0d5fa67ce35dbf6f5114f3d89e88
```

Les trois objets demandés existent, avec exactement la structure spécifiée :

```sql
exp_language(lang_code, label, badge_asset, bit_index)
exp_game_language(game_key, lang_code, batocera_system, crc32)   -- WITHOUT ROWID
exp_game.lang_mask   INTEGER
```

**Résultat :** 25 codes de langue · 92 850 liens ROM↔langue · **5 986 jeux badgés sur
7 581 (79 %)**.

> ⚠️ Une seule différence avec votre spécification, et elle est **cosmétique** : la
> troisième colonne d'`exp_game_language` s'appelle `batocera_system`, pas `platform_key`.
> C'est délibéré — elle porte le **même nom que dans les trois tables existantes**, pour ne
> pas introduire deux conventions dans un même export. Votre §9.1 tient toujours : le
> renommage se fera d'un bloc, en 2.0.0, sur les quatre tables à la fois. Votre façade
> `platform_key` reste le bon endroit pour l'absorber.

---

## 2. Vos six critères d'acceptation

Vérifiés un par un sur l'artefact réellement publié :

| # | Critère | Résultat |
|---|---|---|
| 1 | `schema_version` = 1.4.0, majeure inchangée | ✅ |
| 2 | `exp_language` non vide, `bit_index` unique | ✅ 25 codes, 22 portent un bit, 22 valeurs distinctes |
| 3 | tout `lang_code` d'`exp_game_language` existe dans `exp_language` | ✅ **0** orphelin |
| 4 | tout `crc32` existe dans `exp_rom_hash` | ✅ **0** orphelin |
| 5 | `lang_mask` = OU binaire des `bit_index` | ✅ **0** écart sur 5 986 jeux |
| 6 | tables existantes inchangées | ✅ les cinq intactes, seule `exp_game.lang_mask` s'ajoute |

Le critère 4 était le seul qui décidait vraiment — sans lui aucune ROM locale n'aurait
jamais pu allumer un badge. Il est vérifié à zéro orphelin.

**Vos propres outils confirment**, exécutés depuis votre dépôt sur l'export en ligne :
`tools/fetch-export.sh` télécharge et valide le sha256, puis `tools/probe.py` conclut
**« Contrat respecté »**.

---

## 3. La décision que vous nous laissiez — `Europe` et `World`

Vous demandiez de trancher et de documenter. **Option prudente retenue : aucune langue
n'est déduite pour `Europe`, `World` ni `Asia`.**

Le raisonnement est le vôtre : un badge faux est invérifiable par l'utilisateur. Déduire
`en` aurait fabriqué des badges sur ~52 500 lignes sans qu'aucune donnée ne le justifie.

Une troisième voie a été **mesurée puis écartée** : récupérer les langues d'une autre
entrée du même titre. Couverture 12,9 %, et **fausse** — `norm_key` regroupe jusqu'à 11 dats
différents (« aladdin » réunit le Game Boy, la NES et des pirates taïwanais), ce qui
attribuait « ja » à des sorties européennes.

Le repli ne porte donc que sur les régions à langue **certaine** :

```
balise de langues explicite   15 403 liens
repli région → langue         40 101 liens   (Japan→ja, USA→en, Spain→es, Germany→de…)
écarté, région multilingue    12 877 lignes
```

**Conséquence pratique pour vous :** un jeu européen sans balise n'aura pas de badge. Ce
n'est pas une donnée manquante à corriger côté appareil, c'est une information que le dat
ne contient pas.

---

## 4. `bit_index` — votre option 1, et le registre est figé

Vous proposiez trois issues au budget de 63 bits ; **l'option 1 est retenue**.

**24 langues affichables** reçoivent un bit : `en fr es de it ja nl pt sv da ru no ko fi pl
zh cs hu tr el ar sk ca hr`. Les autres codes restent dans `exp_game_language` — donc
filtrables — simplement absents du masque. `bit_index` y vaut `NULL` : **traitez `NULL`
comme « pas de bit », pas comme le bit 0.**

Côté backend, `LANG_BIT_REGISTRY` est une **liste ordonnée et figée à vie**, avec la règle
écrite dans le code : on n'insère jamais au milieu, on ne retire jamais, une langue nouvelle
s'ajoute **en fin de liste**. Une langue retirée du service garde son bit, mort.

Il reste **39 bits** de marge. Le débordement silencieux que vous redoutiez ne peut plus
arriver dans un avenir prévisible.

---

## 5. Votre §8 — `is_preferred` : corrigé dans la même livraison

Vous l'aviez signalé « à corriger indépendamment ». C'est fait, et le diagnostic mérite
d'être partagé parce qu'il **change la façon dont vous devez lire ce champ**.

**La cause.** Le marquage testait `elo = MAX(elo) du titre`. Un variant jamais comparé reste
à l'elo de départ, 1500 : tous les variants d'un titre sans vote étaient donc à égalité au
sommet, donc tous « élus ». **Le champ décrivait l'absence de vote**, pas un choix de la
communauté.

Chiffre qui explique tout : sur la base de production, **83 titres seulement ont reçu un
vote**, sur 7 581 exportés.

**Le correctif.** Trois conditions nécessaires : le variant a été comparé, son elo est le
maximum du titre, et ce maximum n'est atteint que par **lui seul** — une égalité au sommet
n'élit personne.

```
avant : 18 116 lignes marquées (97,6 %) · 3 932 jeux à plusieurs élues
après :      31 lignes marquées ( 0,2 %) ·     0 jeu  à plusieurs élues
```

### Ce que ça implique pour votre §7

**Votre repli n'est plus un pis-aller : il devient le cas normal.** Sur la quasi-totalité du
catalogue `is_preferred` vaut désormais 0, et « premier système jouable, puis meilleur
`emu_score` » est le bon comportement — gardez-le.

Ce que vous gagnez, c'est de pouvoir **distinguer deux situations** qui étaient
indiscernables : `is_preferred = 1` signifie maintenant « la communauté a réellement
tranché » (31 lignes, et l'unicité est garantie côté génération), et son absence signifie
« personne n'a encore voté ». Si vous voulez le montrer à l'utilisateur, c'est désormais une
information fiable.

La colonne se remplira d'elle-même à mesure que les votes arrivent, sans nouvelle livraison.

---

## 6. Un défaut corrigé au passage, qui vous concerne

Le générateur écrivait **directement dans le répertoire servi par Caddy** : il supprimait
`games.db` puis le réécrivait pendant les ~5 minutes du build. L'URL publique renvoyait donc
404, puis un fichier tronqué, à chaque génération.

Votre `fetch-export.sh` s'en protégeait déjà correctement (vérification du sha256 avant
bascule), mais il aurait échoué sans raison visible s'il était tombé dans cette fenêtre.

La construction se fait désormais à côté (`games.db.new`) suivie d'un `os.replace()`
atomique. Vérifié en cours de build : l'export servi conserve son sha et sa taille jusqu'à
la bascule. **L'URL ne renvoie plus jamais qu'un export complet** — l'ancien ou le nouveau.

---

## 7. Signalé en amont, pour votre information

Les dats contiennent des formes composées au « + » (`De+En`, `En+En+En`) : le parseur de
langues de l'import découpe mal certaines balises. Elles sont **éclatées à la génération**
plutôt que perdues, donc vos données sont correctes ; la cause reste à corriger côté import.
Aucune action de votre côté.

---

## 8. Ce qui reste ouvert, et qui n'est pas de votre ressort

- **`platform_key`** (votre §9.1) — renommage des quatre tables, **2.0.0**. Pas planifié :
  c'est une rupture majeure pour un gain de nommage, et votre façade absorbe déjà le sujet.
  À déclencher par une demande de votre part si le besoin devient réel.
- **Header SNES** (votre §9.3) — non traité côté backend. **L'heuristique locale
  `taille % 1024 == 512` reste la voie recommandée** : elle n'attend personne et ne double
  pas le volume de la table de hashes.
- **Pack d'images hors ligne** (votre §11) — `cover_ref` reste une URL IGDB. Faisable côté
  backend, mais c'est une demande à formuler : volume, droits de redistribution et cadence
  de rafraîchissement sont à cadrer avant.

---

*Backend : commits `8bd5a89` (langues), `994cc26` (`is_preferred`), `a8ae162` (bascule
atomique), branche `tests`, poussés. Détail dans `/opt/igiris/docs/TODO-app-igiris.md`,
lots 7 et 8.*
