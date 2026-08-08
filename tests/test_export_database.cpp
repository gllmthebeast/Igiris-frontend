// Tests du lot 3 — chargeur de l'export.
//
// L'essentiel tourne sur un export SYNTHÉTIQUE construit ici : les tests ne doivent pas
// dépendre de data/games.db, qui est un artefact téléchargé et absent d'un clone neuf.
// Un dernier test s'exécute EN PLUS sur le vrai export quand il est présent.

#include "catalog/ExportDatabase.h"

#include <sqlite3.h>

#include <QTemporaryDir>
#include <QTest>

using namespace igiris::catalog;

namespace {

// Construit un export minimal au schéma 1.3.0.
bool buildExport(const QString &path, const QString &schemaVersion)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    const QString sql = QStringLiteral(R"(
        CREATE TABLE exp_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
        INSERT INTO exp_meta VALUES('schema_version','%1'),('generated_at','2026-08-08T00:00:00Z'),
                                   ('games','2'),('platforms','3'),('rom_hashes','2'),
                                   ('arcade_romsets','1'),('dat_sets','9');

        CREATE TABLE exp_game(game_key TEXT PRIMARY KEY, title TEXT NOT NULL,
                              search_key TEXT NOT NULL, year INTEGER, cover_ref TEXT,
                              rating INTEGER);
        INSERT INTO exp_game VALUES('igdb-1','Super Mario World','super mario world',1990,NULL,96),
                                   ('igdb-2','Street Fighter II','street fighter ii',1991,NULL,88);

        CREATE TABLE exp_game_platform(game_key TEXT NOT NULL, batocera_system TEXT,
                                       display_name TEXT NOT NULL, emu_score INTEGER,
                                       is_preferred INTEGER NOT NULL DEFAULT 0,
                                       PRIMARY KEY(game_key, display_name));
        INSERT INTO exp_game_platform VALUES
            ('igdb-1','snes','Super Nintendo',95,1),
            ('igdb-1',NULL,'Game Boy Advance (non émulé ici)',0,0),
            ('igdb-2','arcade','Arcade',80,1);

        CREATE TABLE exp_rom_hash(crc32 TEXT NOT NULL, batocera_system TEXT NOT NULL,
                                  game_key TEXT NOT NULL, header_skip INTEGER NOT NULL DEFAULT 0,
                                  PRIMARY KEY(crc32, batocera_system));
        INSERT INTO exp_rom_hash VALUES('B19ED489','snes','igdb-1',0),
                                       ('DEADBEEF','nes','igdb-1',16);

        CREATE TABLE exp_romset(romset TEXT NOT NULL, batocera_system TEXT NOT NULL,
                                game_key TEXT NOT NULL, emulators TEXT, hardware TEXT,
                                driver_status TEXT, PRIMARY KEY(romset, batocera_system));
        INSERT INTO exp_romset VALUES('sf2ce','arcade','igdb-2','fbneo,mame','cps1','good');
    )").arg(schemaVersion);

    char      *errmsg = nullptr;
    const bool ok = sqlite3_exec(db, sql.toUtf8().constData(), nullptr, nullptr, &errmsg)
                    == SQLITE_OK;
    if (errmsg)
        sqlite3_free(errmsg);
    sqlite3_close(db);
    return ok;
}

} // namespace

class TestExportDatabase : public QObject
{
    Q_OBJECT

private slots:
    void open_readsMeta();
    void open_refusesUnknownMajor();
    void open_refusesNonExport();
    void open_refusesMissingFile();

    void findByCrc_matchesAndCarriesHeaderSkip();
    void findByCrc_isCaseInsensitiveOnCrc();
    void findByCrc_missesOnOtherPlatform();

    void findByRomset_normalisesToLowercase();

    void searchByName_ordersByRating();

    void platformsForGame_preferredFirstAndNullIsNotATarget();
    void allPlatformKeys_excludesNull();

    void romHashesForGame_roundTripsWithFindByCrc();
    void romsetsForGame_roundTripsWithFindByRomset();
    void realExport_ifPresent();
};

void TestExportDatabase::open_readsMeta()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));

    ExportDatabase db;
    QString        error;
    QVERIFY2(db.open(path, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(db.meta().schemaVersion, QStringLiteral("1.3.0"));
    QCOMPARE(db.meta().major, 1);
    QCOMPARE(db.meta().minor, 3);
    QCOMPARE(db.meta().games, 2);
}

void TestExportDatabase::open_refusesUnknownMajor()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "2.0.0"));

    ExportDatabase db;
    QString        error;
    // §2 : refuser une MAJEURE inconnue plutôt que casser en silence.
    QVERIFY(!db.open(path, &error));
    QVERIFY(error.contains(QStringLiteral("MAJEURE")));
    QVERIFY(error.contains(QStringLiteral("2")));
    QVERIFY(!db.isOpen());
}

