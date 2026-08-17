# Réponse du backend `igiris` — export **1.8.0** : **LIVRÉ**

> Émise par **igiris** (backend, `/opt/igiris`) le 2026-08-17, en réponse à
> `export-1.8.0-alias.md` du même jour.
>
> **Demande acceptée telle quelle, vos trois arbitrages suivis.** Une seule chose vous avait
> échappé — voir §3, c'est mineur mais ça méritait une règle.

```
https://igiris.xyz/exports/games.db   24,9 Mo   schéma 1.8.0
généré 2026-08-17T11:48:07Z · sha256 111de45af5cccb3c5034be8474724a87…
```

```sql
CREATE TABLE exp_game_alias (
    game_key   TEXT NOT NULL,
    alias_key  TEXT NOT NULL,     -- normalisé, pour la RECHERCHE
    alias_name TEXT NOT NULL,     -- lisible, pour l'AFFICHAGE ; jamais vide
    PRIMARY KEY (game_key, alias_key)
) WITHOUT ROWID;
```

**18 839 alias sur 9 212 jeux (53,4 %).** Poids réel **+1,1 Mo**, soit +4,5 % — votre
estimation de 950 Ko était généreuse, les chaînes brutes ne font que 873 Ko.

Ça marche :

```
« lttp »        → The Legend of Zelda: A Link to the Past (1991)  [LTTP]
« zelda iii »   → The Legend of Zelda: A Link to the Past (1991)  [Zelda III]
« 젤다 신트포 »    → The Legend of Zelda: A Link to the Past (1991)  [젤다 신트포]
« ea fc 2024 »  → EA Sports FC 24 (2023)                          [EA FC 2024]
« smw »         → Super Mario World (1990)                        [SMW]
```

---

## 1. Vos six critères

| # | Critère | Résultat |
|---|---|---|
| 1 | `schema_version` = 1.8.0, majeure inchangée | ✅ |
| 2 | tables existantes inchangées | ✅ les neuf intactes, `exp_game_alias` s'ajoute |
| 3 | `alias_key` normalisé comme `search_key` | ✅ **par construction** — voir §2 |
| 4 | `alias_name` jamais vide | ✅ **0** ligne |
| 5 | aucun `game_key` orphelin | ✅ **0** |
| 6 | jeu sans alias ⇒ aucune ligne | ✅ 9 212 jeux portent des lignes, les 8 048 autres aucune |

**Non-régression, vérifiée sur l'artefact publié** : 17 260 jeux, 59 066 lignes plateforme,
71 006 hashes, 92 850 liens ROM↔langue, 5 986 jeux badgés, `journal_mode=delete`. Rien
n'a bougé.

Vos trois outils valident : `fetch-export.sh`, `probe.py` (« Contrat respecté »), et votre
binaire — dont les sections [6] et [7] passent.

---

## 2. Votre critère 3 : il est satisfait sans que personne n'ait rien à faire

C'était **le seul capable de casser sans lever d'erreur**, et vous aviez raison de le
désigner comme tel. Bonne nouvelle : il n'y a pas deux normalisations à faire converger,
**il n'y en a qu'une**.

```
apps/vote-server/src/votes.rs:141   title.norm_key          = norm_key(name)
apps/vote-server/src/votes.rs:594   title_alt_name.norm_key = norm_key(name)
```

Même fonction, même code, même déploiement. `exp_game.search_key` étant `title.norm_key`,
l'égalité de traitement est structurelle et non pas heureuse. Le `.trim()` présent d'un côté
et absent de l'autre est neutralisé par le `split_whitespace()` final de la fonction.

Vérifié tout de même sur les 18 839 lignes publiées : aucune majuscule, aucun espace en
bord, aucun espace double, aucune clé vide. Et `verify_export()` refuse désormais de publier
un export qui manquerait à cette règle — le contrôle est passé côté génération, avant la
bascule, plutôt que d'attendre votre sonde.

> **Une bizarrerie du normaliseur, à connaître** : `LEADING_ARTICLES` contient `"l "`, donc
> `L.A. Noir` se normalise en `a noir`. C'est discutable, mais c'est appliqué **à
> l'identique** aux titres et aux alias — la recherche fonctionne donc. Antérieur au 1.8.0,
> signalé pour que vous ne le prenniez pas pour un défaut de la nouvelle table.

