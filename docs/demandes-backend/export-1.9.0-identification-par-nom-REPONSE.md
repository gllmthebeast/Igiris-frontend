# Réponse du frontend — export **1.9.0** : identifier par nom de fichier

> Émise par **igiris-frontend** le 2026-08-18, en réponse à
> `notes/export-1.9.0-identification-par-nom.md` du même jour.
>
> **D'accord sur le principe.** Vos quatre questions ont une réponse ; la première a été
> tranchée par une mesure et pas par un avis. Un cinquième point vous a échappé — voir §5,
> c'est le même piège que les collisions d'alias du 1.8.0.

---

## 1. Table dédiée ou extension d'`exp_romset` ? → **TABLE DÉDIÉE, et ce n'est pas un goût**

Étendre `exp_romset` **casserait un filtre visible par l'utilisateur**, en silence.

`arcadePlatformKeys()` est un `SELECT DISTINCT batocera_system FROM exp_romset`, et cette
même liste alimente **deux choses** chez nous :

```
src/scan/RomScanner.cpp:55      quel chemin d'identification prendre
src/ui/GameListModel.cpp        entry.isArcade → le filtre « Type : arcade » du §6
```

Y ajouter des lignes DOS ferait donc de `dos` une plateforme d'arcade, et **tous les jeux
DOS deviendraient « arcade »** aux yeux du filtre. Compté sur l'export 1.8.0 en cours :

```
filtre « arcade » aujourd'hui        833 jeux
filtre « arcade » si dos entre dans exp_romset   1 536 jeux
```

**703 jeux DOS remonteraient sous un filtre « arcade ».** Aucune erreur, aucun message :
juste un filtre qui ment. C'est exactement la classe de dégât que le §8 et le §11 passent
leur temps à éviter.

`exp_game_file` est donc le bon choix, et sa forme telle que vous la proposez nous va.

> **Conséquence côté frontend, à notre charge** : nous exposerons deux listes distinctes —
> « plateformes identifiées par nom de romset » (arcade, pour le filtre **et** le scan) et
> « plateformes identifiées par nom de fichier » (pour le scan seulement). Deux concepts qui
> partagent un mécanisme ne sont pas le même concept.

---

## 2. Quelle clé de comparaison ? → **nom sans extension, en minuscules. RIEN D'AUTRE.**

Notre scanner calcule déjà exactement ceci, pour l'arcade :

```cpp
// src/scan/RomScanner.cpp:106
const QString romset = QFileInfo(path).completeBaseName().toLower();
```

`completeBaseName()` retire la dernière extension, `toLower()` fait le reste. **Si votre
`file_key` est produit par cette règle-là, il n'y a rien à écrire chez nous.**

⚠️ **Attention à « espaces normalisés »**, que vous mentionnez. Nous ne normalisons pas les
espaces, et nous ne le ferons pas de notre propre initiative : le §0 interdit à l'appareil
d'inventer une transformation. Si vous repliez les espaces doubles et que nous ne le faisons
pas, un fichier portant un espace double **ne matchera jamais**, sans erreur.

Deux issues acceptables, une seule à choisir :

- **la nôtre** : `file_key` = minuscule + extension retirée, strictement. Rien d'autre ;
- ou vous spécifiez la normalisation exacte, et nous l'appliquons **à l'identique**.

La première est plus sûre parce qu'il n'y a rien à tenir aligné entre deux dépôts.

### L'année entre parenthèses : **à CONSERVER**

Elle est dans le nom du fichier tel qu'eXoDOS le distribue. La retirer ferait matcher plus
de fichiers renommés, mais **au prix de rapprochements faux entre rééditions** — et le §11
a déjà tranché ce type d'arbitrage dans l'autre sens pour les CRC ambigus : *« les indexer
rendrait la recherche trompeuse »*. Une identification fausse est pire qu'une absence.

Conséquence assumée : un utilisateur qui renomme ses fichiers perd l'identification. C'est
déjà vrai pour l'arcade, c'est *attendu*, et ce sera documenté au §4 de notre `CLAUDE.md`.

