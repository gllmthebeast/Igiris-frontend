// Tests du lot 4 — scanner de ROMs.
//
// Le §4 prévient : les erreurs de ce lot sont SILENCIEUSES. Rien ne plante, tout tombe en
// rouge à tort. D'où des tests qui vérifient non seulement qu'une ROM est reconnue, mais
// PAR QUEL CHEMIN elle l'a été — un fichier identifié par le mauvais chemin serait un bug
// invisible.

#include "scan/RomHasher.h"
#include "scan/RomScanner.h"
#include "scan/ScanCache.h"

#include "zip_fixture.h"

#include <sqlite3.h>

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace igiris::scan;
using igiris::catalog::ExportDatabase;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();
}

QString writeZipFixture(const QString &path)
{
    writeFile(path, QByteArray(reinterpret_cast<const char *>(igiris::test::kZipFixture),
                               sizeof(igiris::test::kZipFixture)));
    return path;
}

// Construit un export dont les CRC sont ceux des fichiers RÉELLEMENT créés par le test.
// Figer des CRC en dur ici rendrait les tests faux au premier changement de contenu.
bool buildExport(const QString &path, const QList<QPair<QString, QString>> &crcAndPlatform,
                 int nesHeaderSkip = 16)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    QString sql = QStringLiteral(
        "CREATE TABLE exp_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "INSERT INTO exp_meta VALUES('schema_version','1.3.0');"
        "CREATE TABLE exp_game(game_key TEXT PRIMARY KEY, title TEXT NOT NULL,"
        " search_key TEXT NOT NULL, year INTEGER, cover_ref TEXT, rating INTEGER);"
        "CREATE TABLE exp_game_platform(game_key TEXT, batocera_system TEXT,"
        " display_name TEXT NOT NULL, emu_score INTEGER, is_preferred INTEGER DEFAULT 0,"
        " PRIMARY KEY(game_key, display_name));"
        "CREATE TABLE exp_rom_hash(crc32 TEXT NOT NULL, batocera_system TEXT NOT NULL,"
        " game_key TEXT NOT NULL, header_skip INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(crc32, batocera_system));"
        "CREATE TABLE exp_romset(romset TEXT NOT NULL, batocera_system TEXT NOT NULL,"
        " game_key TEXT NOT NULL, emulators TEXT, hardware TEXT, driver_status TEXT,"
        " PRIMARY KEY(romset, batocera_system));"
        "INSERT INTO exp_game VALUES('igdb-arcade','Street Fighter II CE','sf2',1992,NULL,90);"
        "INSERT INTO exp_romset VALUES('sf2ce','arcade','igdb-arcade','mame','cps1','good');");

    for (int i = 0; i < crcAndPlatform.size(); ++i) {
        const QString key      = QStringLiteral("igdb-%1").arg(i);
        const QString crc      = crcAndPlatform.at(i).first;
        const QString platform = crcAndPlatform.at(i).second;
        sql += QStringLiteral("INSERT INTO exp_game VALUES('%1','Jeu %2','jeu %2',1990,NULL,80);")
                   .arg(key)
                   .arg(i);
        sql += QStringLiteral("INSERT INTO exp_rom_hash VALUES('%1','%2','%3',%4);")
                   .arg(crc, platform, key)
                   .arg(platform == QLatin1String("nes") ? nesHeaderSkip : 0);
    }

    char      *errmsg = nullptr;
    const bool ok = sqlite3_exec(db, sql.toUtf8().constData(), nullptr, nullptr, &errmsg)
                    == SQLITE_OK;
    if (errmsg)
        sqlite3_free(errmsg);
    sqlite3_close(db);
    return ok;
}

} // namespace

class TestRomScanner : public QObject
{
    Q_OBJECT

private slots:
    void crc32OfFile_computesAndSkips();
    void crc32OfFile_refusesSkipBeyondEnd();
    void smcHeuristic_recognisesOnly512Overhang();

    void zip_readsEntriesWithoutDecompressing();
    void zip_storedCrcMatchesContent();
    void zip_inflatesWhenHeaderMustBeSkipped();
    void zip_handlesStoredEntries();

    void cache_hitsOnlyWhenSizeAndMtimeMatch();

    void scan_identifiesPlainRom();
    void scan_identifiesViaHeaderSkip();
    void scan_identifiesViaSmcHeuristic();
    void scan_identifiesArcadeByName();
    void scan_identifiesInsideZipWithoutDecompressing();
    void scan_ignoresNonGameFiles();
    void scan_reportsUnidentified();
    void scan_secondPassUsesCache();
};

// ------------------------------------------------------------------------- RomHasher

