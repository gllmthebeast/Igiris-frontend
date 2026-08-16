# Note du backend `igiris` — export **1.7.0** : le catalogue entier

> Émise par **igiris** (backend, `/opt/igiris`) le 2026-08-16. **En production.**
>
> Changement **à l'initiative du backend**, sur décision produit — ce n'est pas la réponse
> à une demande de votre part. D'où cette note plutôt qu'un `-REPONSE.md`.
>
> **Version MINEURE : aucune colonne supprimée, aucune renommée. Votre binaire actuel
> charge cet export sans modification.** Mais c'est le changement qui touche le plus votre
> interface depuis le début — lisez le §3.

```
https://igiris.xyz/exports/games.db   23,8 Mo   schéma 1.7.0
généré 2026-08-16T18:13:29Z · sha256 f7af0ac9e163dfe26339f4594bf93f9c…
```

---

## 1. Ce qui change, en un tableau

| | 1.6.0 | **1.7.0** |
|---|---|---|
| jeux | 7 581 | **17 260** |
| lignes plateforme | 18 555 | **59 066** |
| … dont **non émulables** | 0 | **40 511 (69 %)** |
| hashes CRC | 71 006 | 71 006 |
| romsets arcade | 2 697 | 2 697 |
| liens ROM↔langue | 92 850 | 92 850 |
| taille | 14,9 Mo | **23,8 Mo** |

**Les ROMs ne bougent pas d'un octet.** Les dats ne couvrent que des systèmes émulables :
tout ce qui s'ajoute est du catalogue, jamais de l'identification.

### Pourquoi

Le frontend a vocation à devenir un **launcher**, pas seulement un sélecteur de jeux
émulés. Il lui faut donc tout le catalogue — y compris ce qui tourne nativement. Les
plateformes ajoutées, par volume : **PC (11 310 jeux)**, PS4, Mac, Switch, Xbox One, iOS,
Linux, Android, PS5, Series X|S, X360.

---

## 2. `emu_score` vaut désormais `NULL` sur les plateformes non émulées

Il valait 0, 12, 22, 28, 38 ou 42 sur ces lignes. C'est **un taux de fidélité
d'émulation** : sur une PS4 ou une Switch, la bonne réponse n'est ni « 0 % » ni « 42 % »,
c'est **« sans objet »**.

```
lignes émulables      18 555 / 18 555  ont un emu_score
lignes non émulables       0 / 40 511  en ont un
```

Votre §5 explique déjà comment lire ce champ. La règle s'étend d'elle-même :
`emu_score IS NULL` ⇒ ne rien afficher, ne pas trier dessus, ne pas proposer au lancement.

---

## 3. Ce que ça change chez vous — la section à lire

Bonne nouvelle d'abord : **votre code est déjà prêt**, on l'a vérifié dans vos sources.

- `ExportDatabase.cpp:386` gère `platformKey` NULL, avec le commentaire qui va bien ;
- `platformKeys()` et `platformKeysByGame()` filtrent tous les deux `WHERE … IS NOT NULL`.

Donc **votre menu de filtres et votre index de plateformes ignorent déjà les lignes non
émulables**. Rien ne casse.

Ce qui change en revanche, c'est le **comportement**, et sur trois points :

**Le statut noir devient le cas dominant.** Votre §11 documentait
`exp_game_platform.batocera_system` peut être NULL — « plateforme d'origine non émulée…
ne pas les traiter comme des cibles ». Jusqu'ici ce cas **n'arrivait jamais** : il y en
avait zéro dans l'export. Il concerne maintenant **69 % des lignes**. Le code existait,
il n'avait jamais été exercé.

**9 679 jeux n'ont aucune plateforme émulable.** Ils s'affichent, ils se cherchent, ils ont
jaquette, bandeau, synopsis et note — mais **aucun n'est lançable aujourd'hui**. C'est
exactement l'objet du chantier launcher. En attendant, votre fiche doit savoir dire
« présent au catalogue, pas lançable ici » sans que ça ressemble à une erreur.

**100 jeux n'ont aucune ligne plateforme du tout.** Cas limite : ni émulable, ni non
émulable, rien. Ils sont conservés délibérément — ce sont des entrées de catalogue
valides — mais votre fiche doit tolérer une liste de plateformes **vide**.

Et un point de mesure, pas de correction : votre liste passe de 7 581 à 17 260 lignes, et
votre recherche `LIKE '%…%'` balaie 2,3 fois plus. Vous aviez chiffré le coût des badges à
1,1 µs par ligne visible ; **vérifiez la recherche**, c'est elle qui balaie tout.

---

## 4. Les langues : **deux couches, et il ne faut pas les mélanger**

C'est le point le plus délicat de cette version.

