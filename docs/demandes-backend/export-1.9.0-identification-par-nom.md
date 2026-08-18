# Note backend → frontend — export **1.9.0** : identifier par NOM DE FICHIER

> Émise par **igiris** (backend) le 2026-08-18. **Rien n'est implémenté** : cette note
> demande votre avis avant que le contrat bouge, parce qu'elle ajoute un **axe
> d'identification** et que c'est votre appareil qui l'exécutera.
>
> Ajout strictement additif → **mineure**. Aucune table existante touchée.

---

## 1. Le problème : des collections entières sans aucune empreinte

Les collections **eXoDOS** et **eXoWin3x** (projet eXo, `retro-exo.com`) couvrent le trou
que vous connaissez : le DOS et le Windows 3.x, là où No-Intro et Redump n'ont presque rien.

```
eXoDOS     13 985 jeux      649 Go
eXoWin3x    1 138 jeux      345 Go   (compte confirmé par le site officiel)
```

**Elles ne sont distribuées que par torrent, et un torrent ne contient aucun hash de
fichier.** BitTorrent v1 ne hache que des *pièces* de 8 Mo, à cheval sur plusieurs
fichiers : chaque entrée ne porte que son chemin et sa taille. Il n'y a donc **aucun CRC à
mettre dans `exp_rom_hash`**, et il n'y en aura pas — ce n'est pas une lacune de la source,
c'est une propriété du format.

Ce qu'on a en revanche, exactement et depuis la source officielle : **le nom de fichier**.

```
Gabriel Knight 2 - The Beast Within (1995).zip
688 Attack Sub (1989).zip
A-10 Tank Killer (1989).zip
```

---

## 2. Ce qu'on propose : le motif que vous appliquez déjà à l'arcade

Votre §4 tranche déjà ce débat pour l'arcade, et pour une raison qui vaut ici mot pour mot :

> « Le CRC d'un `.zip` d'arcade change dès qu'on reconstruit le romset […] Le **nom** est
> stable, et c'est sous ce nom que Batocera range les jeux d'arcade. »

Un jeu eXoDOS est distribué **comme un zip nommé**, exactement pareil. Le nom est
l'identifiant stable ; le hash du conteneur ne l'est pas.

D'où l'ajout envisagé :

```sql
CREATE TABLE exp_game_file (
    file_key        TEXT NOT NULL,   -- nom normalisé, SANS extension, minuscules
    batocera_system TEXT NOT NULL,
    game_key        TEXT NOT NULL,
    collection      TEXT,            -- 'exodos' | 'exowin3x' — d'où vient l'entrée
    PRIMARY KEY (file_key, batocera_system)
) WITHOUT ROWID;
```

Même forme qu'`exp_romset`, même sémantique, même coût : un *lookup*, pas un rapprochement.
La normalisation reste **entièrement chez nous** (§0) — vous comparez des chaînes qu'on a
préparées.

---

## 3. Ce que ça rapporte, mesuré

Rapprochement des 13 985 titres eXoDOS avec le catalogue :

```
rapprochés au catalogue          1 871   (13,4 %)
… dont le jeu est listé sur DOS  1 365
identifiables par CRC aujourd'hui  678   ← l'export actuel
```

**La couverture DOS doublerait**, de 678 à ~1 365 jeux reconnaissables sur l'appareil.
eXoWin3x est plus modeste : 85 rapprochés sur 1 138.

Et cela **sans télécharger un octet** des 649 Go : tout vient du fichier `.torrent`, qui
pèse 2,7 Mo et qu'on décode.

---

## 4. La limite, dite franchement

**86,6 % des titres eXoDOS ne sont pas au catalogue du tout.**

Ce n'est pas un défaut de la source, c'est notre seuil d'import IGDB (≥ 3 notes) : les
milliers de titres DOS obscurs, sharewares et éducatifs n'y figurent pas. Le même mur nous
est apparu hier avec TOSEC — 18 327 titres ZX Spectrum chez eux, 311 chez nous.

Le goulot de ce projet n'est plus la couverture des dats. **C'est le catalogue.** On le
signale parce que ça conditionne ce que vous pouvez espérer des prochaines livraisons :
tant que le seuil ne bouge pas, chaque nouvelle source plafonnera autour de 10-15 %.

---

## 5. Ce sur quoi on a besoin de votre avis

1. **Table dédiée ou extension d'`exp_romset` ?** `exp_romset` est marquée « arcade
   uniquement » dans votre §3. Étendre son sens ou créer `exp_game_file` : votre chargeur,
   votre choix.
2. **Quelle clé de comparaison ?** Notre proposition : nom **sans extension**, en
   minuscules, espaces normalisés — pour que `Gabriel Knight 2 - The Beast Within
   (1995).zip` et le même fichier renommé `.ZIP` tombent au même endroit. Faut-il conserver
   l'année entre parenthèses dans la clé (elle désambiguïse les rééditions) ou l'écarter ?
3. **Trois voies d'identification cohabiteraient** : CRC (console), nom de romset (arcade),
   nom de fichier (DOS/Win3x). Est-ce que ça reste lisible dans votre scanner, ou faut-il
   unifier les deux dernières ?
4. **Précédence** : si un jeu est trouvable par CRC *et* par nom, lequel gagne ? Notre avis :
   le CRC, il est plus fort.

---

## 6. Ce qui n'est pas dans cette note

Ni ROM, ni hash, ni contenu : uniquement des noms de fichiers issus des torrents officiels.
Les cinq `.torrent` et les 15 122 titres extraits sont en cache côté backend
(`/opt/igiris/.cache/dats/exo/`), rien n'est publié.

**eXoScummVM est écarté pour l'instant** : ses 836 zips suivent une autre convention de
nommage (`3 Skulls of the Toltecs (CD DOS).zip`, sans année), qui demande un traitement à
part.
