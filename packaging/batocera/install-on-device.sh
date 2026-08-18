#!/bin/sh
# Installe igiris-frontend à la place d'EmulationStation, à chaud, sur un appareil déjà
# installé — pour ESSAYER sans reconstruire d'image.
#
# Établi sur Batocera 43.1 : la chaîne de démarrage passe par labwc, dont le fichier
# autostart lance EmulationStation en dernière ligne. On remplace cette ligne, et rien
# d'autre (§16).
#
# Tout est écrit dans /userdata, la seule partition inscriptible. Le reste du système est
# en lecture seule et serait écrasé à la prochaine mise à jour.
set -eu

BIN_SRC="${1:-/userdata/system/igiris-frontend}"
BIN_DST="/userdata/system/igiris/igiris-frontend"
BACKUP="/userdata/system/igiris/autostart.original"
# Le lanceur, et non le binaire, est ce que la chaîne de démarrage appelle : voir la
# « porte de sortie » plus bas.
START_DST="/userdata/system/igiris/start.sh"

# Le frontend d'origine, qui reste le SEUL moyen de configurer la machine — manettes,
# wifi, audio, mise à jour — et donc à la fois le repli en cas d'échec et la cible de
# l'entrée « Paramètres » de notre interface.
if command -v emulationstation-standalone >/dev/null 2>&1; then
    ES_CMD="$(command -v emulationstation-standalone)"
elif command -v emulationstation >/dev/null 2>&1; then
    ES_CMD="$(command -v emulationstation)"
else
    ES_CMD="/usr/bin/emulationstation-standalone"
fi

if [ ! -f "$BIN_SRC" ]; then
    echo "✗ binaire introuvable : $BIN_SRC" >&2
    echo "  usage : sh install-on-device.sh [chemin-du-binaire]" >&2
    exit 1
fi

# LA CHAÎNE DE DÉMARRAGE DÉPEND DE L'ARCHITECTURE, PAS DE LA VERSION.
#
# Constaté en montant les deux images 43.1 — même version, même date de build :
#
#   ARM (bcm2712)  Wayland : labwc, lancé par S31emulationstation, qui lit
#                  /usr/share/labwc/autostart dont la dernière ligne lance ES.
#   x86_64         X11 : S31emulationstation appelle startx, donc
#                  /etc/X11/xinit/xinitrc, dont la dernière ligne est
#                  « openbox --startup "emulationstation-standalone" ».
#
# Les plugins Qt le confirment : l'image ARM embarque libqwayland.so, celle x86_64 non —
# elle a xcb. Supposer une seule chaîne aurait produit un appareil qui redémarre sur
# EmulationStation sans rien dire.
if [ -f "/usr/share/labwc/autostart" ]; then
    AUTOSTART="/usr/share/labwc/autostart"
    # Toute la ligne est remplacée : c'est elle qui lance ES.
    PATTERN="^/usr/bin/emulationstation-standalone.*"
    REPLACEMENT="$START_DST"
    CHAIN="labwc (Wayland)"
elif [ -f "/etc/X11/xinit/xinitrc" ] \
     && grep -q 'startup "emulationstation-standalone"' /etc/X11/xinit/xinitrc; then
    AUTOSTART="/etc/X11/xinit/xinitrc"
    # Ici seul l'ARGUMENT de --startup change : openbox doit rester, sans quoi il n'y a
    # plus de gestionnaire de fenêtres et Qt n'a plus de surface où s'afficher.
    PATTERN='startup "emulationstation-standalone"'
    REPLACEMENT="startup \"$START_DST\""
    CHAIN="openbox (X11)"
else
    echo "✗ aucune chaîne de démarrage reconnue :" >&2
    echo "    ni /usr/share/labwc/autostart (Wayland)," >&2
    echo "    ni la ligne openbox de /etc/X11/xinit/xinitrc (X11)." >&2
    echo "  Ne rien modifier au hasard — vérifier la chaîne de démarrage d'abord." >&2
    exit 1
fi

echo "▶ chaîne détectée : $CHAIN — $AUTOSTART"

mkdir -p /userdata/system/igiris /userdata/system/logs
cp -f "$BIN_SRC" "$BIN_DST"
chmod +x "$BIN_DST"

# ------------------------------------------------------------------ porte de sortie
#
# La chaîne de démarrage n'appelle PAS le binaire directement, mais ce lanceur.
#
# Raison : en remplaçant l'écran d'accueil, on remplace la seule interface de la machine.
# Si notre binaire ne démarre pas — bibliothèque manquante, export corrompu, plantage —
# l'utilisateur se retrouve devant un écran noir, sans aucun moyen d'aller réparer. Sur une
# borne sans clavier ni réseau, ça veut dire reflasher la carte.
#
# Le lanceur rend donc la main à EmulationStation dès que le frontend échoue OU s'arrête
# trop vite pour avoir affiché quoi que ce soit. Le repli vaut mieux que le vide.
#
# Le lanceur vit dans /userdata, qui est la partition inscriptible : il se corrige sans
# remonter la racine en écriture, et sans batocera-save-overlay.
cat > "$START_DST" <<LANCEUR
#!/bin/sh
# Lanceur igiris — généré par install-on-device.sh, modifiable à la main.
LOG=/userdata/system/logs/igiris.log
FRONTEND=$BIN_DST
SECOURS=$ES_CMD

