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

    // --- export 1.9.0 : la troisième voie, par nom de fichier ---
    void scan_identifiesByFileNameWhenNoCrcExists();
    void scan_crcWinsOverFileName();
    void scan_fileNameIgnoredOnPlatformsThatDoNotDeclareIt();
};

namespace {

// Greffe la table du 1.9.0 sur un export déjà construit. `crcToo` ajoute EN PLUS un hash
// pour le même fichier — c'est ce qui met la règle de précédence à l'épreuve.
bool addGameFiles(const QString &path, const QString &crcToo = QString())
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    QString sql = QStringLiteral(
        "CREATE TABLE exp_game_file(file_key TEXT NOT NULL, batocera_system TEXT NOT NULL,"
        " game_key TEXT NOT NULL, collection TEXT,"
        " PRIMARY KEY(file_key, batocera_system)) WITHOUT ROWID;"
        "INSERT INTO exp_game VALUES('igdb-dos','Gabriel Knight 2 (1995)',"
        " 'gabriel knight 2',1995,NULL,85);"
        // La clé porte l'année, comme en production : elle désambiguïse les rééditions, et
        // c'est ce qu'on a demandé au backend de conserver.
        "INSERT INTO exp_game_file VALUES('gabriel knight 2 (1995)','dos','igdb-dos',"
        " 'eXoDOS');");

    if (!crcToo.isEmpty()) {
        // MÊME jeu, MÊME plateforme, identifiable des deux façons. C'est le cas réel de
        // `dos` : 678 jeux par CRC, des centaines d'autres par nom seulement.
        sql += QStringLiteral("INSERT INTO exp_game VALUES('igdb-dos-crc','Le jeu du CRC',"
                              "'crc',1990,NULL,70);"
                              "INSERT INTO exp_rom_hash VALUES('%1','dos','igdb-dos-crc',0);")
                   .arg(crcToo);
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

void TestRomScanner::scan_identifiesByFileNameWhenNoCrcExists()
{
    QTemporaryDir dir;
    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, {}));
    QVERIFY(addGameFiles(exportPath));

    // Le contenu du fichier n'a AUCUNE importance ici, et c'est tout le sujet : eXoDOS est
    // distribué par torrent, et un torrent ne hache pas les fichiers. Il n'existe aucun
    // CRC à retrouver — seul le nom identifie.
    QDir(dir.path()).mkpath(QStringLiteral("roms/dos"));
    QFile rom(dir.filePath("roms/dos/Gabriel Knight 2 (1995).zip"));
    QVERIFY(rom.open(QIODevice::WriteOnly));
    rom.write("ceci n'est pas un vrai zip, et ça n'a aucune importance");
    rom.close();

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);

    const QList<ScanTarget> targets = { { QStringLiteral("dos"),
                                          dir.filePath("roms/dos") } };
    const auto              report  = scanner.scan(targets);

    QCOMPARE(report.identified.size(), 1);
    const auto &rom0 = report.identified.first();
    QCOMPARE(rom0.gameKey, QStringLiteral("igdb-dos"));
    QCOMPARE(rom0.kind, MatchKind::FileName);
    // La casse du fichier ne compte pas, l'année si : la clé est le nom tel qu'il est
    // distribué, en minuscules, sans sa dernière extension.
    QCOMPARE(rom0.fileKey, QStringLiteral("gabriel knight 2 (1995)"));
    QCOMPARE(rom0.collection, QStringLiteral("eXoDOS"));
    // Aucun CRC : rien n'a été haché, et c'est normal.
    QVERIFY(rom0.crc32.isEmpty());
}

void TestRomScanner::scan_crcWinsOverFileName()
{
    QTemporaryDir dir;
    const QString exportPath = dir.filePath("games.db");

    // Un vrai fichier, avec un vrai CRC, sur une plateforme qui relève des DEUX voies.
    QDir(dir.path()).mkpath(QStringLiteral("roms/dos"));
    const QString romPath = dir.filePath("roms/dos/Gabriel Knight 2 (1995).bin");
    QFile         rom(romPath);
    QVERIFY(rom.open(QIODevice::WriteOnly));
    rom.write("contenu identifiable par son empreinte");
    rom.close();

    const auto hashed = igiris::scan::crc32OfFile(romPath, 0);
    QVERIFY(hashed.ok);

    QVERIFY(buildExport(exportPath, {}));
    // Le MÊME fichier est déclaré des deux façons, vers DEUX jeux différents. C'est
    // artificiel, et c'est exactement ce qu'il faut pour que le test tranche.
    QVERIFY(addGameFiles(exportPath, hashed.crc32));

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    RomScanner scanner(db);

    const auto report = scanner.scan({ { QStringLiteral("dos"),
                                         dir.filePath("roms/dos") } });

    QCOMPARE(report.identified.size(), 1);
    const auto &found = report.identified.first();

    // LE test de cette version. Le CRC identifie un CONTENU, le nom identifie un contenant
    // que n'importe qui peut renommer : quand les deux répondent, c'est le nom qui a tort.
    // C'est la précédence défendue au backend, et sans ce test elle peut s'inverser un jour
    // sans que rien ne le signale.
    QCOMPARE(found.gameKey, QStringLiteral("igdb-dos-crc"));
    QCOMPARE(found.kind, MatchKind::Crc);
    QCOMPARE(found.crc32, hashed.crc32);
    QVERIFY(found.fileKey.isEmpty());
}

void TestRomScanner::scan_fileNameIgnoredOnPlatformsThatDoNotDeclareIt()
{
    QTemporaryDir dir;
    const QString exportPath = dir.filePath("games.db");
    QVERIFY(buildExport(exportPath, {}));
    QVERIFY(addGameFiles(exportPath));

    // Le même nom de fichier, mais rangé sous « snes » — que exp_game_file ne déclare pas.
    // Il ne doit RIEN identifier : la voie par nom est réservée aux plateformes que
    // l'export désigne, jamais appliquée partout « au cas où ».
    QDir(dir.path()).mkpath(QStringLiteral("roms/snes"));
    QFile rom(dir.filePath("roms/snes/Gabriel Knight 2 (1995).sfc"));
    QVERIFY(rom.open(QIODevice::WriteOnly));
    rom.write("peu importe");
    rom.close();

    ExportDatabase db;
    QVERIFY(db.open(exportPath, nullptr));
    QCOMPARE(db.fileNamePlatformKeys(), QStringList({ QStringLiteral("dos") }));

    RomScanner scanner(db);
    const auto report = scanner.scan({ { QStringLiteral("snes"),
                                         dir.filePath("roms/snes") } });

    QVERIFY(report.identified.isEmpty());
    QCOMPARE(report.unidentified.size(), 1);
}

QTEST_GUILESS_MAIN(TestRomScanner)
#include "test_rom_scanner.moc"