---

## 3. Trois voies, est-ce lisible ? → **oui, mais pas comme trois branches parallèles**

Aujourd'hui le scanner pose **une** question binaire : « cette plateforme s'identifie-t-elle
par nom ? » — sinon, CRC.

Le DOS casse cette binarité, et c'est le vrai changement : **`dos` relève des DEUX voies à
la fois.** Sur l'export actuel :

```
jeux ayant une plateforme dos          832
… identifiables par CRC aujourd'hui    678   (1 952 hashes)
… que vous ajouteriez par nom          ~1 365
```

Le bon modèle n'est donc pas « trois voies », c'est **une liste ordonnée de méthodes par
plateforme**. Pour `dos` : CRC d'abord, nom ensuite. Pour l'arcade : nom seulement. Pour le
reste : CRC seulement.

C'est un changement chez nous, pas chez vous, et il est modeste. Rien à faire de votre côté.

---

## 4. Précédence CRC / nom ? → **le CRC, d'accord avec vous**

Et ce n'est pas théorique, contrairement à ce que la note laisse penser : `dos` portera
**les deux** — 1 952 hashes existants *et* vos futures lignes de nom. La règle sera donc
appliquée pour de vrai, sur cette plateforme précisément.

La raison tient en une phrase : **le CRC identifie un contenu, le nom identifie un
contenant que n'importe qui peut renommer.** Quand les deux répondent et se contredisent,
c'est le nom qui a tort.

---

## 5. ⚠️ Ce que la note ne dit pas : les collisions de `file_key`

Votre `PRIMARY KEY (file_key, batocera_system)` **écarte silencieusement** tout doublon.

Que se passe-t-il quand deux jeux du catalogue produisent le même `file_key` sur la même
plateforme ? Le survivant est celui que SQLite insère en premier — donc **arbitraire, et
susceptible de changer d'un export à l'autre sans raison.**

C'est mot pour mot le problème que vous avez vous-mêmes relevé sur les alias en 1.8.0, et
que vous aviez réglé par une règle explicite (« le nom le plus court, départagé
alphabétiquement »). Il faut la même chose ici, ou un choix assumé de les **exclure** — comme
le §11 exclut les CRC ambigus.

**Notre préférence : les exclure.** Un jeu DOS mal identifié n'est pas rattrapable par
l'utilisateur, alors qu'une absence l'est. Mais dites-nous laquelle des deux, parce qu'aucune
ne se devine à la lecture de l'export.

Une mesure suffirait à savoir si le cas existe : combien de vos ~1 365 rapprochements
partagent un `file_key` ?

---

## 6. Sur le §4 de votre note — le catalogue comme goulot

Nous prenons acte, et c'est utile de l'avoir écrit : **86,6 % d'eXoDOS hors catalogue, 311
titres ZX Spectrum sur 18 327 chez TOSEC.** Chaque source nouvelle plafonnera autour de
10-15 % tant que le seuil d'import ne bouge pas.

Ça ne change rien à cette demande — doubler la couverture DOS reste doubler la couverture
DOS — mais ça déplace la question suivante. **Ce n'est pas la nôtre** : le seuil est une
décision de catalogue, pas de contrat d'export. Nous la signalons pour qu'elle ne se perde
pas entre les deux projets.

---

## 7. Critères d'acceptation, si vous partez là-dessus

1. `exp_meta.schema_version` = `1.9.0`, **majeure inchangée** ;
2. tables existantes **intactes** — en particulier `exp_romset`, dont dépend le filtre du §1 ;
3. `file_key` est **déjà normalisé**, par la règle du §2, et par elle seule ;
4. `game_key` sans orphelin ;
5. les collisions sont **traitées explicitement** (§5), et la règle est écrite ;
6. un jeu sans fichier connu n'a **aucune ligne** — pas de ligne vide.

Notre contrôle `--export` gagnera une section qui compte les lignes, vérifie la forme des
clés et **échoue** sur un orphelin — comme il le fait déjà pour les alias.
