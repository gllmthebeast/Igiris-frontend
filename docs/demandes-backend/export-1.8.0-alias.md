# Demande au backend `igiris` — export **1.8.0** : les alias de noms de jeux

> Émise par **igiris-frontend** le 2026-08-17, depuis la version 1.7.0.
> Ajout **strictement additif** → version **mineure**. Un frontend 1.7.0 continue de lire
> un export 1.8.0 sans modification : il contrôle la majeure, pas la mineure (§2).
>
> Mesures faites sur `backups/votes-20260817-041503.db`, **pas sur la production** — la
> règle du §13 tient, ce dépôt n'ouvre jamais votre base en écriture.

---

## 0. Ce que ça vous coûte : **rien sur votre base**

C'est le point à lire en premier, parce qu'il change la nature de la demande.

| | |
|---|---|
| `votes.db` — schéma | **aucun changement** |
| `votes.db` — écriture | **aucune**. Pas de backfill, pas de ré-import IGDB |
| Fenêtre de maintenance | **aucune** |
| `scripts/build-export.py` | un `CREATE TABLE` et un `SELECT … JOIN` |
| Régénérer et publier | l'action habituelle |

`title_alt_name` **existe déjà, peuplée et à jour** : `night-import.py` appelle
`put_alt_names()` à chaque passage nocturne, et `norm_key` est renseigné sur **100 %** des
21 152 lignes — compté, pas supposé.

Et `build-export.py` ouvre la base en `file:…?mode=ro` : **produire l'export ne l'écrit
jamais**. Rien à voir, donc, avec la 1.6.0, où obtenir les synopsis avait imposé une
écriture sur une base en `journal_mode=delete` — un écrivain y bloque tous les lecteurs, site
de vote compris, d'où l'attente d'un créneau.

**Ici il n'y a rien à collecter, rien à migrer, rien à attendre.** Toute la demande consiste
à transporter une donnée qui dort déjà dans votre base depuis les imports nocturnes.

L'essentiel du travail est chez nous.

---

## 1. Pourquoi maintenant

`title_alt_name` existe et est peuplée : **21 152 alias sur 10 108 titres**, et les 10 108
sont **tous** dans l'export. C'est **58,6 % du catalogue** qui porte au moins un autre nom
que celui qu'on affiche.

Aujourd'hui la recherche du frontend porte sur `exp_game.search_key` seul, c'est-à-dire sur
**un unique nom canonique**. Conséquence concrète, sur un jeu que tout le monde connaît :

```
The Legend of Zelda: A Link to the Past
  ├─ LTTP                              Abbreviation
  ├─ TLoZ: ALttP                       Acronym
  ├─ Zelda III                         Alternative spelling
  ├─ Zelda no Densetsu: Kamigami…      Japanese title - romanised
  ├─ 젤다 신트포                          Korean title - abbreviated
  └─ 塞尔达传说 - 众神的三角力量               Chinese title - simplified
```

Taper `LTTP` sur une borne ne donne **rien**. Taper le titre japonais non plus. C'est la
recherche la plus naturelle pour la moitié des utilisateurs d'un frontend de rétrogaming,
et elle échoue en silence.

Le §0 interdit tout rapprochement de titres sur l'appareil, donc le frontend **ne peut pas**
combler ça localement. La donnée existe chez vous, normalisée : il ne manque que le
transport.

---

## 2. Ce qui est demandé

```sql
CREATE TABLE exp_game_alias (
    game_key   TEXT NOT NULL,   -- = exp_game.game_key
    alias_key  TEXT NOT NULL,   -- = title_alt_name.norm_key, pour la RECHERCHE
    alias_name TEXT NOT NULL,   -- = title_alt_name.name, pour l'AFFICHAGE
    PRIMARY KEY (game_key, alias_key)
);
```

`WITHOUT ROWID` si ça vous convient, comme `exp_game_language`.

**Les deux colonnes sont nécessaires, et elles doivent rester dans la même ligne** — le §3
explique pourquoi c'est le point structurant de cette demande.

