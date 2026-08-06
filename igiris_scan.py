#!/usr/bin/env python3
"""igiris-scan — identifie les ROMs présentes sur un appareil et les confronte aux votes.

Parcourt une arborescence de ROMs façon Batocera (`roms/<système>/…`), identifie chaque
fichier, puis répond à trois questions :

  1. Qu'est-ce que je possède, et qu'est-ce que l'appareil ne reconnaît pas ?
  2. Ai-je des doublons — le même jeu sur plusieurs systèmes ?
  3. Ai-je la MEILLEURE version ? La communauté a voté, et certaines plateformes
     s'émulent plus fidèlement que d'autres.

Aucune dépendance hors bibliothèque standard : ça tourne tel quel sur un Raspberry Pi.
Aucun accès réseau : tout est précalculé dans l'export (cf. CLAUDE.md).

Usage :
    python3 igiris_scan.py /userdata/roms
    python3 igiris_scan.py ~/roms --db data/games.db --json rapport.json
    python3 igiris_scan.py ~/roms --system snes,megadrive
"""
import argparse
import json
import os
import sqlite3
import sys
import zipfile
import zlib
from collections import defaultdict

# Version MAJEURE du schéma d'export que cet outil sait lire.
SUPPORTED_MAJOR = 1

# Systèmes où l'identification se fait par NOM DE FICHIER et non par CRC : le hash d'un
# .zip d'arcade change dès qu'on reconstruit le romset (merged / split / non-merged).
ARCADE_SYSTEMS = {"mame", "fbneo", "neogeo", "naomi", "atomiswave", "arcade",
                  "model2", "model3", "cps1", "cps2", "cps3", "gameandwatch"}

# En-têtes que certains formats portent, et que les dats n'incluent pas toujours. Quand
# le CRC direct ne donne rien, on retente en sautant ces octets — sinon des ROMs valides
# passeraient pour inconnues.
HEADER_SIZES = {"nes": 16, "atari7800": 128, "lynx": 64, "fds": 16}

# Extensions qui ne sont pas des jeux : on ne les compte pas comme « non reconnues ».
IGNORED_EXT = {".txt", ".xml", ".jpg", ".png", ".cfg", ".dat", ".md5", ".sha1", ".nfo",
               ".srm", ".state", ".sav", ".cue", ".m3u", ".auto", ".db", ".json", ".bak"}
IGNORED_DIRS = {"media", "images", "videos", "manuals", "downloaded_images", ".git"}

CHUNK = 1 << 20  # 1 Mio : les images de CD pèsent des centaines de Mo


def crc32_file(path, skip=0):
    """CRC32 d'un fichier, en sautant éventuellement un en-tête. Lecture par blocs."""
    crc = 0
    try:
        with open(path, "rb") as f:
            if skip:
                f.read(skip)
            while True:
                b = f.read(CHUNK)
                if not b:
                    break
                crc = zlib.crc32(b, crc)
    except OSError:
        return None
    return f"{crc & 0xFFFFFFFF:08X}"


def crc32_zip(path):
    """CRC des fichiers CONTENUS dans une archive.

    Le hash doit porter sur le contenu, jamais sur le zip lui-même : deux archives du même
    jeu diffèrent selon la compression. Bonus : le CRC est déjà stocké dans l'en-tête zip,
    donc aucune décompression n'est nécessaire.
    """
    try:
        with zipfile.ZipFile(path) as z:
            return [f"{i.CRC & 0xFFFFFFFF:08X}" for i in z.infolist() if not i.is_dir()]
    except (zipfile.BadZipFile, OSError):
        return []


