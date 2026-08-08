#!/usr/bin/env bash
# Applique la règle du CLAUDE.md §1 :
#
#   « Aucune chaîne littérale spécifique à une distribution en dehors de l'adaptateur.
#     Pas de /userdata/, pas de batocera, pas de emulatorlauncher.py ailleurs.
#     C'est vérifiable par un simple grep, et ça doit l'être en CI. »
#
# Ce test existe dès le lot 0, avant la première ligne d'adaptateur : il est là pour
# empêcher la dérive, pas pour la constater après coup.
#
# Périmètre : le CODE seulement (src/ et qml/). La documentation a évidemment le droit de
# nommer les distributions — c'est même son travail.
# Exception : src/platform/, qui EST l'adaptateur. C'est le seul endroit autorisé.
#
# Usage : bash tools/check-no-distro-literals.sh [racine du dépôt]
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

# Marqueurs propres à une distribution hôte : chemins, noms de launcher, noms de projet.
PATTERN='/userdata/|batocera|emulatorlauncher\.py|runcommand\.sh|/recalbox|retropie|retrobat|emuelec|knulli'

SEARCH_DIRS=()
[ -d "$ROOT/src" ] && SEARCH_DIRS+=("$ROOT/src")
[ -d "$ROOT/qml" ] && SEARCH_DIRS+=("$ROOT/qml")

if [ ${#SEARCH_DIRS[@]} -eq 0 ]; then
    echo "✗ ni src/ ni qml/ sous $ROOT — le test ne vérifie rien, c'est une erreur."
    exit 1
fi

# --exclude-dir ne s'applique qu'au nom de dossier, ce qui suffit : l'adaptateur vit dans
# src/platform/ et nulle part ailleurs.
hits="$(grep -rIn -i -E "$PATTERN" "${SEARCH_DIRS[@]}" --exclude-dir=platform 2>/dev/null || true)"

if [ -n "$hits" ]; then
    echo "✗ Chaîne spécifique à une distribution trouvée hors de l'adaptateur (§1) :"
    echo
    echo "$hits" | sed "s|^$ROOT/|    |"
    echo
    echo "  Ces valeurs se lisent dans le fichier de description des systèmes, via"
    echo "  l'adaptateur de plateforme. Elles ne se codent jamais en dur ailleurs."
    exit 1
fi

echo "✓ Aucune chaîne spécifique à une distribution hors de src/platform/."
exit 0