void TestExportDatabase::open_refusesNonExport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("autre.db");

    sqlite3 *raw = nullptr;
    QVERIFY(sqlite3_open(path.toUtf8().constData(), &raw) == SQLITE_OK);
    sqlite3_exec(raw, "CREATE TABLE t(x)", nullptr, nullptr, nullptr);
    sqlite3_close(raw);

    ExportDatabase db;
    QString        error;
    QVERIFY(!db.open(path, &error));
    QVERIFY(!error.isEmpty());
}

void TestExportDatabase::open_refusesMissingFile()
{
    ExportDatabase db;
    QString        error;
    QVERIFY(!db.open(QStringLiteral("/n/existe/pas.db"), &error));
    QVERIFY(error.contains(QStringLiteral("introuvable")));
}

void TestExportDatabase::findByCrc_matchesAndCarriesHeaderSkip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto match = db.findByCrc(QStringLiteral("DEADBEEF"), QStringLiteral("nes"));
    QVERIFY(match.has_value());
    QCOMPARE(match->gameKey, QStringLiteral("igdb-1"));
    QCOMPARE(match->headerSkip, 16); // l'en-tête iNES doit remonter jusqu'au scanner
}

void TestExportDatabase::findByCrc_isCaseInsensitiveOnCrc()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    // Un CRC calculé en minuscules est courant ; ce serait un faux négatif silencieux.
    QVERIFY(db.findByCrc(QStringLiteral("b19ed489"), QStringLiteral("snes")).has_value());
}

void TestExportDatabase::findByCrc_missesOnOtherPlatform()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    QVERIFY(!db.findByCrc(QStringLiteral("B19ED489"), QStringLiteral("megadrive")).has_value());
}

void TestExportDatabase::findByRomset_normalisesToLowercase()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    // Le nom de fichier peut arriver en majuscules depuis le disque.
    const auto match = db.findByRomset(QStringLiteral("SF2CE"), QStringLiteral("arcade"));
    QVERIFY(match.has_value());
    QCOMPARE(match->hardware, QStringLiteral("cps1"));
    QCOMPARE(match->driverStatus, QStringLiteral("good"));
}

void TestExportDatabase::searchByName_ordersByRating()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto games = db.searchByName(QStringLiteral("MARIO"));
    QCOMPARE(games.size(), 1);
    QCOMPARE(games.first().title, QStringLiteral("Super Mario World"));
    QCOMPARE(games.first().rating, 96);
}

void TestExportDatabase::platformsForGame_preferredFirstAndNullIsNotATarget()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto platforms = db.platformsForGame(QStringLiteral("igdb-1"));
    QCOMPARE(platforms.size(), 2);
    QVERIFY(platforms.first().isPreferred);
    QCOMPARE(platforms.first().platformKey, QStringLiteral("snes"));

    // La plateforme sans clé est affichable mais pas lançable : à ne pas confondre avec
    // un statut noir.
    QVERIFY(!platforms.last().isEmulationTarget());
}

void TestExportDatabase::allPlatformKeys_excludesNull()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    QCOMPARE(db.allPlatformKeys(), (QStringList{ "arcade", "snes" }));
}

void TestExportDatabase::romHashesForGame_roundTripsWithFindByCrc()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    // Le sens inverse doit refermer la boucle : tout CRC listé pour un jeu doit ramener
    // ce jeu. Sans ça, le lookup à chaud du scanner serait cassé sans que rien ne le dise.
    const auto hashes = db.romHashesForGame(QStringLiteral("igdb-1"));
    QCOMPARE(hashes.size(), 2);
    for (const auto &hash : hashes) {
        const auto found = db.findByCrc(hash.crc32, hash.platformKey);
        QVERIFY2(found.has_value(), qPrintable(hash.crc32));
        QCOMPARE(found->gameKey, QStringLiteral("igdb-1"));
    }
}

void TestExportDatabase::romsetsForGame_roundTripsWithFindByRomset()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto romsets = db.romsetsForGame(QStringLiteral("igdb-2"));
    QCOMPARE(romsets.size(), 1);
    const auto found = db.findByRomset(romsets.first().romset, romsets.first().platformKey);
    QVERIFY(found.has_value());
    QCOMPARE(found->gameKey, QStringLiteral("igdb-2"));
}

void TestExportDatabase::realExport_ifPresent()
{
    const QString path = QStringLiteral(IGIRIS_SOURCE_DIR "/data/games.db");
    if (!QFileInfo::exists(path))
        QSKIP("data/games.db absent — artefact téléchargé, pas versionné");

    ExportDatabase db;
    QString        error;
    QVERIFY2(db.open(path, &error), qPrintable(error));

    // Le vrai export, aux vrais chiffres : c'est le contrat du §3 qui est vérifié ici.
    QCOMPARE(db.meta().major, 1);
    QVERIFY(db.meta().games > 1000);
    QVERIFY(!db.allPlatformKeys().isEmpty());
    QVERIFY(!db.searchByName(QStringLiteral("mario")).isEmpty());
}

QTEST_GUILESS_MAIN(TestExportDatabase)
#include "test_export_database.moc"
