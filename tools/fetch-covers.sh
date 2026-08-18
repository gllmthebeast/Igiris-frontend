#!/usr/bin/env bash
# Constitue le cache local de vignettes, pour que l'appareil devienne autonome.
#
# ⚠️ CE SCRIPT NE REDISTRIBUE RIEN. Chaque appareil télécharge ses vignettes DEPUIS IGDB,
# c'est-à-dire depuis la source que le frontend interroge déjà à chaque affichage. La seule
# différence est qu'il le fait UNE fois et garde le résultat, au lieu de refaire la même
# requête à chaque défilement.
#
# C'est ce qui lève le point bloquant du §11 : héberger nous-mêmes un pack ferait de nous
# un distributeur d'images qui ne nous appartiennent pas. Là, rien ne transite par nous.
#
# Usage : bash tools/fetch-covers.sh [répertoire-données]   (défaut : ./data)
#
#   IGIRIS_MACHINE=1        lignes « igiris:clé=valeur » analysables par l'application
#   IGIRIS_LIMIT_RATE=1M    plafond de débit passé à curl
#   IGIRIS_COVER_VARIANT    variante IGDB (défaut t_cover_small, 90×120, ~3,9 Ko)
#   IGIRIS_WITH_ARTWORK=0   retire les FONDS D'ÉCRAN des fiches. Ils sont pris PAR DÉFAUT
#                           depuis la 1.15.0 : la fiche est alors entièrement hors ligne,
#                           décor compris, et plus rien ne dépend du réseau une fois le
#                           cache constitué.
#
#                           Ce que ça coûte, mesuré et non estimé :
#
#                             vignettes  t_cover_small      3,9 Ko →   66 Mo
#                             fonds      t_screenshot_med    30 Ko →  500 Mo   ← défaut
#                             fonds      t_720p              88 Ko → 1458 Mo
#
#                           Soit ≈ 570 Mo au total. La variante par défaut n'est pas la
#                           plus belle : c'est celle qui reste raisonnable sur une carte SD.
#                           IGIRIS_ARTWORK_VARIANT=t_720p pour le confort, si la place est là.
set -euo pipefail

DATA="${1:-$(cd "$(dirname "$0")/.." && pwd)/data}"
DB="$DATA/games.db"
DEST="$DATA/covers"
VARIANT="${IGIRIS_COVER_VARIANT:-t_cover_small}"
# Les fonds sont pris PAR DÉFAUT, et se retirent explicitement. L'inverse — les proposer
# en option — laissait la fiche dépendre du réseau pour son décor alors que tout le reste
# était local : une seule chose manquante suffit à faire mentir « hors ligne ».
#
# La valeur est comparée à 0 et non testée « non vide » : IGIRIS_WITH_ARTWORK=0 doit
# désactiver, pas activer parce que la chaîne « 0 » n'est pas vide.
ARTWORK="${IGIRIS_WITH_ARTWORK:-1}"
[ "$ARTWORK" = "0" ] && ARTWORK=""
ARTWORK_VARIANT="${IGIRIS_ARTWORK_VARIANT:-t_screenshot_med}"

MACHINE="${IGIRIS_MACHINE:-}"
say() { [ -n "$MACHINE" ] && echo "igiris:$1" || true; }

LIMIT=()
[ -n "${IGIRIS_LIMIT_RATE:-}" ] && LIMIT=(--limit-rate "$IGIRIS_LIMIT_RATE")

[ -f "$DB" ] || { echo "✗ catalogue introuvable : $DB" >&2; say "status=nodb"; exit 1; }
mkdir -p "$DEST"

# La liste vient du catalogue, jamais d'une convention de nommage : c'est un LOOKUP, comme
# tout ce que fait cet appareil (§0). La variante est substituée ICI et pas dans
# l'application — l'appareil ne fabrique aucune URL.
LISTE="$(mktemp)"
trap 'rm -f "$LISTE"' EXIT
sqlite3 "$DB" \
  "SELECT game_key || ' ' || replace(cover_ref, 't_cover_big', '$VARIANT')
     FROM exp_game WHERE cover_ref IS NOT NULL AND cover_ref != '';" > "$LISTE"

# Les fonds sont rangés sous un SUFFIXE de clé, jamais dans un second répertoire : le nom
# du fichier reste la clé du jeu, et l'application n'a toujours qu'un lookup à faire (§0).
if [ -n "$ARTWORK" ]; then
    sqlite3 "$DB" \
      "SELECT game_key || '-fond ' || replace(artwork_ref, 't_1080p', '$ARTWORK_VARIANT')
         FROM exp_game WHERE artwork_ref IS NOT NULL AND artwork_ref != '';" >> "$LISTE"
    echo "▶ fonds d'écran inclus ($ARTWORK_VARIANT) — IGIRIS_WITH_ARTWORK=0 pour s'en passer"
else
    echo "▶ fonds d'écran exclus — les fiches iront les chercher en ligne"
fi

total="$(wc -l < "$LISTE")"
say "total=$total"
say "status=downloading"
echo "▶ $total images · variante $VARIANT · destination $DEST"

# QUATRE en parallèle, et pas davantage.
#
# En séquentiel, 17 000 vignettes prennent une heure : mesuré à 0,21 s l'unité, dont
# l'essentiel est la latence et non le transfert. Quatre ramène ça au quart.
#
# Pourquoi pas plus : ces requêtes partent vers un service tiers que nous n'exploitons pas.
# Ouvrir vingt connexions par appareil serait discourtois, et se ferait étrangler — c'est
# exactement ce qui est arrivé au backend avec GitHub, et qui a bloqué ses dats pendant
# une journée entière.
PARALLELE="${IGIRIS_JOBS:-4}"

export DEST LIMIT_STR="${IGIRIS_LIMIT_RATE:-}"
recupere() {
    key="${1%% *}"; url="${1#* }"
    cible="$DEST/$key.jpg"
    # REPRENABLE : ce qui est déjà là n'est pas retéléchargé. Une coupure au milieu de
    # 17 000 fichiers ne doit pas tout recommencer.
    [ -s "$cible" ] && return 0
    limit=""
    [ -n "$LIMIT_STR" ] && limit="--limit-rate $LIMIT_STR"
    # Écrit à côté puis renommé : un fichier partiel ne doit jamais passer pour une
    # vignette valide, sinon la reprise le sauterait.
    if curl -sSfL -m 30 $limit "$url" -o "$cible.part" 2>/dev/null; then
        mv "$cible.part" "$cible"
    else
        # Un échec isolé n'arrête RIEN : la vignette manquante retombe sur l'affichage
        # réseau, et la passe suivante la reprendra.
        rm -f "$cible.part"
    fi
}
export -f recupere

# La progression se compte sur les FICHIERS PRÉSENTS, pas sur un compteur de boucle : avec
# plusieurs tâches en parallèle, c'est la seule mesure qui reste juste — et elle survit à
# une interruption, puisqu'elle repart du disque.
xargs -d '\n' -P "$PARALLELE" -I{} bash -c 'recupere "{}"' < "$LISTE"

pris="$(find "$DEST" -maxdepth 1 -name '*.jpg' -type f | wc -l)"
say "done=$pris"
say "status=ok"
echo "✓ $pris / $total images · $(du -sh "$DEST" | cut -f1) dans $DEST"
