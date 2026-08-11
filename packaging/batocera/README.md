# Intégration Batocera — le fork mince de la phase 2

> `CLAUDE.md` §16 : « Chaque fork reste le plus mince possible : un paquet pointant vers ce
> dépôt, la modification du service de démarrage pour lancer le frontend au lieu d'ES, rien
> d'autre. Plus le diff avec l'upstream est petit, moins le rebase coûte cher. »

Établi sur l'image **Batocera 43.1 (bcm2712)**, en la montant en lecture seule — pas
d'après la documentation.

## ⚠️ La chaîne de démarrage dépend de l'ARCHITECTURE, pas de la version

Constaté en montant les **deux** images 43.1 — même version, même date de build,
compositeurs différents. C'est le piège principal de cette intégration.

```
ARM (bcm2712) — Wayland
/etc/init.d/S31emulationstation   →  labwc-launch (si system.es.atstartup ≠ 0)
        labwc (compositeur Wayland)
                /usr/share/labwc/autostart
                        dernière ligne :
                        /usr/bin/emulationstation-standalone > /dev/null 2>&1

x86_64 — X11
/etc/init.d/S31emulationstation   →  startx (si system.es.atstartup ≠ 0)
        /etc/X11/xinit/xinitrc
                ligne 87 :
                openbox --config-file /etc/openbox/rc.xml --startup "emulationstation-standalone"
```

Les plugins Qt le confirment : l'image ARM embarque `libqwayland.so`, celle x86_64 **non**
— elle a `libqxcb.so`. Le frontend utilise donc Wayland sur Pi et X11 sur PC, sans rien
avoir à configurer : Qt choisit son plugin selon ce qu'il trouve.

## Le diff : une ligne, dans les deux cas

```diff
ARM
--- a/usr/share/labwc/autostart
+++ b/usr/share/labwc/autostart
@@
 /usr/bin/batocera-mouse hide > /dev/null 2>&1
-/usr/bin/emulationstation-standalone > /dev/null 2>&1
+/userdata/system/igiris/igiris-frontend > /userdata/system/logs/igiris.log 2>&1
```

```diff
x86_64
--- a/etc/X11/xinit/xinitrc
+++ b/etc/X11/xinit/xinitrc
@@
-openbox --config-file /etc/openbox/rc.xml --startup "emulationstation-standalone"
+openbox --config-file /etc/openbox/rc.xml --startup "/userdata/system/igiris/igiris-frontend > /userdata/system/logs/igiris.log 2>&1"
```

Sous X11, **seul l'argument de `--startup` change** : `openbox` doit rester, sinon il n'y a
plus de gestionnaire de fenêtres et Qt n'a plus de surface où s'afficher. C'est l'exact
pendant du compositeur `labwc` côté ARM.

C'est le critère de conception du §16 : **un fork dont le diff tient en une ligne se rebase
sans douleur** à chaque version de la distribution hôte.

> ⚠️ Ne PAS toucher à `S31emulationstation` ni au réglage `system.es.atstartup`. Mettre ce
> réglage à `0` empêcherait `labwc` (ou `startx`) de démarrer, et le frontend n'aurait plus
> ni compositeur ni serveur X — donc plus d'affichage.

## ⚠️ Persister la modification, sinon elle est perdue

La racine est un squashfs recouvert d'un overlay **en RAM**. `mount -o remount,rw /` rend
l'écriture possible, mais elle **ne survit pas à l'extinction**. L'en-tête de
`batocera-save-overlay` le dit lui-même :

> *if you modify the root using `mount -o remount,rw /`, then you need to save it using this
> script*

Sans cet appel, l'appareil redémarre sur EmulationStation — sans erreur, sans trace, et la
cause est introuvable. `install-on-device.sh` s'en charge.

## Ce que l'image fournit déjà — rien à embarquer

Vérifié sur les deux images 43.1, en les montant :

| | ARM (bcm2712) | x86_64 |
|---|---|---|
| Qt6 | **6.10.1** complet | **6.10.1** complet |
| Plugin de plateforme | `wayland`, `eglfs`, `xcb`… | `xcb`, `eglfs`… (**pas** de wayland) |
| glibc | 2.40 | 2.40 |
| libstdc++ | 6.0.32 (GCC 13) | 6.0.32 (GCC 13) |
| SQLite | 3.47 | 3.47 |

Le binaire compilé sous Ubuntu 24.04 (Qt 6.4, glibc 2.34 requise, GCC 13) s'exécute
directement : Qt garantit la compatibilité binaire dans une majeure, et la glibc est
rétrocompatible. **Vérifié** en exécutant le binaire dans la racine ARM montée — il y
annonce `Qt 6.10.1`, lit les 224 systèmes et rend l'interface.

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
