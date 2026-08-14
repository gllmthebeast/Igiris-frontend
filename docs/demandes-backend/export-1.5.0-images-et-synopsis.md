# Demande au backend `igiris` — export **1.5.0** : visuels, synopsis et métadonnées

> Émise par **igiris-frontend** le 2026-08-14, depuis la version 1.3.0.
> Ajout **strictement additif** → version **mineure**. Un frontend 1.3.0 continue de lire
> un export 1.5.0 sans modification : il contrôle la majeure, pas la mineure (§2).

---

## 1. Pourquoi maintenant

Le lot 11 vient de livrer l'affichage des jaquettes, en lisant `cover_ref` — donc **par le
réseau**. C'est la seule entorse au hors-ligne que le §11 autorise, et elle a un coût réel
qu'on peut maintenant constater plutôt que supposer :

- sur un appareil **sans réseau**, la liste n'a aucune image, définitivement ;
- sur un appareil connecté, chaque défilement déclenche des requêtes vers `images.igdb.com`,
  avec la latence et la dépendance à un tiers que ça implique ;
- une URL qui change ou expire côté IGDB casse l'affichage sans que personne ne le sache.

La fiche de jeu, elle, n'a **rien à afficher** : l'export ne porte aucune description.

---

## 2. Ce qui est demandé

### 2.1 Un synopsis — ajout de colonne

```sql
ALTER TABLE exp_game ADD COLUMN summary TEXT;
```

Quelques lignes de présentation, telles qu'IGDB les fournit. Le §7 prévoit « jaquette,
métadonnées » sur la fiche ; aujourd'hui la fiche affiche un titre et une note, rien qui
donne envie d'ouvrir un jeu qu'on ne connaît pas.

**Langue** : le français si IGDB le fournit, l'anglais sinon. Le frontend n'a pas d'avis,
mais il a besoin de savoir **laquelle** est stockée — une colonne `summary_lang` réglerait
la question, ou une règle documentée.

### 2.2 Le nombre de joueurs — ajout de colonne

```sql
ALTER TABLE exp_game ADD COLUMN players TEXT;   -- « 1 », « 1-2 », « 1-4 »…
```

Demandé par l'usage : sur une borne, « à combien peut-on y jouer » est la question qu'on se
pose avant d'ouvrir un jeu qu'on ne connaît pas. C'est aussi un **filtre** évident à terme
(« jeux à deux »), au même titre que la décennie ou la langue.

En **texte** et non en entier : la donnée réelle est un intervalle, et « 1-4 » ne se range
pas dans un `INTEGER` sans perdre l'information. Si vous préférez deux colonnes
(`players_min`, `players_max`), c'est aussi bien — le frontend s'adapte, il a juste besoin
que le choix soit fait une fois.

### 2.3 L'année de sortie **par plateforme**

```sql
ALTER TABLE exp_game_platform ADD COLUMN release_year INTEGER;
```

Aujourd'hui `exp_game.year` porte **une seule année pour le jeu**, celle de sa première
sortie. Or un même jeu sort souvent à plusieurs années d'écart selon la machine, et c'est
précisément ce que la fiche de jeu montre : une liste de plateformes.

Exemple type : un titre arcade de 1987 porté sur NES en 1988, sur Amiga en 1989 et ressorti
sur console virtuelle vingt ans plus tard. La fiche affiche ces plateformes côte à côte, et
ne peut associer à aucune sa propre date.

IGDB expose des `release_dates` par plateforme : la donnée existe en amont. `NULL` quand
elle est inconnue — le frontend n'affichera alors rien, ce qui est correct.

### 2.4 Une image de **bandeau**, distincte de la jaquette

C'est la demande la plus visible aujourd'hui, et la seule qu'aucun contournement ne sauve.

```sql
ALTER TABLE exp_game ADD COLUMN artwork_ref TEXT;
```

La fiche de jeu a désormais un **bandeau** en haut. Il ne peut afficher que `cover_ref`,
faute d'autre image — c'est-à-dire une **jaquette recadrée en large**. Le résultat est
médiocre par nature :

- une jaquette est **verticale** ; l'étirer en bandeau en montre une bande centrale
  arbitraire ;
- elle porte du **texte** — logo, mentions légales, code-barres — qui se retrouve tronqué
  en travers du bandeau ;
- c'est la **même image** que la vignette affichée juste à côté, ce qui appauvrit la fiche
  au lieu de l'enrichir.

Le frontend affiche donc ce bandeau **volontairement discret** (opacité 0,22) tant qu'il
sait qu'il ne s'agit pas d'une vraie illustration. La propriété `hasRealBanner` existe déjà
côté frontend : livrer `artwork_ref` suffit à basculer le rendu, sans rien redessiner.

IGDB expose des **`artworks`** — des illustrations horizontales, prévues exactement pour cet
usage — et des **`screenshots`**. Les deux conviennent ; les `artworks` sont préférables
parce qu'ils sont composés, là où une capture est prise au hasard d'une partie.

### 2.5 Des captures **par système** — la question ouverte

Demandé par l'usage : voir à quoi ressemble le jeu **sur la machine qu'on va lancer**. Une
version NES et une version Amiga du même titre n'ont rien à voir à l'écran, et c'est
précisément le choix que la fiche propose.

