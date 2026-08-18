#!/usr/bin/env bash
# Récupère l'export igiris et NE REMPLACE l'ancien QUE si l'empreinte concorde.
#
# Un export tronqué ne doit jamais devenir l'export courant : on télécharge à côté, on
# vérifie, puis on bascule d'un seul mv (atomique sur le même système de fichiers).
#
# Usage : bash tools/fetch-export.sh [répertoire]   (défaut : ./data)
#
# Trois variables d'environnement, pour que l'APPLICATION puisse s'en servir sans que la
# logique vérifiée soit dupliquée en C++ :
#
#   IGIRIS_MACHINE=1        émet des lignes « igiris:clé=valeur » sur stdout, analysables.
#   IGIRIS_LIMIT_RATE=1M    plafonne le débit de curl — une borne qui télécharge 26 Mo ne
#                           doit pas manger la connexion pendant qu'on joue.
#   IGIRIS_STAGE_DIR=…      répertoire de travail imposé, au lieu d'un mktemp. C'est ce qui
#                           rend la PROGRESSION observable : l'appelant connaît le chemin du
#                           fichier qui grossit, et n'a rien à deviner.
set -euo pipefail
BASE="${IGIRIS_EXPORT_BASE:-https://igiris.xyz/exports}"
DEST="${1:-$(cd "$(dirname "$0")/.." && pwd)/data}"
mkdir -p "$DEST"

MACHINE="${IGIRIS_MACHINE:-}"
say() { [ -n "$MACHINE" ] && echo "igiris:$1" || true; }

LIMIT=()
[ -n "${IGIRIS_LIMIT_RATE:-}" ] && LIMIT=(--limit-rate "$IGIRIS_LIMIT_RATE")

if [ -n "${IGIRIS_STAGE_DIR:-}" ]; then
    tmp="$IGIRIS_STAGE_DIR"
    mkdir -p "$tmp"
else
    tmp="$(mktemp -d "$DEST/.dl.XXXXXX")"
fi
trap 'rm -rf "$tmp"' EXIT

echo "▶ Manifeste…"
curl -sSfL -m 60 "$BASE/manifest.json" -o "$tmp/manifest.json"
want="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json'))['sha256'])")"
ver="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json'))['schema_version'])")"
gen="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json'))['generated_at'])")"
size="$(python3 -c "import json;print(json.load(open('$tmp/manifest.json')).get('size_bytes',0))")"
echo "  schéma $ver · généré $gen"
say "version=$ver"
say "generated=$gen"

# Rien à faire si on a déjà exactement ce fichier.
if [ -f "$DEST/games.db" ]; then
    have="$(sha256sum "$DEST/games.db" | cut -d' ' -f1)"
    if [ "$have" = "$want" ]; then
        echo "✓ Déjà à jour ($ver) — rien à télécharger."
        say "status=uptodate"
        exit 0
    fi
fi

echo "▶ Téléchargement…"
# Annoncés AVANT de commencer : l'appelant peut afficher une progression dès la première
# seconde, au lieu d'attendre le premier octet pour savoir quoi afficher.
say "total=$size"
say "file=$tmp/games.db"
say "status=downloading"
curl -sSfL -m 900 "${LIMIT[@]}" "$BASE/games.db" -o "$tmp/games.db"
got="$(sha256sum "$tmp/games.db" | cut -d' ' -f1)"
if [ "$got" != "$want" ]; then
    echo "✗ Empreinte incorrecte — l'ancien export est CONSERVÉ." >&2
    echo "  attendu $want" >&2
    echo "  obtenu  $got" >&2
    say "status=badhash"
    exit 1
fi

# Bascule atomique : à aucun instant l'appareil ne voit un fichier incomplet.
mv "$tmp/games.db" "$DEST/games.db"
mv "$tmp/manifest.json" "$DEST/manifest.json"
say "status=ok"
echo "✓ Export à jour : $DEST/games.db ($(du -h "$DEST/games.db" | cut -f1), schéma $ver)"
