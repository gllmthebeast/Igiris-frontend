#pragma once

// Scanner de ROMs — CLAUDE.md §4 et §10.
//
// Il ne fait QUE des lookups dans l'export : aucun rapprochement de titres, aucun fuzzy,
// aucune normalisation. Tout cela est précalculé côté serveur.
//
// Ses règles ne sont pas codées en dur : les plateformes d'arcade et les tailles d'en-tête
// sont LUES DANS L'EXPORT. Si le backend ajoute une plateforme, le scanner suit sans
// modification.

#include "catalog/ExportDatabase.h"
#include "scan/ScanCache.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace igiris::scan {

// Un dossier à parcourir, et la clé de plateforme sous laquelle l'interroger.
// La correspondance dossier → platformKey est le travail de l'adaptateur, pas du scanner.
struct ScanTarget {
    QString platformKey;
    QString directory;
};

// Comment la ROM a été reconnue. Utile en diagnostic : « identifiée seulement après avoir
// ignoré un en-tête » est une information, pas un détail.
enum class MatchKind {
    Crc,           // CRC direct
    CrcHeaderSkip, // CRC après avoir ignoré l'en-tête déclaré par l'export
    CrcSmcHeuristic, // CRC après les 512 octets du SMC — heuristique locale (§4)
    ZipEntryCrc,   // CRC lu dans l'annuaire du zip, sans décompression
    Romset,        // arcade, par nom de romset
    FileName,      // DOS / Windows 3.x, par nom de fichier (export 1.9.0)
};

struct IdentifiedRom {
    QString   path;
    QString   platformKey;
    QString   gameKey;
    QString   title;
    QString   crc32;  // vide pour l'arcade et pour l'identification par nom
    QString   romset; // renseigné pour l'arcade seulement
    QString   fileKey;    // renseigné pour l'identification par nom seulement
    QString   collection; // « eXoDOS », « eXoWin3x » — d'où vient l'entrée
    MatchKind kind = MatchKind::Crc;
};

struct ScanReport {
    QList<IdentifiedRom> identified;
    QStringList          unidentified; // fichiers vus mais inconnus de l'export
    QStringList          errors;       // messages verbatim (§15)

    int filesSeen = 0;
    int cacheHits = 0;
    int hashed    = 0;

    // Jeux distincts possédés — la base des statuts vert/rouge du §7.
    QStringList ownedGameKeys() const;
};

class RomScanner
{
public:
    // `cache` peut être nul : le scan fonctionne alors, mais sans incrémental.
    RomScanner(const catalog::ExportDatabase &db, ScanCache *cache = nullptr);

    ScanReport scan(const QList<ScanTarget> &targets);

private:
    void scanDirectory(const ScanTarget &target, ScanReport &report);
    void identifyFile(const QString &path, const ScanTarget &target, ScanReport &report);
    void identifyArchive(const QString &path, const ScanTarget &target, ScanReport &report);
    void identifyArcade(const QString &path, const ScanTarget &target, ScanReport &report);

    // Troisième voie, export 1.9.0. Retourne vrai si le fichier a été identifié — c'est ce
    // qui permet de l'ESSAYER APRÈS le CRC plutôt qu'à sa place.
    bool identifyByFileName(const QString &path, const ScanTarget &target,
                            ScanReport &report);

    QString cachedOrComputedCrc(const QString &path, ScanReport &report);

    const catalog::ExportDatabase &m_db;
    ScanCache                     *m_cache = nullptr;
    // Deux listes DISTINCTES, et c'est le point de conception de cette version.
    //
    // m_arcadeKeys sert aussi, ailleurs, à décider si un jeu est « arcade » en vue liste :
    // y verser les plateformes DOS aurait fait passer le filtre « Type : arcade » de 833 à
    // 1 536 jeux, sans erreur ni message. m_fileNameKeys ne sort jamais du scanner.
    //
    // Une plateforme peut figurer dans les deux : `dos` relève des DEUX voies — 678 jeux
    // par CRC, ~700 de plus par nom.
    QStringList                    m_arcadeKeys;
    QStringList                    m_fileNameKeys;
    QHash<QString, int>            m_headerSkips;
};

} // namespace igiris::scan
