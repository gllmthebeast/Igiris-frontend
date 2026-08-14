# igiris-frontend

Un frontend de rétrogaming qui présente **une liste unique de jeux, tous systèmes
confondus** — pas d'écran de sélection de plateforme. Il vous dit quels jeux vous possédez,
sur quelle machine, et dans quelles langues.

Tous les jeux du catalogue s'affichent, ROM présente ou non : **l'absence est une
information, pas un filtre.**

Il remplace EmulationStation au démarrage, sans jamais modifier le moteur d'émulation de la
distribution hôte.

---

## Installer sur un PC Ubuntu

Pour essayer, ou pour développer. **Ubuntu 24.04 requis** — le binaire est compilé contre
Qt 6.4, et Ubuntu 22.04 n'en fournit que 6.2.

Téléchargez le paquet correspondant à votre machine depuis la
[dernière release](https://github.com/gllmthebeast/Igiris-frontend/releases/latest)
(`amd64` pour un PC, `arm64` pour un Raspberry Pi sous Ubuntu), puis :

```bash
sudo apt install ./igiris-frontend_1.2.0_amd64.deb
```

`apt` installe lui-même Qt6, les modules QML et le plugin de plateforme.

### Récupérer le catalogue

Le catalogue **n'est pas dans le paquet** : c'est un fichier de 9,6 Mo régénéré chaque mois
côté serveur, qui se télécharge à part.

```bash
mkdir -p ~/.local/share/igiris
/usr/share/igiris-frontend/tools/fetch-export.sh ~/.local/share/igiris
```

Ce chemin n'est pas arbitraire — c'est un des emplacements que l'application cherche
d'elle-même. Aucune option à passer ensuite.

Le script vérifie l'empreinte sha256 avant de remplacer quoi que ce soit : un
téléchargement tronqué ne devient jamais le catalogue courant.

### Lancer

```bash
igiris-frontend
```

Navigation au clavier ou à la manette : **flèches** pour se déplacer, **←/→** sur un filtre
pour changer sa valeur, **Entrée** ouvre la fiche d'un jeu, **Échap** revient.

### Vérifier que tout est en place

```bash
python3 /usr/share/igiris-frontend/tools/probe.py ~/.local/share/igiris/games.db
igiris-frontend --languages ~/.local/share/igiris/games.db
```

Le premier doit conclure **« ✓ Contrat respecté »**.

### Ce qui ne marchera pas sur un PC de bureau, et pourquoi

Il n'y a **aucune distribution de rétrogaming installée**. L'application le dit franchement :

```
⚠ aucune distribution reconnue : statuts et lancement indisponibles
```

C'est normal. Les 7 581 jeux s'affichent, la recherche, les filtres et les badges de langue
fonctionnent — mais aucun jeu ne peut être lancé, et les pastilles vert / rouge / noir
restent indécidables : c'est le fichier de description des systèmes de la distribution qui
les tranche, et il n'existe pas ici.

Pour les exercer quand même, fabriquez une description factice :

```bash
cat > /tmp/es_systems.cfg <<'FIN'
<?xml version="1.0"?>
<systemList>
  <system><name>snes</name><fullname>Super Nintendo</fullname>
    <path>/tmp/roms/snes</path><extension>.sfc .smc .zip</extension>
    <command>/bin/echo -system snes -rom %ROM%</command></system>
  <system><name>nes</name><fullname>NES</fullname>
    <path>/tmp/roms/nes</path><extension>.nes .zip</extension>
    <command>/bin/echo -system nes -rom %ROM%</command></system>
</systemList>
FIN

mkdir -p /tmp/roms/snes /tmp/roms/nes
igiris-frontend --systems-file /tmp/es_systems.cfg --distro batocera --roms /tmp/roms
```

Déposez de vraies ROMs dans `/tmp/roms/snes` : le scan les identifie par CRC32, les
pastilles passent au vert et les badges de langue s'illuminent.

---

## Installer sur Batocera

**Prenez l'archive**, pas le `.deb` : Batocera est un Buildroot en lecture seule, sans
gestionnaire de paquets. `aarch64` pour un Raspberry Pi, `x86_64` pour un PC.

Rien n'est à embarquer. Vérifié en montant les deux images **43.1** : elles fournissent déjà
Qt 6.10.1, SQLite 3.47, glibc 2.40 et libstdc++ de GCC 13. Le binaire compilé sous Ubuntu
s'y exécute directement — testé en l'exécutant dans la racine ARM, où il annonce
`Qt 6.10.1`, lit les 224 systèmes et rend l'interface.

```sh
scp igiris-frontend root@batocera:/userdata/system/
scp install-on-device.sh root@batocera:/userdata/system/
ssh root@batocera 'sh /userdata/system/install-on-device.sh'
```

Le script détecte la chaîne de démarrage, remplace **une seule ligne**, conserve l'original,
puis persiste la modification.

> ⚠️ **La chaîne de démarrage dépend de l'architecture, pas de la version.** À version
> identique, l'image ARM démarre sous Wayland (`labwc`, via
> `/usr/share/labwc/autostart`) et l'image x86_64 sous X11 (`startx` puis `openbox`, via
> `/etc/X11/xinit/xinitrc`). Le script gère les deux ; ne patchez pas un fichier à la main
> sans avoir vérifié lequel s'applique.