void TestRomScanner::crc32OfFile_computesAndSkips()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("rom.bin");
    QByteArray    header(16, '\0');
    for (int i = 0; i < 16; ++i)
        header[i] = static_cast<char>(i);
    writeFile(path, header + QByteArray("NESBODY").repeated(50));

    QCOMPARE(crc32OfFile(path).crc32, QString(igiris::test::kCrcHeaderedWhole));
    QCOMPARE(crc32OfFile(path, 16).crc32, QString(igiris::test::kCrcHeaderedBody));
}

void TestRomScanner::crc32OfFile_refusesSkipBeyondEnd()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("court.bin");
    writeFile(path, QByteArray("ab"));

    const auto result = crc32OfFile(path, 512);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty()); // §15 : jamais d'échec muet
}

void TestRomScanner::smcHeuristic_recognisesOnly512Overhang()
{
    QVERIFY(looksLikeSmcHeader(1024 + 512));
    QVERIFY(looksLikeSmcHeader(2 * 1024 * 1024 + 512));
    QVERIFY(!looksLikeSmcHeader(1024));       // ROM propre
    QVERIFY(!looksLikeSmcHeader(512));        // trop court pour porter un corps
    QVERIFY(!looksLikeSmcHeader(1024 + 256)); // dépassement, mais pas de 512
}

void TestRomScanner::zip_readsEntriesWithoutDecompressing()
{
    QTemporaryDir dir;
    const QString path = writeZipFixture(dir.filePath("a.zip"));

    QString    error;
    const auto entries = readZipEntries(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).name, QStringLiteral("jeu.sfc"));
    QCOMPARE(entries.at(1).name, QStringLiteral("avec_header.nes"));
}

void TestRomScanner::zip_storedCrcMatchesContent()
{
    QTemporaryDir dir;
    const QString path    = writeZipFixture(dir.filePath("a.zip"));
    const auto    entries = readZipEntries(path, nullptr);

    // Le CRC sort de l'annuaire du zip : aucune décompression n'a eu lieu.
    QCOMPARE(crc32OfZipEntry(path, entries.at(0), 0).crc32, QString(igiris::test::kCrcPlain));
    QCOMPARE(crc32OfZipEntry(path, entries.at(1), 0).crc32,
             QString(igiris::test::kCrcHeaderedWhole));
}

void TestRomScanner::zip_inflatesWhenHeaderMustBeSkipped()
{
    QTemporaryDir dir;
    const QString path    = writeZipFixture(dir.filePath("a.zip"));
    const auto    entries = readZipEntries(path, nullptr);

    // Ignorer un en-tête impose de décompresser : le CRC stocké porte sur le contenu ENTIER.
    const auto result = crc32OfZipEntry(path, entries.at(1), 16);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.crc32, QString(igiris::test::kCrcHeaderedBody));
}

void TestRomScanner::zip_handlesStoredEntries()
{
    QTemporaryDir dir;
    const QString path    = writeZipFixture(dir.filePath("a.zip"));
    const auto    entries = readZipEntries(path, nullptr);

    QCOMPARE(entries.at(2).method, quint16(0)); // stockée, non compressée
    const auto result = crc32OfZipEntry(path, entries.at(2), 7);
    QVERIFY2(result.ok, qPrintable(result.error));
}

// ------------------------------------------------------------------------- ScanCache

void TestRomScanner::cache_hitsOnlyWhenSizeAndMtimeMatch()
{
    QTemporaryDir dir;
    ScanCache     cache;
    QString       error;
    QVERIFY2(cache.open(dir.filePath("cache.db"), &error), qPrintable(error));

    cache.store(QStringLiteral("/roms/a.sfc"), 100, 42, QStringLiteral("AABBCCDD"));
    QCOMPARE(cache.lookup(QStringLiteral("/roms/a.sfc"), 100, 42).value_or(QString()),
             QStringLiteral("AABBCCDD"));

    // Un fichier réécrit change de date ou de taille : le cache doit se taire, pas mentir.
    QVERIFY(!cache.lookup(QStringLiteral("/roms/a.sfc"), 100, 43).has_value());
    QVERIFY(!cache.lookup(QStringLiteral("/roms/a.sfc"), 101, 42).has_value());
}

// ------------------------------------------------------------------------ RomScanner

void TestRomScanner::scan_identifiesPlainRom()
{
    QTemporaryDir dir;
    const QString rom = dir.filePath("roms/snes/jeu.sfc");
    writeFile(rom, QByteArray("ROMDATA").repeated(100));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, { { crc32OfFile(rom).crc32, QStringLiteral("snes") } }));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("snes"), dir.filePath("roms/snes") } });

    QCOMPARE(report.identified.size(), 1);
    QCOMPARE(report.identified.first().kind, MatchKind::Crc);
    QCOMPARE(report.ownedGameKeys().size(), 1);
}

