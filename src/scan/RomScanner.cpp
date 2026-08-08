#include "scan/RomScanner.h"

#include "scan/RomHasher.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

namespace igiris::scan {

namespace {

// Ce qui n'est pas un jeu. Sans cette liste, les jaquettes, sauvegardes et listes de
// lecture seraient comptées comme « non reconnues » et noieraient le vrai signal.
const QSet<QString> kIgnoredExtensions = {
    ".txt", ".xml", ".jpg", ".jpeg", ".png", ".cfg", ".dat", ".md5", ".sha1", ".nfo",
    ".srm", ".state", ".sav", ".cue", ".m3u", ".auto", ".db", ".json", ".bak", ".log",
};

const QSet<QString> kIgnoredDirectories = {
    "media", "images", "videos", "manuals", "downloaded_images", "downloaded_videos", ".git",
};

bool isIgnoredPath(const QFileInfo &info, const QString &root)
{
    const QString suffix = QLatin1Char('.') + info.suffix().toLower();
    if (kIgnoredExtensions.contains(suffix))
        return true;

    // Un dossier ignoré n'importe où sous la racine du système écarte tout son contenu.
    const QString relative = info.absoluteFilePath().mid(root.size());
    for (const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (kIgnoredDirectories.contains(part.toLower()))
            return true;
    }
    return false;
}

} // namespace

QStringList ScanReport::ownedGameKeys() const
{
    QSet<QString> keys;
    for (const IdentifiedRom &rom : identified)
        keys.insert(rom.gameKey);
    QStringList out(keys.cbegin(), keys.cend());
    out.sort();
    return out;
}

RomScanner::RomScanner(const catalog::ExportDatabase &db, ScanCache *cache)
    : m_db(db)
    , m_cache(cache)
    , m_arcadeKeys(db.arcadePlatformKeys())
    , m_headerSkips(db.headerSkipByPlatform())
{
}

ScanReport RomScanner::scan(const QList<ScanTarget> &targets)
{
    ScanReport report;
    for (const ScanTarget &target : targets)
        scanDirectory(target, report);
    return report;
}

void RomScanner::scanDirectory(const ScanTarget &target, ScanReport &report)
{
    const QFileInfo rootInfo(target.directory);
    if (!rootInfo.isDir()) {
        report.errors.append(
            QStringLiteral("dossier absent : %1").arg(target.directory));
        return;
    }

    const QString root = rootInfo.absoluteFilePath();
    QDirIterator  it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (isIgnoredPath(info, root))
            continue;

        ++report.filesSeen;

        // L'arcade s'identifie par NOM, jamais par CRC : le hash d'un .zip d'arcade change
        // dès qu'on reconstruit le romset (merged / split / non-merged), ce que les
        // utilisateurs de MAME font couramment (§4).
        if (m_arcadeKeys.contains(target.platformKey)) {
            identifyArcade(info.absoluteFilePath(), target, report);
            continue;
        }

        if (info.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0)
            identifyArchive(info.absoluteFilePath(), target, report);
        else
            identifyFile(info.absoluteFilePath(), target, report);
    }
}

void RomScanner::identifyArcade(const QString &path, const ScanTarget &target,
                                ScanReport &report)
{
    // Nom de fichier sans extension, en minuscules — la forme de exp_romset.
    const QString romset = QFileInfo(path).completeBaseName().toLower();

    const auto match = m_db.findByRomset(romset, target.platformKey);
    if (!match) {
        report.unidentified.append(path);
        return;
    }

    IdentifiedRom rom;
    rom.path        = path;
    rom.platformKey = target.platformKey;
    rom.gameKey     = match->gameKey;
    rom.title       = match->title;
    rom.romset      = romset;
    rom.kind        = MatchKind::Romset;
    report.identified.append(rom);
}

QString RomScanner::cachedOrComputedCrc(const QString &path, ScanReport &report)
{
    const QFileInfo info(path);
    const qint64    size  = info.size();
    const qint64    mtime = info.lastModified().toSecsSinceEpoch();

    if (m_cache) {
        if (const auto cached = m_cache->lookup(path, size, mtime)) {
            ++report.cacheHits;
            return *cached;
        }
    }

    const HashResult result = crc32OfFile(path);
    if (!result.ok) {
        report.errors.append(result.error);
        return {};
    }

    ++report.hashed;
    if (m_cache)
        m_cache->store(path, size, mtime, result.crc32);
    return result.crc32;
}