⚠️ **Le frontend ignore si c'est réalisable, et ne prétend pas trancher.** Autant qu'on
sache, IGDB indexe ses médias **par jeu**, pas par plateforme : il n'y aurait donc rien à
extraire de la source actuelle. Les bases spécialisées du rétrogaming — ScreenScraper et
consorts — fournissent bien du média par système, mais changer ou ajouter une source est
une décision de fond côté backend, avec ses propres questions de licence et de volume.

La demande est donc posée comme **une question, pas une exigence** : est-ce atteignable
depuis les sources actuelles ? Si non, un `artwork_ref` unique par jeu (§2.4) couvre déjà
l'essentiel du besoin, et c'est lui qu'il faut livrer en premier.

### 2.6 Un pack d'images hors ligne

Le §11 le signale depuis le début comme « à demander ». Volume mesuré sur les vraies
images IGDB, pas estimé :

| Variante | Dimensions | Taille unitaire | Pack complet (7 580) |
|---|---|---|---|
| `t_cover_small` | 90 × 120 | 3,6 Ko | **≈ 27 Mo** |
| `t_cover_big` | 264 × 352 | 19 Ko | ≈ 144 Mo |

**La vignette suffit largement** pour la vue liste, où le frontend affiche 33 × 44 points.
27 Mo à côté des 9,6 Mo de l'export est un rapport acceptable, y compris sur un appareil
modeste.

Trois points à trancher **côté backend**, et le frontend n'a pas d'avis sur le résultat mais
a besoin qu'il soit documenté :

- **Forme de livraison.** Un fichier à part (`covers.zip`, `covers.sqlite`) téléchargé comme
  l'export, ou des BLOB dans l'export lui-même ? Séparé a notre préférence : ça garde
  `games.db` léger pour qui ne veut pas les images, et ça permet de ne pas retélécharger
  9,6 Mo quand seules les images changent.
- **Nommage.** Idéalement indexé par `game_key`, pour que le frontend n'ait aucune règle de
  correspondance à appliquer — la même logique que partout ailleurs : l'appareil fait des
  *lookups*, pas des rapprochements (§0).
- **Cadence de rafraîchissement.** Mensuelle, comme l'export ? Ou seulement quand des
  jaquettes changent ?

### 2.7 ⚠️ Droits de redistribution — le vrai point bloquant

C'est la question à régler **avant** toute décision technique.

Les jaquettes viennent d'IGDB, qui les tient des éditeurs. Les redistribuer dans un pack que
nous hébergeons n'est pas la même chose que d'afficher une URL pointant chez eux : dans le
premier cas nous devenons le distributeur. Le §16 du frontend pose déjà la règle pour les
assets — « n'embarquer que des icônes redistribuables commercialement, ou les produire en
propre » — et elle s'applique ici avec bien plus d'enjeu.

**Le frontend ne peut pas trancher ça.** Si la redistribution n'est pas possible, l'option
`cover_ref` en ligne reste en place et fonctionne : ce n'est pas bloquant pour nous, c'est
une limite à assumer et à documenter.

---

## 3. Critères d'acceptation, côté frontend

1. `exp_meta.schema_version` = `1.5.0` — **majeure inchangée**, sinon le frontend refuse de
   charger, à raison (§2) ;
2. les tables existantes sont **inchangées** : c'est ce qui fait la mineure ;
3. `summary` et `players` sont `NULL` ou renseignés — jamais une chaîne vide, qui
   obligerait le frontend à deux tests là où un seul suffit ;
4. `exp_game_platform.release_year` est `NULL` ou une année plausible ; il ne **remplace
   pas** `exp_game.year`, qui reste la date de référence du jeu ;
5. si un pack d'images est livré : chaque entrée est **retrouvable par `game_key` seul**,
   sans règle de correspondance côté appareil ;
6. le pack porte une empreinte **sha256** publiée, comme l'export : `fetch-export.sh` vérifie
   avant de basculer, et le même mécanisme doit s'appliquer.

---

## 4. Ce que ça débloque

- La fiche de jeu cesse d'être une liste de systèmes : elle devient une **fiche**. Elle
  affiche déjà tout ce que l'export permet — jaquette, année, note, et pour l'arcade le
  matériel réel et l'état du pilote MAME. Il lui manque ce qui raconte le jeu.
- Un filtre « jouable à plusieurs », qui est demandé et qu'aucune donnée actuelle ne permet.
- Le frontend devient **entièrement hors ligne**, ce qui est sa promesse depuis le §0. Il
  reste aujourd'hui une exception, et c'est la seule.
- Un appareil sans réseau — le cas d'usage le plus courant d'une borne de rétrogaming —
  affiche enfin la même chose qu'un appareil connecté.

---

## 5. Rappel — ce que le frontend ne fera jamais

Aucun *scraping* sur l'appareil. Le §0 est catégorique : l'appareil ne fait **aucun
rapprochement de titres**, et le §10 lui interdit toute requête réseau pour afficher une
fiche. Récupérer des visuels en interrogeant un service depuis le Raspberry Pi reviendrait à
refaire, sur chaque appareil et avec des résultats divergents, le travail que le backend fait
une fois pour tous. C'est précisément la décision structurante du duo de projets.

C'est pourquoi cette demande est adressée au backend, et pas résolue localement.