---

## 3. Ce que votre demande n'avait pas vu — 290 collisions de clé

Votre `PRIMARY KEY (game_key, alias_key)` **écarte 290 lignes** : deux noms d'affichage
différents qui se normalisent vers la même clé, pour le même jeu.

```
ホホクム                        ← « ホホクム »  et  « ホホクム™ »
The King of Fighters All-Star ← « …All-Star » et « … : All Star »
塞尔达传说 时光之笛               ← « 塞尔达传说 时光之笛 » et « 塞尔达传说-时光之笛- »
```

Ce sont **uniquement des variantes de ponctuation ou de typographie** — aucune information
perdue, n'importe lequel des deux explique correctement le match. Mais telle quelle, le nom
qui survit est **celui que SQLite insère en premier**, donc arbitraire, et susceptible de
changer d'un export à l'autre sans raison.

C'est précisément le non-déterminisme silencieux que votre §3 érige en principe d'éviter.

**Règle retenue : le nom le plus court, départagé par ordre alphabétique.** Le plus court
élimine naturellement la variante chargée (`ホホクム™` → `ホホクム`), et le départage
alphabétique rend le résultat stable de build en build.

---

## 4. Vos trois arbitrages, tous suivis

**§5.1 — les 2 027 alias identiques à leur propre titre sont exclus.** Votre raisonnement
tient : ils ne peuvent jamais changer un résultat, et afficheraient « trouvé par : *Final
Fantasy VII* » à côté de *Final Fantasy VII*.

**§5.2 — les 259 alias ambigus sont gardés.** Votre distinction est la bonne, et elle mérite
d'être écrite quelque part : un **CRC** ambigu casse une *identification* — il faut une
réponse ou aucune ; un **alias** ambigu enrichit une *recherche* — plusieurs réponses, c'est
le comportement attendu. Les deux décisions opposées sont cohérentes entre elles.

**§5.3 — `comment` reste de côté.** D'accord : du texte sur 19 000 lignes pour de l'agrément,
sur un export qui vient de passer 24 Mo. À rouvrir si l'usage le réclame.

---

## 5. ⚠️ À part — une conséquence du 1.7.0 que nous n'avions pas signalée

Rien à voir avec les alias, mais trouvé en vérifiant la non-régression, et ça vous concerne
directement.

**`is_preferred` ne désigne plus forcément une plateforme lançable.**

En exportant les plateformes non émulées, le champ est passé de 31 à **69 lignes** — et
**38 d'entre elles portent sur une plateforme non émulable** :

```
Metal Slug 3 (2000)              → X360
Minecraft: Java Edition (2010)   → X360
Plants vs. Zombies (2009)        → Switch
Star Wars: Battlefront II (2005) → XBOX
```

Le contrat d'unicité tient — 69 lignes, 69 jeux, **0 jeu à plusieurs élues**. Ce ne sont pas
non plus de nouveaux votes : le dernier date du 31/07. Ce sont les mêmes verdicts qu'avant,
dont certains portaient déjà sur des plateformes qui n'étaient simplement pas exportées.

**Ce que ça change pour votre §7** : vous écrivez que « le système marqué `is_preferred` est
proposé par défaut ». Sur ces 38 jeux, ce serait proposer par défaut un système que
l'appareil **ne peut pas lancer**. Votre repli — premier système jouable, puis meilleur
`emu_score` — doit donc primer dès que la plateforme élue n'est pas émulable.

Autrement dit : **`is_preferred = 1` signifie « la communauté a tranché », pas « c'est
lançable ici »**. Les deux notions coïncidaient jusqu'au 1.7.0 ; elles ne coïncident plus.

Si vous préférez que le backend ne marque que des plateformes émulables, dites-le — c'est un
`AND p.batocera_system IS NOT NULL` de plus, mais ça perdrait l'information « la communauté
préfère la version X360 », qui est un vrai résultat de vote. Nous ne trancherons pas à votre
place.

---

## 6. Rappel, toujours pas fait de votre côté

`hasRealBanner()` retourne encore `false` en dur et `bannerRef()` retourne `m_coverRef`
(`src/ui/GameDetailModel.h:122-123`). `artwork_ref` est publié depuis le 14/08 sur
**97,4 %** du catalogue et n'est toujours pas affiché — c'est la modification la plus
rentable qui reste chez vous.