# Seuil au-delà duquel on considère que le frontend a VRAIMENT tourné. En dessous, même
# un code de sortie 0 est suspect : personne ne quitte un frontend en trois secondes.
MINIMUM=5

DEBUT=\$(date +%s)
"\$FRONTEND" >> "\$LOG" 2>&1
CODE=\$?
DUREE=\$(( \$(date +%s) - DEBUT ))

if [ "\$CODE" -ne 0 ] || [ "\$DUREE" -lt "\$MINIMUM" ]; then
    echo "[\$(date -Is)] igiris a rendu \$CODE apres \${DUREE}s — repli sur \$SECOURS" >> "\$LOG"
    exec "\$SECOURS"
fi
LANCEUR
chmod +x "$START_DST"
echo "✓ lanceur installé : $START_DST (repli sur $ES_CMD)"

# ------------------------------------------------- outil de mise à jour
#
# Déposé À CÔTÉ du binaire, c'est là qu'il le cherche en premier. Sans lui, le bouton de
# mise à jour de l'interface reste inerte : sur une borne sans clavier, c'est le SEUL moyen
# de récupérer un nouveau catalogue.
FETCH_SRC="$(dirname "$0")/../../tools/fetch-export.sh"
[ -f "$FETCH_SRC" ] || FETCH_SRC="$(dirname "$0")/fetch-export.sh"
if [ -f "$FETCH_SRC" ]; then
    cp -f "$FETCH_SRC" /userdata/system/igiris/fetch-export.sh
    chmod +x /userdata/system/igiris/fetch-export.sh
    echo "✓ outil de mise à jour installé : /userdata/system/igiris/fetch-export.sh"
else
    echo "⚠ fetch-export.sh introuvable — mise à jour depuis l'interface indisponible" >&2
fi

# ------------------------------------------------------- entrée « port »
#
# Déposée en plus, et sans condition : elle ne coûte rien et couvre le cas où l'utilisateur
# veut revenir à EmulationStation comme écran d'accueil tout en gardant igiris à portée.
#
# C'est aussi le mode d'installation le plus simple pris isolément — voir igiris.sh, qui
# peut être déposé SEUL, sans jamais toucher à la chaîne de démarrage.
PORT_SRC="$(dirname "$0")/igiris.sh"
PORT_DST="/userdata/roms/ports/igiris.sh"
if [ -f "$PORT_SRC" ]; then
    mkdir -p /userdata/roms/ports
    cp -f "$PORT_SRC" "$PORT_DST"
    chmod +x "$PORT_DST"
    echo "✓ entrée Ports installée : $PORT_DST"
else
    echo "⚠ igiris.sh introuvable à côté de ce script — entrée Ports non installée" >&2
fi

# Conserver l'original AVANT de toucher à quoi que ce soit : c'est ce qui rend le
# retour en arrière possible.
if [ ! -f "$BACKUP" ]; then
    cp -f "$AUTOSTART" "$BACKUP"
    echo "✓ autostart d'origine conservé dans $BACKUP"
fi

# La partition système est en lecture seule : la remonter le temps de l'écriture.
mount -o remount,rw / 2>/dev/null || true

# Remplacer UNE chose, celle qui lance EmulationStation — la ligne entière sous Wayland,
# le seul argument de --startup sous X11.
sed -i "s|$PATTERN|$REPLACEMENT|" "$AUTOSTART"

if grep -q "igiris-frontend" "$AUTOSTART"; then
    # SANS CECI, TOUT CE QUI PRÉCÈDE EST PERDU AU REDÉMARRAGE.
    #
    # La racine de Batocera est un squashfs en lecture seule recouvert d'un overlay EN RAM :
    # « remount,rw / » rend l'écriture possible, mais elle ne survit pas à l'extinction.
    # batocera-save-overlay persiste cet overlay dans /boot/boot/overlay — son propre
    # en-tête le dit : « if you modify the root using mount -o remount,rw / then, you need
    # to save it using this script ».
    #
    # L'appareil serait sinon redémarré droit sur EmulationStation, sans le moindre message
    # d'erreur, et la cause aurait été introuvable.
    if command -v batocera-save-overlay >/dev/null 2>&1; then
        echo "▶ persistance de la modification (batocera-save-overlay)…"
        batocera-save-overlay
        echo "✓ autostart modifié ET persisté — redémarrer pour lancer igiris-frontend"
    else
        mount -o remount,ro / 2>/dev/null || true
        echo "⚠ batocera-save-overlay introuvable : la modification n'est PAS persistée."
        echo "  Elle sera perdue au redémarrage. Vérifier la distribution avant d'aller"
        echo "  plus loin — le mécanisme de persistance diffère d'une image à l'autre."
    fi
    echo "  retour en arrière : cp $BACKUP $AUTOSTART puis batocera-save-overlay"
else
    mount -o remount,ro / 2>/dev/null || true
    echo "✗ la ligne d'EmulationStation n'a pas été trouvée dans $AUTOSTART" >&2
    echo "  rien n'a été remplacé ; l'appareil démarre toujours sur EmulationStation." >&2
    exit 1
fi

echo
echo "N'oubliez pas l'export : /userdata/system/igiris/games.db"
echo "  bash tools/fetch-export.sh /userdata/system/igiris"
