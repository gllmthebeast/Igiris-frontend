# Note backend → frontend — test de mise à jour de bout en bout (17/08)

> Déposée par **igiris** (backend) le 2026-08-17 à 15:45.
> Deux choses vous concernent : **60 romsets ont perdu leur matériel et leur statut de
> pilote**, et **les dats ne se mettent plus à jour**. L'export publié reste valide.

## L'export en ligne a changé

```
https://igiris.xyz/exports/games.db   26,1 Mo   schéma 1.8.0
généré 2026-08-17T15:39:19Z · sha256 b51e90724aed4d6fef018549a261cf21…
```

Majeure inchangée, aucune colonne touchée. Un `fetch-export.sh` suffit.

| | avant | après |
|---|---|---|
| jeux | 17 260 | **17 346** |
| lignes plateforme | 59 066 | 59 311 |
| hashes CRC | 71 006 | 71 499 |
| liens ROM↔langue | 92 850 | 93 671 |
| alias | 18 839 | 18 871 |
| romsets arcade | 2 697 | 2 702 |

**+87 jeux**, tous récents (2023-2026, PC/PS5/Switch 2 pour l'essentiel) et **123 jeux datés
2026 ou plus** sont maintenant au catalogue. Notes IGDB rafraîchies sur 17 324 titres.

Intégrité vérifiée sur l'artefact publié : **0 masque incohérent**, 0 CRC orphelin, 0 alias
orphelin, 0 jeu à plusieurs élues, 0 chaîne vide, `journal_mode=delete`. Vos trois outils
valident.

## ⚠️ Ce qui a régressé et qui se voit chez vous

```
romsets avec matériel réel      2 342 → 2 282   (−60)
romsets avec statut de pilote   2 342 → 2 282   (−60)
```

Votre fiche arcade affiche `hardware` (neogeo, cps2…) et `driver_status`. Sur **60 romsets**
ces deux champs sont passés à `NULL`.

**Cause** : l'import MAME agrège trois sources. La principale (listxml 0.288, en cache local)
a fonctionné — 42 880 machines. Les deux secondaires, *MAME 2016 XML (Arcade Only)* et
*MAME 2003-Plus XML*, ont échoué en **HTTP 429** et **503**. L'import remplace au lieu de
compléter : leurs machines ont disparu, et avec elles le matériel de ces 60 romsets.

**Ce n'est pas une perte définitive** : relancer `import-mame.sh` quand l'étranglement est
retombé les ramène. Aucune action de votre côté ; si vous voyez un romset sans matériel,
c'est ça, pas une donnée manquante en amont.

## ⚠️ Les dats ne se mettent plus à jour du tout

**95 sets tentés, 0 importé.** Tous les téléchargements échouent en **HTTP 429** :
`raw.githubusercontent.com` étrangle notre IP, et l'hébergeur no-intro a en plus changé sa
page (« bouton de préparation introuvable »).

Le dernier dat importé date du **31/07**. Conséquence pour vous : la couverture ROM du
catalogue ne progresse plus — ni nouveaux CRC, ni nouvelles langues issues des balises de
dat. Les 493 hashes et 821 liens de langue gagnés ci-dessus viennent du **rapprochement** des
87 nouveaux titres avec les dats déjà en base, pas de dats neufs.

La base n'a pas été abîmée : un téléchargement en échec n'écrit rien (96 sets, 448 071 lignes
de ROM, `integrity_check` ok, inchangés).

C'est un chantier backend, pas le vôtre. Signalé pour que vous ne cherchiez pas ailleurs si
la couverture ROM stagne aux prochaines livraisons.

## À noter au passage

Le rapprochement exact utilise vos alias : **6 700 des 46 982 correspondances** ROM↔titre
passent par un nom alternatif plutôt que par le titre canonique. La table `exp_game_alias`
que vous avez demandée expose côté appareil une donnée qui servait déjà, en interne, au
rapprochement côté serveur.
