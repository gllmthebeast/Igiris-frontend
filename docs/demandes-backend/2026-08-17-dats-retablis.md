# Note backend → frontend — dats rétablis, matériel arcade récupéré (17/08, 18:05)

> Suite de `2026-08-17-test-bout-en-bout-maj.md`. **Les deux problèmes signalés ce matin
> sont corrigés.** Un nouvel export est en ligne.

```
https://igiris.xyz/exports/games.db   26,1 Mo   schéma 1.8.0
généré 2026-08-17T18:04:53Z · sha256 9c453c5f5ed2338aa00d005c715bfcbb…
```

## Le matériel arcade est revenu — et au-dessus du niveau d'avant

```
romsets avec hardware / driver_status
  avant incident   2 342
  dégradé          2 282   ← l'export que vous aviez ce matin
  maintenant       2 347   ✅
```

Les **60 romsets** qui avaient perdu `hardware` et `driver_status` les ont retrouvés, et 5
de plus grâce aux romsets gagnés en chemin. Si vous aviez vu des fiches arcade sans matériel,
c'est réglé — un `fetch-export.sh` suffit.

Statuts de pilote : 1 734 `good` · 377 `imperfect` · 236 `preliminary`.

## Les dats se réimportent

**382 805 entrées importées, 2 sets en échec, 47 inchangés** — contre 0 importé et 95 en
échec ce matin.

⚠️ **Mais rien de neuf n'est entré pour autant** : `src_dat_rom` reste à 448 071 lignes et le
dernier dat date toujours du **31/07**. Les dats mirrorés par libretro ont exactement le même
contenu qu'au 31/07 — il n'y avait simplement rien de nouveau à prendre. Le correctif rétablit
la **capacité** d'importer, il n'invente pas de données.

Autrement dit : la couverture ROM n'a pas progressé, mais elle n'est plus **bloquée**.

## Ce qui a été corrigé, pour votre information

Le problème n'était pas un backoff manquant mais la **méthode** : prendre ~90 fichiers un par
un sur `raw.githubusercontent.com` est traité comme du scraping, et GitHub étrangle l'IP
entière — sans en-tête `Retry-After`, donc sans moyen de savoir combien attendre.

Le générateur télécharge désormais l'archive du dépôt en **une seule requête** (177 Mo,
26 s) et lit les 211 dats sur disque. Zéro requête par dat.

Les deux sources MAME secondaires vivent dans ce même dépôt : elles sont lues dans la même
archive. C'est ce qui a permis de récupérer les 2 560 machines perdues alors que GitHub
refusait toujours les requêtes directes.

Et l'import MAME ne remplace plus la table quand une source manque — c'est cette combinaison
(source absente + remplacement en bloc) qui avait effacé le matériel de vos 60 romsets.

## Deux limites qui subsistent, pour que vous ne les cherchiez pas

- **`Acorn - BBC Micro`** n'est mirroré nulle part chez libretro et DAT-o-MATIC ne répond
  pas : le set reste absent.
- **`Microsoft - XBOX 360 (Games on Demand)`** ne se parse pas (« 0 entrée analysée »), et
  c'était déjà le cas avant. Format non reconnu, à regarder à l'occasion.

Aucune des deux ne concerne un système émulable côté appareil.

## Rappel

Toujours en attente de votre côté : **`is_preferred` sur plateforme non émulable** (38 lignes
sur 69) — voir `reponses/export-1.8.0-alias-REPONSE.md` §5. Et `hasRealBanner()` retourne
encore `false` en dur alors qu'`artwork_ref` est publié sur 97,4 % du catalogue.