En élargissant le catalogue, le taux de badge langue s'effondrerait de 79 % à **35 %** :
les 9 679 jeux ajoutés n'ont aucun dat, donc aucune langue. On a donc ajouté une seconde
source — **IGDB** — et il fallait le faire **sans casser votre illumination**.

### La nouvelle colonne

```sql
exp_game.lang_catalog_mask  INTEGER   -- masque de bits, même registre qu'exp_language
```

Alimentée par IGDB, **types Audio et Sous-titres uniquement**. *Interface* est écarté
délibérément : un jeu dont seuls les menus sont traduits n'est pas « jouable en français ».

### La règle, et elle n'est pas négociable

> **IGDB donne les langues DU JEU. Pas d'une release, pas d'une ROM, pas d'un CRC.**

Une langue présente dans `lang_catalog_mask` mais absente d'`exp_game_language` **ne pourra
jamais s'illuminer**, même en possédant toutes les ROMs du jeu — il n'existe aucun CRC pour
la rattacher. C'est une propriété de la source, pas une lacune à combler.

D'où la séparation stricte, qui tombe **exactement** sur les deux filtres de votre §8 :

| Votre filtre | Source | Statut |
|---|---|---|
| « **Existe** en français » | `lang_mask \| lang_catalog_mask` | statique, élargi |
| « **Jouable** en français » | `exp_game_language` ∩ CRC possédés | **inchangé** |

**`exp_game_language` et `lang_mask` n'ont pas bougé d'une ligne** : 92 850 liens, 5 986
jeux badgés, **0 masque incohérent**, 0 CRC orphelin. Votre correspondance version possédée
↔ langue est intacte, c'était la condition posée.

### ⚠️ Ce qu'il ne faut PAS faire

**N'ajoutez pas `lang_catalog_mask` au masque qui pilote les badges illuminés.** Vous
produiriez des badges définitivement gris, que l'utilisateur ne pourrait jamais allumer
quoi qu'il télécharge. Votre §8 dit « il n'y a pas de troisième état » — c'est toujours
vrai, à condition que la vue liste continue de ne connaître que `lang_mask`.

Si vous voulez montrer les langues de catalogue, la **fiche** est le bon endroit (elle lit
déjà `exp_game_language` directement), avec un traitement visuel distinct de l'axe
illuminé/grisé.

### Le gain

```
lang_mask (dats, illuminable)     5 986 jeux
lang_catalog_mask (IGDB)          8 121 jeux
au moins une des deux            12 865 / 17 260   (74,5 %)
IGDB là où les dats sont muets    6 879 jeux
```

---

## 5. Quatre langues de plus au registre

`exp_language` passe de 25 à **29 lignes**, 26 portant un `bit_index`.

Ajoutées **en fin de registre**, donc sans déplacer aucun bit existant : **`he` (hébreu),
`th` (thaï), `vi` (vietnamien), `uk` (ukrainien)**. Elles n'existent dans aucune balise de
dat — elles ne viennent que du catalogue IGDB.

**Une normalisation à connaître** : IGDB code le norvégien `nb-NO` (bokmål), les dats `no`.
Les deux sont repliés sur **`no`**. Sans ça, la même langue aurait occupé deux bits et un
filtre « norvégien » aurait manqué la moitié du catalogue. De même `zh-CN`/`zh-TW` → `zh`
et `pt-PT`/`pt-BR` → `pt`.

Il reste **36 bits libres** sur 63.

---

## 6. Corrigé au passage

`exp_meta.languages` comptait les langues **des dats seuls** (25) alors qu'`exp_language`
porte l'union des deux sources (29). Un manifeste qui annonce moins que ce que la table
contient est un contrat qui ment : corrigé, les deux disent 29.

---

## 7. Ce qui reste vrai, et ce qui reste à faire chez vous

Inchangé et vérifié sur l'artefact publié : `journal_mode=delete`, majeure **1**, aucune
chaîne vide là où le contrat promet `NULL`, `is_preferred` unique par jeu,
`artwork_ref` 97,4 %, `summary` 99,8 %, `mode_mask` 97,6 %, `release_year` 100 %
(**22 266 lignes** diffèrent maintenant de l'année du jeu).

Vos trois outils valident : `fetch-export.sh`, `probe.py` (« Contrat respecté »), et votre
binaire — masques 20/20, CRC 23/23, romsets 53/53.

**Rappel du §9 de la réponse 1.5.0, toujours d'actualité** : `hasRealBanner()` retourne
encore `false` en dur et `bannerRef()` retourne `m_coverRef`
(`src/ui/GameDetailModel.h:122-123`). `artwork_ref` est publié depuis le 14/08 sur 97,4 %
du catalogue et n'est toujours pas affiché.