> ⚠️ **La persistance n'est pas cosmétique.** La racine est un squashfs recouvert d'un
> overlay **en RAM** : sans `batocera-save-overlay`, la modification disparaît à
> l'extinction et l'appareil redémarre sur EmulationStation, sans le moindre message
> d'erreur.

N'oubliez pas le catalogue :

```sh
bash tools/fetch-export.sh /userdata/system/igiris
```

Pour revenir en arrière : restaurez `/userdata/system/igiris/autostart.original` par-dessus
le fichier patché, puis `batocera-save-overlay`.

Détail de la chaîne de démarrage : [`packaging/batocera/README.md`](packaging/batocera/README.md).

---

## Tester dans une machine virtuelle

**Sans appareil, et sans reconstruire d'image.** L'image x86_64 démarre telle quelle sous
QEMU, ce qui exerce le **vrai chemin de démarrage** — `startx`, `openbox`, plugin Qt `xcb`.
C'est la seule chose qu'un rendu `offscreen` ou un `chroot` ne peuvent pas vérifier.

Reconstruire une image Batocera n'est **pas** nécessaire, et coûte cher : un build Buildroot
de plusieurs heures et d'une centaine de gigaoctets. L'image officielle contient déjà tout.

### Préparer

```bash
sudo apt install qemu-system-x86 qemu-utils

curl -sSLO https://mirrors.o2switch.fr/batocera/x86_64/stable/last/batocera-x86_64-43.1-20260529.img.gz
gunzip batocera-x86_64-43.1-20260529.img.gz
```

Créez un disque de travail plutôt que d'écrire dans l'image : toutes les modifications vont
dans la surcouche, et **supprimer ce seul fichier vous rend un système neuf**.

```bash
qemu-img create -f qcow2 -F raw \
    -b "$PWD/batocera-x86_64-43.1-20260529.img" batocera-test.qcow2 32G
```

### Démarrer

```bash
qemu-system-x86_64 -enable-kvm -m 4096 -smp 4 \
  -drive file=batocera-test.qcow2,format=qcow2,if=virtio \
  -device virtio-vga-gl -display gtk,gl=on \
  -device usb-ehci -device usb-tablet \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0
```

Trois choix méritent une explication :

- `-enable-kvm` — sans lui tout est émulé, donc inutilisable. Il suppose un hôte x86_64.

- **`-device virtio-vga-gl` avec `-display gtk,gl=on`** — c'est la ligne à ne pas rater.
  Le `-gl` et le `gl=on` ne sont pas des raffinements : sans eux, **aucune accélération
  GL/GLX n'est exposée à l'invité**, et toute application OpenGL échoue au démarrage.
  EmulationStation le dit crûment (`Error creating SDL window! GLX is not supported`),
  QEMU affiche « Display output is not active », et le frontend — qui est du Qt Quick —
  échouerait exactement pareil.

  Le piège est que `virtio_gpu_dri.so` est bien présent dans l'image : c'est **le pilote
  virgl**, et il ne sert à rien tant que l'hôte ne fournit pas le rendu correspondant.
  La présence du pilote côté invité ne dit rien de ce que QEMU expose.

  Côté hôte, il faut `libvirglrenderer` et un vrai GPU, et un QEMU assez récent pour
  connaître `virtio-vga-gl` (vérifié sur QEMU 10.2.1).

- `hostfwd=tcp::2222-:22` — **la pièce maîtresse.** Elle expose le SSH de la machine
  virtuelle sur le port 2222 de l'hôte, ce qui permet d'installer exactement comme sur un
  appareil réel. Le serveur SSH (dropbear) est actif par défaut.

L'amorçage fonctionne en BIOS comme en UEFI — l'image porte `syslinux` **et** `EFI/BOOT` —
donc le SeaBIOS par défaut de QEMU suffit, sans OVMF.

Le mot de passe root est **`linux`**. Il n'est remplacé par un mot de passe généré que si
`system.security.enabled=1` est posé dans la configuration — c'est `S35securepasswd` qui en
décide au démarrage. En cas de doute : `batocera-config getRootPassword` dans la fenêtre de
la machine virtuelle.

### Installer, depuis l'hôte

```bash
unzip igiris-frontend-1.2.1-x86_64.zip
cd igiris-frontend-1.2.1-x86_64

scp -P 2222 bin/igiris-frontend root@localhost:/userdata/system/
scp -P 2222 share/igiris-frontend/packaging/batocera/install-on-device.sh root@localhost:/userdata/system/
scp -P 2222 share/igiris-frontend/tools/fetch-export.sh root@localhost:/userdata/system/

ssh -p 2222 root@localhost 'sh /userdata/system/install-on-device.sh'
ssh -p 2222 root@localhost 'bash /userdata/system/fetch-export.sh /userdata/system/igiris'
ssh -p 2222 root@localhost reboot
```

