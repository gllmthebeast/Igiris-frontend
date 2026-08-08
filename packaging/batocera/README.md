# Intégration Batocera — le fork mince de la phase 2

> `CLAUDE.md` §16 : « Chaque fork reste le plus mince possible : un paquet pointant vers ce
> dépôt, la modification du service de démarrage pour lancer le frontend au lieu d'ES, rien
> d'autre. Plus le diff avec l'upstream est petit, moins le rebase coûte cher. »

Établi sur l'image **Batocera 43.1 (bcm2712)**, en la montant en lecture seule — pas
d'après la documentation.

## La chaîne de démarrage réelle

```
/etc/init.d/S31emulationstation   →  lance labwc-launch (si system.es.atstartup ≠ 0)
        labwc (compositeur Wayland)
                /usr/share/labwc/autostart
                        dernière ligne :
                        /usr/bin/emulationstation-standalone > /dev/null 2>&1
```

## Le diff : une ligne

Tout tient dans la dernière ligne d'`autostart`. Le compositeur Wayland reste en place —
Qt en a besoin pour afficher quoi que ce soit.

```diff
--- a/usr/share/labwc/autostart
+++ b/usr/share/labwc/autostart
@@
 /usr/bin/batocera-mouse hide > /dev/null 2>&1
-/usr/bin/emulationstation-standalone > /dev/null 2>&1
+/usr/bin/igiris-frontend > /userdata/system/logs/igiris.log 2>&1
```

C'est le critère de conception du §16 : **un fork dont le diff tient en une ligne se rebase
sans douleur** à chaque version de la distribution hôte.

> ⚠️ Ne PAS toucher à `S31emulationstation` ni au réglage `system.es.atstartup`. Mettre ce
> réglage à `0` empêcherait `labwc` de démarrer, et le frontend n'aurait plus de
> compositeur — donc plus d'affichage.

## Essayer sans reconstruire d'image

`install-on-device.sh` fait la même chose à chaud, sur un appareil déjà installé. Utile
pour tester avant de fabriquer un paquet.

```sh
scp build/igiris-frontend root@batocera:/userdata/system/
scp packaging/batocera/install-on-device.sh root@batocera:/userdata/system/
ssh root@batocera 'sh /userdata/system/install-on-device.sh'
```

Le script écrit dans `/userdata`, la seule partition inscriptible : le reste du système est
en lecture seule et serait de toute façon écrasé à la mise à jour.

## Ce que le paquet devra contenir

1. le binaire `igiris-frontend` ;
2. l'export `games.db`, déposé dans `/userdata/system/igiris/` et **jamais versionné** (§15) ;
3. la ligne d'`autostart` ci-dessus.

Rien d'autre. Toute autre modification de l'upstream alourdit le rebase sans contrepartie.

## Dépendances à l'exécution

Qt6 Core / Gui / Qml / Quick, le plugin de plateforme **wayland**, et `libsqlite3`. Sur une
image construite par Buildroot, ces paquets doivent être activés dans le `defconfig` de la
cible — c'est le vrai travail d'intégration, et il est propre à chaque distribution.