---

## 3. Pourquoi une TABLE, et pas une colonne concaténée

Il existe une solution à coût nul de votre côté comme du nôtre : coller les `norm_key` dans
`exp_game.search_key`, séparés par un caractère de contrôle. Le frontend n'aurait **pas une
ligne à changer** et la recherche marcherait le soir même.

Nous l'écartons, et voici la raison.

### Le piège : une ligne qui correspond sans qu'on voie pourquoi

L'utilisateur tape `LTTP`. La liste affiche *The Legend of Zelda: A Link to the Past* — un
titre qui **ne contient aucun des caractères tapés**. À l'écran, ça ne se distingue pas d'un
bug : la recherche a l'air de renvoyer n'importe quoi.

La ligne doit donc afficher l'alias qui a mordu :

```
The Legend of Zelda: A Link to the Past (1991)        · LTTP ·
```

C'est exactement la classe de problème rencontrée en 1.7.0 avec `lang_catalog_mask` : un
filtre élargi dont le résultat ne s'explique plus tout seul. On l'a réglé en montrant
l'information, pas en la cachant.

### Ce que ça impose au schéma

Pour afficher l'alias, il faut le **nom lisible**, pas la clé normalisée — sinon on affiche
`lttp` en minuscules, ou le titre japonais dépouillé de sa ponctuation. Donc la clé de
recherche et le nom d'affichage doivent voyager **ensemble**.

Trois formes possibles, et pourquoi une seule tient :

| Forme | Verdict |
|---|---|
| `search_key` concaténé | Gratuit, mais la clé normalisée ne s'affiche pas. Le match devient inexplicable. **Non** |
| Deux colonnes parallèles `alias_keys` / `alias_names` | Deux listes à garder alignées, indice par indice. **Non** |
| **Une table, une ligne par alias** | La paire est atomique par construction. **Oui** |

Sur la deuxième forme, nous insistons parce que ce n'est pas une préférence de style : ce
duo de projets s'est déjà fait piéger **deux fois** par un désalignement positionnel
silencieux — `lang_mask` parti dans `rating` sur tout le catalogue en 1.5.0, sans une seule
exception ni un seul avertissement. Deux listes parallèles rejouent ce scénario à chaque
alias ajouté ou retiré. Une table le rend impossible.

---

## 4. Ce que le frontend en fera

**Recherche** : `alias_key` est testé **seulement si le titre ne correspond pas**. Le cas
courant ne coûte donc rien de plus.

**Affichage** : `alias_name` apparaît sur la ligne, et uniquement quand c'est **lui** qui a
produit le résultat. Un jeu trouvé par son titre n'affiche aucun alias — l'information
n'aurait rien à expliquer.

**Rien d'autre.** Pas de classement par pertinence, pas de correspondance approximative, pas
de suggestion « vouliez-vous dire ». Le §0 est catégorique et il ne change pas ici : l'appareil
fait des *lookups*, pas des rapprochements.

---

## 5. Trois points à trancher, chez vous

Nous les signalons parce que nous les avons mesurés, pas pour vous dicter la réponse.

### 5.1 — 2 025 alias sont identiques à leur propre titre

**9,6 % des lignes** ont un `norm_key` égal à celui de leur titre. Elles ne peuvent donc
**jamais** changer un résultat de recherche : le titre aurait déjà mordu.

Pire, elles nuisent : la ligne afficherait « trouvé par : *Final Fantasy VII* » à côté du
titre *Final Fantasy VII*, ce qui se lit comme une redite ou comme un bug d'affichage.

**Notre préférence : les exclure à l'export** (`WHERE a.norm_key <> t.norm_key`). Elles sont
utiles dans votre base — elles tracent ce qu'IGDB a renvoyé — mais elles n'ont rien à faire
sur l'appareil.

### 5.2 — 259 alias désignent PLUSIEURS jeux

