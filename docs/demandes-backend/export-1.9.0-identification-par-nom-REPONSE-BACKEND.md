# Backend → frontend — **1.9.0 livré**, et un bug bien plus gros trouvé en route

> Émise par **igiris** (backend) le 2026-08-18, après votre réponse du même jour.
> **Vos quatre réponses sont suivies à la lettre. Le §5 est tranché : exclusion.**
>
> Mais l'essentiel de cette note n'est pas la 1.9.0 — c'est ce qu'on a découvert en
> l'implémentant, et qui vous rapporte beaucoup plus.

```
https://igiris.xyz/exports/games.db   26,6 Mo   schéma 1.9.0
sha256 061a7032ace07f815887379441168458…
```

---

## 1. ⚠️ D'abord : 164 719 lignes de ROM n'étaient jamais rapprochées

En cherchant pourquoi eXoDOS ne produisait aucune ligne, on est tombés sur un défaut
**très antérieur** et invisible.

Une plateforme peut déclarer ses dats de deux façons : une source **principale**, et des
sources **complémentaires** (`dat_extra` — les variantes dématérialisées, notamment). Les
quatre requêtes de rapprochement ne joignaient que sur la principale. **Tout set
complémentaire était donc importé puis ignoré, sans la moindre erreur :**

```
PS Vita PSN        17 100 lignes        Xbox 360 Digital    5 567
FBNeo Arcade        7 718               PS3 PSN             5 585
PSP PSN             2 685               Wii Digital         2 722
3DS Digital           947               DS Download Play      597
+ TOSEC officiel, eXoDOS…
                              TOTAL   164 719 lignes · 0 rapprochement
```

C'est corrigé. L'effet dépasse largement cette demande :

| | avant | après |
|---|---|---|
| rapprochements | 46 999 | **66 783** (+42 %) |
| jeux couverts | 6 800 | **7 559** (+759) |
| `exp_rom_hash` | 71 522 | **83 482** (+11 960) |
| `exp_romset` | 2 702 | **3 482** (+780) |
| liens ROM↔langue | 93 671 | **100 437** |
| jeux badgés | 5 996 | **6 642** |

**780 romsets d'arcade en plus** — c'est FBNeo qui entre enfin. Et la couverture du filtre
« existe en… » passe de 74,5 % à **76,5 %**.

Votre §6 disait que le catalogue était le goulot. Il l'est toujours, mais il n'était pas
seul : une partie des sources était importée pour rien.

---

## 2. Vos quatre réponses, appliquées

**§1 — table dédiée.** Votre mesure a tranché, pas un avis : 833 → 1 536 jeux sous le filtre
« arcade ». Vérifié après génération : `exp_romset` ne contient **aucune** ligne `dos`.

```sql
CREATE TABLE exp_game_file (
    file_key        TEXT NOT NULL,
    batocera_system TEXT NOT NULL,
    game_key        TEXT NOT NULL,
    collection      TEXT,
    PRIMARY KEY (file_key, batocera_system)
) WITHOUT ROWID;
```

**§2 — clé.** Votre règle, exactement : nom sans la **dernière** extension, en minuscules,
**rien d'autre**. **J'abandonne la normalisation des espaces** que j'avais proposée — vous
avez raison, appliquée d'un seul côté elle rend des fichiers introuvables sans erreur.
`file_key` est donc littéralement ce que produit `completeBaseName().toLower()`. L'année
entre parenthèses est conservée.

**§4 — précédence.** Le CRC prime, d'accord. Et c'est concret : DOS porte bien les deux.

---

## 3. §5 — les collisions : **ÉCARTÉES**, décision prise

Votre préférence, et je la partage pour votre propre raison : sur un nom de fichier, une
identification fausse n'est **pas rattrapable par l'utilisateur**, une absence l'est. C'est
le même arbitrage que le §11 sur les CRC ambigus, et il doit pencher du même côté.

La règle est donc : **une clé qui désignerait plusieurs jeux sur la même plateforme n'est
pas exportée du tout.** Pas d'arbitrage silencieux, pas de « plus court gagne ».

Vous demandiez la mesure : **4 collisions réelles**, écartées. Le compte figure dans le
journal de génération à chaque build.

---

## 4. Ce que ça donne pour le DOS

```
identifiables par CRC        696 jeux
identifiables par nom        722 jeux   (766 clés)
union des deux               793 jeux
```

Moins que les ~1 365 annoncés dans ma note : **je m'étais trompé, et je le corrige.**
eXoDOS liste chaque zip sous **deux chemins** (`eXo/eXoDOS` et `Content/GameData/eXoDOS`) ;
je comptais des lignes et non des noms uniques. La collection contient **7 639 jeux**, pas
13 985. Le gain reste réel — DOS passe de 696 à 793 jeux identifiables — mais il est plus
modeste que promis.

**eXoWin3x est écarté** : `windows` n'existe pas comme système Batocera dans notre
configuration, ses lignes seraient inutilisables sur l'appareil. À rouvrir si vous ajoutez
ce système.

---

## 5. Vos critères d'acceptation

| # | Critère | Résultat |
|---|---|---|
| 1 | `schema_version` = 1.9.0, majeure inchangée | ✅ |
| 2 | tables existantes intactes, `exp_romset` en particulier | ✅ 0 ligne `dos` |
| 3 | `file_key` normalisé par la règle du §2, et elle seule | ✅ 0 clé mal formée |
| 4 | aucun `game_key` orphelin | ✅ 0 |
| 5 | collisions traitées explicitement, règle écrite | ✅ exclusion, 4 cas |
| 6 | pas de ligne vide pour un jeu sans fichier | ✅ 722 jeux sur 17 346 |

`probe.py` conclut « Contrat respecté ». Le générateur refuse désormais de publier si un
`file_key` est mal normalisé ou pointe un jeu inexistant.

---

## 6. Une chose qui va vous surprendre au prochain scan

Avec +11 960 hashes et +780 romsets, **des jeux vont passer au vert chez des utilisateurs
sans qu'ils aient rien téléchargé**. Ce n'est pas un bug de votre scanner : ce sont les
sources complémentaires qui étaient importées sans être exploitées.
