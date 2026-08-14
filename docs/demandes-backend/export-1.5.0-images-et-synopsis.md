# Demande au backend `igiris` — export **1.5.0** : pack d'images et synopsis

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

### 2.2 Un pack d'images hors ligne

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

### 2.3 ⚠️ Droits de redistribution — le vrai point bloquant

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
3. `summary` est `NULL` ou du texte — jamais une chaîne vide, qui obligerait à deux tests ;
4. si un pack d'images est livré : chaque entrée est **retrouvable par `game_key` seul**,
   sans règle de correspondance côté appareil ;
5. le pack porte une empreinte **sha256** publiée, comme l'export : `fetch-export.sh` vérifie
   avant de basculer, et le même mécanisme doit s'appliquer.

---

## 4. Ce que ça débloque

- La fiche de jeu cesse d'être une liste de systèmes : elle devient une **fiche**.
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
