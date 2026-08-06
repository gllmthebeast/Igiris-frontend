#!/usr/bin/env bash
# Récupère l'export igiris et NE REMPLACE l'ancien QUE si l'empreinte concorde.
#
# Un export tronqué ne doit jamais devenir l'export courant : on télécharge à côté, on
# vérifie, puis on bascule d'un seul mv (atomique sur le même système de fichiers).
#
# Usage : bash tools/fetch-export.sh [répertoire]   (défaut : ./data)
set -euo pipefail
BASE="${IGIRIS_EXPORT_BASE:-https://igiris.xyz/exports}"
DEST="${1:-$(cd "$(dirname "$0")/.." && pwd)/data}"
mkdir -p "$DEST"

tmp="$(mktemp -d "$DEST/.dl.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

echo "▶ Manifeste…"
curl -sSfL -m 60 "$BASE/manifest.json" -o "$tmp/manifest.json"
want="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json'))['sha256'])")"
ver="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json'))['schema_version'])")"
gen="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json'))['generated_at'])")"
echo "  schéma $ver · généré $gen"

# Rien à faire si on a déjà exactement ce fichier.
if [ -f "$DEST/games.db" ]; then
    have="$(sha256sum "$DEST/games.db" | cut -d' ' -f1)"
    if [ "$have" = "$want" ]; then
        echo "✓ Déjà à jour ($ver) — rien à télécharger."
        exit 0
    fi
fi

echo "▶ Téléchargement…"
curl -sSfL -m 900 "$BASE/games.db" -o "$tmp/games.db"
got="$(sha256sum "$tmp/games.db" | cut -d' ' -f1)"
if [ "$got" != "$want" ]; then
    echo "✗ Empreinte incorrecte — l'ancien export est CONSERVÉ." >&2
    echo "  attendu $want" >&2
    echo "  obtenu  $got" >&2
    exit 1
fi

# Bascule atomique : à aucun instant l'appareil ne voit un fichier incomplet.
mv "$tmp/games.db" "$DEST/games.db"
mv "$tmp/manifest.json" "$DEST/manifest.json"
echo "✓ Export à jour : $DEST/games.db ($(du -h "$DEST/games.db" | cut -f1), schéma $ver)"
