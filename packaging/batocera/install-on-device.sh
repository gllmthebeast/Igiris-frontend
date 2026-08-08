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
AUTOSTART="/usr/share/labwc/autostart"
BACKUP="/userdata/system/igiris/autostart.original"

if [ ! -f "$BIN_SRC" ]; then
    echo "✗ binaire introuvable : $BIN_SRC" >&2
    echo "  usage : sh install-on-device.sh [chemin-du-binaire]" >&2
    exit 1
fi

if [ ! -f "$AUTOSTART" ]; then
    echo "✗ $AUTOSTART absent : cette version ne démarre pas via labwc." >&2
    echo "  Ne rien modifier au hasard — vérifier la chaîne de démarrage d'abord." >&2
    exit 1
fi

mkdir -p /userdata/system/igiris /userdata/system/logs
cp -f "$BIN_SRC" "$BIN_DST"
chmod +x "$BIN_DST"

# Conserver l'original AVANT de toucher à quoi que ce soit : c'est ce qui rend le
# retour en arrière possible.
if [ ! -f "$BACKUP" ]; then
    cp -f "$AUTOSTART" "$BACKUP"
    echo "✓ autostart d'origine conservé dans $BACKUP"
fi

# La partition système est en lecture seule : la remonter le temps de l'écriture.
mount -o remount,rw / 2>/dev/null || true

# Remplacer UNE ligne, celle qui lance EmulationStation.
sed -i "s|^/usr/bin/emulationstation-standalone.*|$BIN_DST > /userdata/system/logs/igiris.log 2>\&1|" \
    "$AUTOSTART"

mount -o remount,ro / 2>/dev/null || true

if grep -q "igiris-frontend" "$AUTOSTART"; then
    echo "✓ autostart modifié — redémarrer pour lancer igiris-frontend"
    echo "  retour en arrière : cp $BACKUP $AUTOSTART"
else
    echo "✗ la ligne d'EmulationStation n'a pas été trouvée dans $AUTOSTART" >&2
    echo "  rien n'a été remplacé ; l'appareil démarre toujours sur EmulationStation." >&2
    exit 1
fi

echo
echo "N'oubliez pas l'export : /userdata/system/igiris/games.db"
echo "  bash tools/fetch-export.sh /userdata/system/igiris"