class Catalog:
    """Accès en lecture seule à l'export. Ouvert en immuable : aucun verrou, aucun -shm."""

    def __init__(self, path):
        if not os.path.exists(path):
            sys.exit(f"✗ export introuvable : {path}\n"
                     f"  → lancer d'abord : bash tools/fetch-export.sh")
        self.con = sqlite3.connect(f"file:{path}?immutable=1", uri=True)
        meta = dict(self.con.execute("SELECT key, value FROM exp_meta").fetchall())
        self.version = meta.get("schema_version", "0.0.0")
        if int(self.version.split(".")[0]) != SUPPORTED_MAJOR:
            sys.exit(f"✗ export en schéma {self.version}, cet outil lit la majeure "
                     f"{SUPPORTED_MAJOR}. Mise à jour nécessaire.")
        self.generated = meta.get("generated_at", "?")

    def by_crc(self, crc, system):
        return self.con.execute(
            "SELECT game_key FROM exp_rom_hash WHERE crc32 = ? AND batocera_system = ?",
            (crc, system)).fetchone()

    def by_crc_any(self, crc):
        """Repli : le fichier est peut-être rangé dans le mauvais dossier."""
        return self.con.execute(
            "SELECT game_key, batocera_system FROM exp_rom_hash WHERE crc32 = ? LIMIT 1",
            (crc,)).fetchone()

    def by_romset(self, romset, system):
        r = self.con.execute(
            "SELECT game_key, hardware, emulators, driver_status FROM exp_romset "
            "WHERE romset = ? AND batocera_system = ?", (romset, system)).fetchone()
        if r:
            return r
        return self.con.execute(
            "SELECT game_key, hardware, emulators, driver_status FROM exp_romset "
            "WHERE romset = ? LIMIT 1", (romset,)).fetchone()

    def game(self, key):
        return self.con.execute(
            "SELECT title, year, rating FROM exp_game WHERE game_key = ?", (key,)).fetchone()

    def platforms(self, key):
        return self.con.execute(
            "SELECT display_name, batocera_system, emu_score, is_preferred "
            "FROM exp_game_platform WHERE game_key = ? ORDER BY emu_score DESC", (key,)
        ).fetchall()


def scan(root, cat, only=None, verbose=False):
    found, unknown, scanned = [], [], 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d.lower() not in IGNORED_DIRS]
        # Le système est le dossier directement sous la racine : roms/<système>/…
        rel = os.path.relpath(dirpath, root)
        system = rel.split(os.sep)[0].lower()
        if system in (".", ""):
            continue
        if only and system not in only:
            continue
        arcade = system in ARCADE_SYSTEMS

        for name in filenames:
            ext = os.path.splitext(name)[1].lower()
            if ext in IGNORED_EXT or name.startswith("."):
                continue
            path = os.path.join(dirpath, name)
            scanned += 1
            stem = os.path.splitext(name)[0]

            if arcade:
                # Arcade : le nom du romset EST l'identifiant.
                r = cat.by_romset(stem.lower(), system)
                if r:
                    found.append({"path": path, "system": system, "game_key": r[0],
                                  "how": "romset", "hardware": r[1],
                                  "emulators": r[2], "driver_status": r[3]})
                else:
                    unknown.append({"path": path, "system": system, "how": "romset"})
                continue

            # Console : par CRC. Pour une archive, on hashe le CONTENU.
            hit = None
            crcs = crc32_zip(path) if ext == ".zip" else []
            if not crcs:
                c = crc32_file(path)
                crcs = [c] if c else []
            for c in crcs:
                hit = cat.by_crc(c, system)
                if hit:
                    found.append({"path": path, "system": system, "game_key": hit[0],
                                  "how": "crc"})
                    break
            if hit:
                continue

            # Rien ? Peut-être un en-tête que le dat n'a pas — on retente sans lui.
            skip = HEADER_SIZES.get(system)
            if skip and ext != ".zip":
                c = crc32_file(path, skip=skip)
                if c:
                    hit = cat.by_crc(c, system)
                    if hit:
                        found.append({"path": path, "system": system, "game_key": hit[0],
                                      "how": f"crc(sans en-tête {skip}o)"})
                        continue

            # Dernier recours : le fichier est peut-être dans le mauvais dossier.
            for c in crcs:
                alt = cat.by_crc_any(c)
                if alt:
                    found.append({"path": path, "system": system, "game_key": alt[0],
                                  "how": "crc", "misplaced": alt[1]})
                    hit = alt
                    break
            if not hit:
                unknown.append({"path": path, "system": system, "how": "crc"})
        if verbose:
            print(f"  … {scanned} fichiers", end="\r", file=sys.stderr)
    return found, unknown, scanned