Un même `norm_key` apparaît sur au moins deux titres différents. Ce n'est pas un défaut de
données : `Zelda` désigne légitimement plusieurs jeux.

Pour nous ce n'est **pas un problème** : les deux jeux remontent, l'utilisateur choisit,
c'est le comportement attendu d'une recherche. On le signale parce que le §11 vous a fait
prendre la décision **inverse** sur les CRC ambigus — exclus de l'export, parce qu'un lookup
ambigu serait trompeur.

La différence est réelle : un CRC ambigu casse une **identification** (une réponse, ou
aucune), un alias ambigu enrichit une **recherche** (plusieurs réponses, c'est normal).
**Nous demandons de les garder** — mais la décision est la vôtre, et si vous les excluez,
dites-le, on ne le devinera pas.

### 5.3 — Le `comment` : à laisser de côté

`title_alt_name.comment` porte le contexte (« Japanese title », « Acronym », « Abbreviation »)
et il est renseigné à **96,5 %**. Il rendrait l'affichage plus riche : « LTTP *(abréviation)* ».

**Nous ne le demandons pas au premier jet.** C'est du texte en plus sur 19 000 lignes pour un
gain d'agrément, sur un export qui vient de passer à 23,8 Mo. À rouvrir si l'usage le
réclame.

---

## 6. Critères d'acceptation, côté frontend

1. `exp_meta.schema_version` = `1.8.0` — **majeure inchangée**, sinon le frontend refuse de
   charger, à raison (§2) ;
2. les tables existantes sont **inchangées** : c'est ce qui fait la mineure ;
3. `alias_key` est **déjà normalisé**, selon exactement la même règle que
   `exp_game.search_key` — l'appareil ne normalise rien, jamais (§0). Si les deux
   normalisations divergent, la recherche échouera sur les cas mêmes que cette demande veut
   couvrir, et **sans erreur** ;
4. `alias_name` n'est **jamais vide** : une ligne sans nom affichable serait un alias qu'on
   ne peut pas expliquer, donc pire qu'une ligne absente ;
5. tout `game_key` d'`exp_game_alias` existe dans `exp_game` — pas d'orphelin ;
6. un jeu **sans alias** n'a simplement aucune ligne. Pas de ligne vide, pas de sentinelle.

Le point **3** est le seul qui puisse casser en silence, et c'est donc celui qu'on vérifiera
en premier : notre contrôle `--export` comparera un échantillon d'`alias_key` à la
normalisation de son propre `alias_name`.

---

## 7. Le coût, tel qu'on l'a mesuré

| | |
|---|---|
| Lignes après exclusion des redondantes (§5.1) | **~19 100** |
| Poids brut (`game_key` + les deux colonnes) | **≈ 950 Ko** |
| Sur un export à 23,8 Mo | **+4 %** |
| Mémoire sur l'appareil | de l'ordre du Mo — négligeable, même sur un Pi |

**Recherche** : aujourd'hui **1,9 ms par frappe** sur les 17 260 jeux, mesuré au banc sur la
VM de développement. Les alias n'ajoutent un test que sur les lignes dont le titre ne
correspond pas, soit ~1,1 test supplémentaire par ligne en moyenne. **Estimation : 3 à 4 ms**,
toujours très en dessous des 16 ms d'une image.

⚠️ Ce dernier chiffre est une **estimation, pas une mesure** — il sera mesuré au banc avant
la livraison côté frontend, comme l'a été celui du 1.7.0.

---

## 8. Ce que ça débloque

- Chercher `LTTP`, `FF7`, `SMW` ou un titre japonais **fonctionne**, sur 58,6 % du catalogue ;
- la recherche cesse d'être un test de mémoire du titre officiel exact, ce qui est
  précisément ce qu'on ne peut pas demander à quelqu'un devant un téléviseur, avec une
  manette ;
- et le frontend ne fait toujours **aucun rapprochement** : il compare des chaînes que vous
  avez normalisées. La décision structurante du §0 est intacte.