void TestRomScanner::scan_identifiesViaHeaderSkip()
{
    QTemporaryDir dir;
    const QString rom = dir.filePath("roms/nes/jeu.nes");
    writeFile(rom, QByteArray(16, '\0') + QByteArray("NESBODY").repeated(50));

    // L'export ne connaît que le CRC SANS en-tête, comme les dats No-Intro.
    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, { { crc32OfFile(rom, 16).crc32, QStringLiteral("nes") } }));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("nes"), dir.filePath("roms/nes") } });

    QCOMPARE(report.identified.size(), 1);
    QCOMPARE(report.identified.first().kind, MatchKind::CrcHeaderSkip);
}

void TestRomScanner::scan_identifiesViaSmcHeuristic()
{
    QTemporaryDir dir;
    const QString rom = dir.filePath("roms/snes/copieur.sfc");
    // 512 octets d'en-tête de copieur + 1 Ko de corps. L'export ne couvre PAS ce cas :
    // sans l'heuristique locale, cette ROM valide tomberait en rouge (§4).
    writeFile(rom, QByteArray(512, 'H') + QByteArray(1024, 'B'));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, { { crc32OfFile(rom, 512).crc32, QStringLiteral("snes") } }));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("snes"), dir.filePath("roms/snes") } });

    QCOMPARE(report.identified.size(), 1);
    QCOMPARE(report.identified.first().kind, MatchKind::CrcSmcHeuristic);
}

void TestRomScanner::scan_identifiesArcadeByName()
{
    QTemporaryDir dir;
    // Contenu volontairement quelconque : l'arcade ne se hashe JAMAIS. Le CRC d'un .zip
    // d'arcade change à chaque reconstruction du romset (§4).
    writeFile(dir.filePath("roms/arcade/SF2CE.zip"), QByteArray("peu importe"));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, {}));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("arcade"), dir.filePath("roms/arcade") } });

    QCOMPARE(report.identified.size(), 1);
    QCOMPARE(report.identified.first().kind, MatchKind::Romset);
    QCOMPARE(report.identified.first().romset, QStringLiteral("sf2ce")); // normalisé
}

void TestRomScanner::scan_identifiesInsideZipWithoutDecompressing()
{
    QTemporaryDir dir;
    writeZipFixture(dir.filePath("roms/snes/jeu.zip"));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath,
                        { { QString(igiris::test::kCrcPlain), QStringLiteral("snes") } }));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("snes"), dir.filePath("roms/snes") } });

    QCOMPARE(report.identified.size(), 1);
    QCOMPARE(report.identified.first().kind, MatchKind::ZipEntryCrc);
}

void TestRomScanner::scan_ignoresNonGameFiles()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("roms/snes/jaquette.png"), QByteArray("png"));
    writeFile(dir.filePath("roms/snes/gamelist.xml"), QByteArray("<x/>"));
    writeFile(dir.filePath("roms/snes/media/video.mp4"), QByteArray("mp4"));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, {}));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("snes"), dir.filePath("roms/snes") } });

    // Rien vu, donc rien de « non reconnu » : sinon les jaquettes noieraient le vrai signal.
    QCOMPARE(report.filesSeen, 0);
    QVERIFY(report.unidentified.isEmpty());
}

void TestRomScanner::scan_reportsUnidentified()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("roms/snes/inconnu.sfc"), QByteArray("contenu inconnu"));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, {}));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("snes"), dir.filePath("roms/snes") } });

    QCOMPARE(report.filesSeen, 1);
    QCOMPARE(report.unidentified.size(), 1);
    QVERIFY(report.identified.isEmpty());
}

void TestRomScanner::scan_secondPassUsesCache()
{
    QTemporaryDir dir;
    const QString rom = dir.filePath("roms/snes/jeu.sfc");
    writeFile(rom, QByteArray("ROMDATA").repeated(100));

    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, { { crc32OfFile(rom).crc32, QStringLiteral("snes") } }));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    ScanCache cache;
    QVERIFY(cache.open(dir.filePath("cache.db"), nullptr));

    RomScanner              scanner(db, &cache);
    const QList<ScanTarget> targets = { { QStringLiteral("snes"), dir.filePath("roms/snes") } };

    const auto first = scanner.scan(targets);
    QCOMPARE(first.hashed, 1);
    QCOMPARE(first.cacheHits, 0);

    // §4 : « pas de rehash complet à chaque démarrage ».
    const auto second = scanner.scan(targets);
    QCOMPARE(second.hashed, 0);
    QCOMPARE(second.cacheHits, 1);
    QCOMPARE(second.identified.size(), 1);
}

QTEST_GUILESS_MAIN(TestRomScanner)
#include "test_rom_scanner.moc"
