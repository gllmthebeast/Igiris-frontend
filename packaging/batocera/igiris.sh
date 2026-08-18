#!/bin/bash
# igiris — lanceur « port » pour EmulationStation.
#
# Déposé dans /userdata/roms/ports/, ce script apparaît dans EmulationStation comme une
# entrée lançable, au même titre qu'un jeu. C'est la façon la plus simple d'essayer igiris :
#
#   • aucun droit root, aucun remontage de la racine, aucun batocera-save-overlay ;
#   • la chaîne de démarrage n'est PAS touchée — Batocera démarre comme avant ;
#   • tout vit dans /userdata, qui survit aux mises à jour du système ;
#   • désinstallation = supprimer ce fichier.
#
# En quittant igiris (bouton « Retour à Batocera », ou F1), on revient ici.
#
# Pour aller plus loin — faire d'igiris l'écran d'accueil au démarrage — voir
# install-on-device.sh, qui lui touche à la chaîne de démarrage.

FRONTEND=/userdata/system/igiris/igiris-frontend
LOG=/userdata/system/logs/igiris.log

mkdir -p /userdata/system/logs

# ------------------------------------------------- déjà lancé ?
#
# Constaté sur appareil : avec igiris installé À LA FOIS au démarrage et en port, lancer
# l'entrée Ports en démarrait un SECOND — deux processus, deux fenêtres, un seul écran.
#
# On refuse, et on le dit. Se contenter de sortir en silence laisserait l'utilisateur
# devant un écran qui ne change pas, sans comprendre pourquoi.
if pgrep -x igiris-frontend >/dev/null 2>&1; then
    echo "[$(date -Is)] déjà lancé — l'entrée Ports ne fait rien" >> "$LOG"
    echo "igiris tourne déjà : c'est lui l'écran d'accueil de cet appareil."
    echo "Cette entrée Ports ne sert que si Batocera démarre sur EmulationStation."
    sleep 4
    exit 0
fi

if [ ! -x "$FRONTEND" ]; then
    # Message VERBATIM et visible : sans écran de terminal, le journal est le seul endroit
    # où l'utilisateur peut comprendre ce qui manque.
    echo "[$(date -Is)] igiris introuvable ou non exécutable : $FRONTEND" >> "$LOG"
    echo "  Copiez le binaire à cet emplacement, puis relancez depuis Ports." >> "$LOG"
    exit 1
fi

echo "[$(date -Is)] lancement depuis Ports" >> "$LOG"
exec "$FRONTEND" >> "$LOG" 2>&1