Le script doit annoncer **`▶ chaîne détectée : openbox (X11)`**. S'il annonce `labwc`, c'est
l'image ARM qui a été téléchargée.

### Un écran noir ne veut pas dire un échec

Quatre situations produisent un écran noir, et **trois sont normales**. Les distinguer évite
de chercher un problème qui n'existe pas.

| Écran noir | Ce que c'est | Comment trancher |
|---|---|---|
| pendant l'amorçage | **normal** — Batocera démarre en `quiet loglevel=0` sur `tty3` | amorcer le label `verbose`, ou ajouter `console=ttyS0,115200` avec `-serial file:boot.log` |
| au 1er démarrage, prolongé | **normal** — `autoresize` étend la partition `userdata` | patienter, ne pas conclure trop vite |
| dans une capture `screendump` (QMP) | **normal avec `gl=on`** — la surface est un tampon GPU que `screendump` ne sait pas lire | se fier à la fenêtre QEMU elle-même, pas à la capture |
| après le redémarrage, définitif | **là, c'est un vrai échec** | voir ci-dessous |

Le diagnostic est dans la machine, pas dans QEMU :

```bash
ssh -p 2222 root@localhost 'cat /userdata/system/logs/igiris.log'
```

> ⚠️ **Un log vide n'est pas un échec.** Le frontend n'écrit rien sur sa sortie tant qu'il
> tourne normalement : la boucle Qt est silencieuse. Un fichier vide signifie « aucune
> erreur », pas « rien ne s'est lancé ». Pour vérifier qu'il tourne :
> `ssh -p 2222 root@localhost 'pidof igiris-frontend'`.

Si le log contient `GLX is not supported` ou une erreur de contexte OpenGL, le problème est
la ligne QEMU, pas le frontend : reprenez `-device virtio-vga-gl -display gtk,gl=on`.

Et le retour en arrière ne demande pas de réinstaller :

```bash
ssh -p 2222 root@localhost \
  'cp /userdata/system/igiris/autostart.original /etc/X11/xinit/xinitrc && batocera-save-overlay && reboot'
```

---

## En cas de problème

| Symptôme | Cause | Solution |
|---|---|---|
| `module "QtQml.WorkerScript" is not installed` | dépendance manquante | installer par le `.deb`, pas par l'archive |
| `version 'Qt_6.4' not found` | Ubuntu 22.04 | il faut 24.04 |
| `export introuvable` | catalogue absent | relancer `fetch-export.sh` |
| `Illegal option -o pipefail` | script lancé avec `sh` | `./fetch-export.sh`, pas `sh fetch-export.sh` |
| `could not connect to display` | session non graphique (SSH) | lancer depuis le bureau, ou `QT_QPA_PLATFORM=offscreen` |
| Batocera redémarre sur EmulationStation | overlay non persisté | `batocera-save-overlay` |
| `GLX is not supported` en VM | QEMU n'expose pas d'accélération | `-device virtio-vga-gl -display gtk,gl=on` |

---

## Construire depuis les sources

```bash
sudo apt install -y build-essential cmake ninja-build \
    qt6-base-dev qt6-declarative-dev libqt6opengl6-dev libsqlite3-dev zlib1g-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Empaquetage : `cd build && cpack` produit l'archive et, si `dpkg-shlibdeps` est présent, le
paquet Debian.

---

## Comment c'est fait

L'appareil ne fait que **deux choses** : chercher un jeu par son nom, et retrouver un jeu
depuis un fichier local (par CRC32, ou par nom de romset pour l'arcade). **Aucun
rapprochement de titres n'a lieu sur l'appareil** — la normalisation, le fuzzy, l'arbitrage
des versions et la résolution région → langue sont précalculés par le backend
[`igiris`](https://github.com/gllmthebeast/igis) et livrés dans un export SQLite ouvert en
lecture seule immuable.

Tout ce qui est propre à une distribution est isolé derrière **un adaptateur**. Le reste du
code — chargement du catalogue, scan, calcul des statuts, interface QML — ignore sur quelle
distribution il tourne. Une règle de CI l'impose et échoue si une chaîne spécifique
apparaît ailleurs.

Le contexte complet, les décisions et leurs raisons : [`CLAUDE.md`](CLAUDE.md).
Le plan de développement et ce que chaque lot a révélé : [`docs/LOTS.md`](docs/LOTS.md).

---

## Compatibilité

| Distribution | Statut |
|---|---|
| Batocera | cible de référence |
| Recalbox | cible de validation |
| RetroPie, Retrobat, EmuELEC, Knulli | supportés |
| ES-DE | adaptable |
| Lakka, muOS | **hors périmètre** — aucune couche EmulationStation à remplacer |

Consomme l'export igiris **1.4.0**. Un export 1.3.0 reste lisible, sans les badges de
langue : le frontend contrôle la version **majeure** du schéma, les mineures étant additives.