void RomScanner::identifyFile(const QString &path, const ScanTarget &target,
                              ScanReport &report)
{
    const QString crc = cachedOrComputedCrc(path, report);
    if (crc.isEmpty())
        return;

    IdentifiedRom rom;
    rom.path        = path;
    rom.platformKey = target.platformKey;

    if (const auto match = m_db.findByCrc(crc, target.platformKey)) {
        rom.gameKey = match->gameKey;
        rom.title   = match->title;
        rom.crc32   = crc;
        rom.kind    = MatchKind::Crc;
        report.identified.append(rom);
        return;
    }

    // Échec du CRC direct : le fichier porte peut-être un en-tête que le dat n'a pas.
    // On ne devine pas la taille de cet en-tête, c'est l'export qui la donne.
    const int skip = m_headerSkips.value(target.platformKey, 0);
    if (skip > 0) {
        const HashResult retry = crc32OfFile(path, skip);
        if (retry.ok) {
            if (const auto match = m_db.findByCrc(retry.crc32, target.platformKey)) {
                rom.gameKey = match->gameKey;
                rom.title   = match->title;
                rom.crc32   = retry.crc32;
                rom.kind    = MatchKind::CrcHeaderSkip;
                report.identified.append(rom);
                return;
            }
        }
    }

    // Dernier recours : l'en-tête de copieur SMC de 512 octets, que l'export NE COUVRE PAS
    // (§4). Sans cette tentative, des ROMs SNES parfaitement valides tomberaient en rouge.
    if (looksLikeSmcHeader(static_cast<quint64>(QFileInfo(path).size()))) {
        const HashResult retry = crc32OfFile(path, 512);
        if (retry.ok) {
            if (const auto match = m_db.findByCrc(retry.crc32, target.platformKey)) {
                rom.gameKey = match->gameKey;
                rom.title   = match->title;
                rom.crc32   = retry.crc32;
                rom.kind    = MatchKind::CrcSmcHeuristic;
                report.identified.append(rom);
                return;
            }
        }
    }

    report.unidentified.append(path);
}

void RomScanner::identifyArchive(const QString &path, const ScanTarget &target,
                                 ScanReport &report)
{
    QString          error;
    const auto       entries = readZipEntries(path, &error);
    if (entries.isEmpty()) {
        if (!error.isEmpty())
            report.errors.append(error);
        else
            report.unidentified.append(path);
        return;
    }

    const int skip = m_headerSkips.value(target.platformKey, 0);

    for (const ZipEntry &entry : entries) {
        const QString suffix = QLatin1Char('.') + QFileInfo(entry.name).suffix().toLower();
        if (kIgnoredExtensions.contains(suffix))
            continue;

        // Chemin rapide : le CRC du contenu décompressé est DÉJÀ dans l'annuaire du zip.
        // Aucune décompression, ce qui compte sur un Pi.
        const HashResult stored = crc32OfZipEntry(path, entry, 0);
        if (stored.ok) {
            if (const auto match = m_db.findByCrc(stored.crc32, target.platformKey)) {
                IdentifiedRom rom;
                rom.path        = path;
                rom.platformKey = target.platformKey;
                rom.gameKey     = match->gameKey;
                rom.title       = match->title;
                rom.crc32       = stored.crc32;
                rom.kind        = MatchKind::ZipEntryCrc;
                report.identified.append(rom);
                return;
            }
        }

        // Le CRC stocké porte sur le contenu ENTIER, en-tête compris. Pour l'ignorer il
        // faut décompresser — on ne le fait qu'ici, en dernier recours.
        const qint64 retrySkip =
            skip > 0 ? skip : (looksLikeSmcHeader(entry.uncompressedSize) ? 512 : 0);
        if (retrySkip > 0) {
            const HashResult retry = crc32OfZipEntry(path, entry, retrySkip);
            if (!retry.ok) {
                report.errors.append(retry.error);
                continue;
            }
            if (const auto match = m_db.findByCrc(retry.crc32, target.platformKey)) {
                IdentifiedRom rom;
                rom.path        = path;
                rom.platformKey = target.platformKey;
                rom.gameKey     = match->gameKey;
                rom.title       = match->title;
                rom.crc32       = retry.crc32;
                rom.kind = skip > 0 ? MatchKind::CrcHeaderSkip : MatchKind::CrcSmcHeuristic;
                report.identified.append(rom);
                return;
            }
        }
    }

    report.unidentified.append(path);
}

} // namespace igiris::scan
