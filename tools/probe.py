#!/usr/bin/env python3
"""Preuve de bout en bout : le contrat de l'export est-il respecté ?

À lancer en premier, et après chaque mise à jour de l'export. Ce script fait exactement
ce que l'appareil fera — ouverture immuable, contrôle de version, puis les quatre requêtes
types — et échoue bruyamment si quelque chose cloche.

Usage : python3 tools/probe.py [chemin/games.db]
"""
import os
import sqlite3
import sys

# Version MAJEURE que ce projet sait lire. Une majeure inconnue = refus, jamais
# d'interprétation au hasard d'un schéma qu'on ne connaît pas.
SUPPORTED_MAJOR = 1


def fail(msg):
    print(f"✗ {msg}", file=sys.stderr)
    sys.exit(1)


def main(path):
    if not os.path.exists(path):
        fail(f"export introuvable : {path}\n  → lancer d'abord tools/fetch-export.sh")

    con = sqlite3.connect(f"file:{path}?immutable=1", uri=True)
    meta = dict(con.execute("SELECT key, value FROM exp_meta").fetchall())

    version = meta.get("schema_version", "0.0.0")
    major = int(version.split(".")[0])
    if major != SUPPORTED_MAJOR:
        fail(f"schéma {version} incompatible (majeure {SUPPORTED_MAJOR} attendue)")

    mode = con.execute("PRAGMA journal_mode").fetchone()[0]
    if mode.lower() not in ("delete", "off"):
        fail(f"journal_mode={mode} — l'export ne doit PAS être en WAL")

    print(f"✓ schéma {version} · généré {meta.get('generated_at')} · journal {mode}")
    print(f"  {meta.get('games')} jeux · {meta.get('rom_hashes')} hashes · "
          f"{meta.get('arcade_romsets', '?')} romsets arcade")

    # 1. Identification d'un fichier de console par CRC
    row = con.execute("""
        SELECT g.title, h.batocera_system, h.header_skip
        FROM exp_rom_hash h JOIN exp_game g ON g.game_key = h.game_key LIMIT 1
    """).fetchone()
    print(f"\n  [CRC]     {row[1]:12s} → {row[0][:46]}  (header_skip={row[2]})")

    # 2. Identification d'un jeu d'arcade par nom de romset
    for rs in ("mslug3", "pacman", "dkong"):
        r = con.execute("""
            SELECT g.title, r.hardware, r.emulators, r.driver_status
            FROM exp_romset r JOIN exp_game g ON g.game_key = r.game_key WHERE r.romset = ?
        """, (rs,)).fetchone()
        if r:
            print(f"  [ROMSET]  {rs+'.zip':12s} → {r[0][:34]:34s} "
                  f"matériel={r[1]} emu={r[2]} statut={r[3]}")

    # 3. Recherche par nom
    print("\n  [SEARCH]  « sonic » :")
    for t, y, n in con.execute("""
        SELECT title, year, rating FROM exp_game
        WHERE search_key LIKE '%sonic%' ORDER BY rating DESC LIMIT 3
    """):
        print(f"              {t[:44]:44s} {y} note={n}")

    # 4. Meilleure version d'un jeu
    gk = con.execute("SELECT game_key FROM exp_game WHERE title LIKE 'Sonic the Hedgehog (%'").fetchone()
    if gk:
        print("\n  [VERSIONS] Sonic the Hedgehog :")
        for d, s, e, p in con.execute("""
            SELECT display_name, batocera_system, emu_score, is_preferred
            FROM exp_game_platform WHERE game_key = ?
            ORDER BY is_preferred DESC, emu_score DESC LIMIT 4
        """, (gk[0],)):
            print(f"              {d[:16]:16s} {s:14s} fidélité={e:<4} "
                  f"{'← élue par les votes' if p else ''}")

    print("\n✓ Contrat respecté.")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "data", "games.db"))