def report(cat, found, unknown, scanned, root):
    print(f"\n═══ igiris-scan · {root}")
    print(f"    export schéma {cat.version}, généré {cat.generated}\n")
    pct = 100 * len(found) / scanned if scanned else 0
    print(f"  {scanned} fichiers examinés · {len(found)} identifiés ({pct:.0f} %) · "
          f"{len(unknown)} inconnus")

    by_sys = defaultdict(lambda: [0, 0])
    for f in found:
        by_sys[f["system"]][0] += 1
    for u in unknown:
        by_sys[u["system"]][1] += 1
    print("\n  ── par système ──")
    for s, (ok, ko) in sorted(by_sys.items(), key=lambda x: -x[1][0])[:15]:
        print(f"    {s:16s} {ok:5d} identifiés   {ko:5d} inconnus")

    # Fichiers rangés dans le mauvais dossier : cause d'échec fréquente et facile à corriger.
    misplaced = [f for f in found if f.get("misplaced")]
    if misplaced:
        print(f"\n  ── {len(misplaced)} fichier(s) dans le mauvais dossier ──")
        for f in misplaced[:8]:
            print(f"    {os.path.basename(f['path'])[:48]:48s} "
                  f"{f['system']} → devrait être dans « {f['misplaced']} »")

    # Doublons : le même jeu possédé sur plusieurs systèmes.
    by_game = defaultdict(list)
    for f in found:
        by_game[f["game_key"]].append(f)
    dups = {k: v for k, v in by_game.items() if len({x["system"] for x in v}) > 1}
    if dups:
        print(f"\n  ── {len(dups)} jeu(x) possédé(s) sur plusieurs systèmes ──")
        for k, v in list(dups.items())[:8]:
            g = cat.game(k)
            systems = ", ".join(sorted({x["system"] for x in v}))
            print(f"    {(g[0] if g else k)[:44]:44s} {systems}")

    # LE point qui relie l'appareil au vote : possèdes-tu la meilleure version ?
    suggestions = []
    for key, files in by_game.items():
        owned = {f["system"] for f in files}
        plats = cat.platforms(key)
        if not plats:
            continue
        best = next((p for p in plats if p[3]), None)          # élue par les votes
        if best and best[1] and best[1] not in owned:
            cur = max((p for p in plats if p[1] in owned), key=lambda p: p[2] or 0,
                      default=None)
            if cur and (best[2] or 0) > (cur[2] or 0):
                g = cat.game(key)
                suggestions.append((g[0] if g else key, cur[0], cur[2], best[0], best[2]))
    if suggestions:
        suggestions.sort(key=lambda s: -( (s[4] or 0) - (s[2] or 0) ))
        print(f"\n  ── {len(suggestions)} jeu(x) dont une meilleure version existe ──")
        print("     (plateforme élue par les votes, et mieux émulée)")
        for t, cs, ce, bs, be in suggestions[:10]:
            print(f"    {t[:38]:38s} tu as {cs} ({ce}) → {bs} ({be})")

    if unknown:
        print(f"\n  ── {min(len(unknown), 8)} exemple(s) de fichiers non identifiés ──")
        for u in unknown[:8]:
            print(f"    {u['system']:12s} {os.path.basename(u['path'])[:56]}")
        if len(unknown) > 8:
            print(f"    … et {len(unknown) - 8} autres")
    print()
    return {"scanned": scanned, "identified": len(found), "unknown": len(unknown),
            "duplicates": len(dups), "suggestions": len(suggestions)}


def main():
    ap = argparse.ArgumentParser(description="Identifie les ROMs et les confronte aux votes igiris.")
    ap.add_argument("roms", help="racine des ROMs (ex. /userdata/roms)")
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--db", default=os.path.join(here, "data", "games.db"),
                    help="chemin de l'export (déf. data/games.db)")
    ap.add_argument("--system", help="ne scanner que ces systèmes, séparés par des virgules")
    ap.add_argument("--json", help="écrire le rapport détaillé dans ce fichier")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    if not os.path.isdir(a.roms):
        sys.exit(f"✗ répertoire introuvable : {a.roms}")
    cat = Catalog(a.db)
    only = {s.strip().lower() for s in a.system.split(",")} if a.system else None

    found, unknown, scanned = scan(a.roms, cat, only, a.verbose)
    summary = report(cat, found, unknown, scanned, a.roms)

    if a.json:
        for f in found:
            g = cat.game(f["game_key"])
            if g:
                f["title"], f["year"], f["rating"] = g
        json.dump({"summary": summary, "export_version": cat.version,
                   "found": found, "unknown": unknown},
                  open(a.json, "w"), ensure_ascii=False, indent=1)
        print(f"  rapport détaillé : {a.json}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
