// Tests du lot 7 — fiche de jeu.
//
// Le point sensible est le STATUT : c'est le fichier de description des systèmes qui
// décide du noir, pas le catalogue (§1). Se tromper de source afficherait des systèmes
// comme disponibles alors qu'ils ne sont pas installés, et le lancement échouerait au
// dernier moment.

#include "platform/BatoceraAdapter.h"
#include "ui/GameDetailModel.h"

#include <sqlite3.h>

#include <QTemporaryDir>
#include <QTest>

using namespace igiris::ui;
using igiris::catalog::ExportDatabase;
using igiris::platform::BatoceraAdapter;
using igiris::platform::SystemEntry;

namespace {

bool buildExport(const QString &path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    const char *sql =
        "CREATE TABLE exp_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "INSERT INTO exp_meta VALUES('schema_version','1.3.0');"
        "CREATE TABLE exp_game(game_key TEXT PRIMARY KEY, title TEXT NOT NULL,"
        " search_key TEXT NOT NULL, year INTEGER, cover_ref TEXT, rating INTEGER);"
        "INSERT INTO exp_game VALUES('igdb-1','Mega Man X3 (1995)','mega man x3',1995,NULL,78);"
        "CREATE TABLE exp_game_platform(game_key TEXT, batocera_system TEXT,"
        " display_name TEXT NOT NULL, emu_score INTEGER, is_preferred INTEGER DEFAULT 0,"
        " PRIMARY KEY(game_key, display_name));"
        // snes apparaît DEUX fois, comme dans le vrai export (SNES et SFAM).
        "INSERT INTO exp_game_platform VALUES"
        " ('igdb-1','snes','SNES',95,1),"
        " ('igdb-1','snes','SFAM',95,1),"
        " ('igdb-1','psx','PS1',90,1),"
        " ('igdb-1','wiiu','WiiU',42,1),"
        " ('igdb-1',NULL,'Plateforme non émulée',0,0);"
        "CREATE TABLE exp_rom_hash(crc32 TEXT, batocera_system TEXT, game_key TEXT,"
        " header_skip INTEGER DEFAULT 0, PRIMARY KEY(crc32, batocera_system));"
        "CREATE TABLE exp_romset(romset TEXT, batocera_system TEXT, game_key TEXT,"
        " emulators TEXT, hardware TEXT, driver_status TEXT,"
        " PRIMARY KEY(romset, batocera_system));";

    const bool ok = sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

SystemEntry system(const QString &name, const QString &fullName)
{
    SystemEntry entry;
    entry.name     = name;
    entry.fullName = fullName;
    entry.launchOptions = { { QString(),
                              QStringLiteral("emulatorlauncher %CONTROLLERSCONFIG% "
                                             "-system %SYSTEM% -rom %ROM% "
                                             "-gameinfoxml %GAMEINFOXML% "
                                             "-systemname %SYSTEMNAME%") } };
    return entry;
}

QString ownedKey(const QString &game, const QString &platform)
{
    return game + QLatin1Char('\x1f') + platform;
}

} // namespace

class TestGameDetailModel : public QObject
{
    Q_OBJECT

private slots:
    void statuses_comeFromTheSystemsFileNotTheCatalogue();
    void duplicatePlatformRowsAreCollapsed();
    void nonEmulatedPlatformIsExcluded();
    void defaultChoice_isTheFirstPlayableSystem();
    void defaultChoice_fallsBackWhenNothingIsOwned();
    void launch_refusesAnythingButGreen();
    void commandPreview_resolvesTheRealBatoceraCommand();
    void launchWarning_tellsWhatIsMissing();
};

void TestGameDetailModel::statuses_comeFromTheSystemsFileNotTheCatalogue()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    // snes et psx installés ; wiiu ABSENT du fichier de description.
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") },
                            { QStringLiteral("psx"), system("psx", "PlayStation") } });
    model.setOwnedRoms({ { ownedKey("igdb-1", "snes"), QStringLiteral("/roms/snes/x.sfc") } });
    model.setGame(QStringLiteral("igdb-1"));

    QCOMPARE(model.rowCount(), 3); // snes, psx, wiiu
    const auto statusAt = [&](int row) {
        return model.data(model.index(row, 0), GameDetailModel::StatusRole).toInt();
    };
    const auto keyAt = [&](int row) {
        return model.data(model.index(row, 0), GameDetailModel::PlatformKeyRole).toString();
    };

    for (int i = 0; i < model.rowCount(); ++i) {
        if (keyAt(i) == QLatin1String("snes"))
            QCOMPARE(statusAt(i), int(GameDetailModel::Green)); // installé + ROM
        else if (keyAt(i) == QLatin1String("psx"))
            QCOMPARE(statusAt(i), int(GameDetailModel::Red)); // installé, pas de ROM
        else if (keyAt(i) == QLatin1String("wiiu"))
            QCOMPARE(statusAt(i), int(GameDetailModel::Black)); // pas installé
    }
}

void TestGameDetailModel::duplicatePlatformRowsAreCollapsed()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setGame(QStringLiteral("igdb-1"));

    // « SNES » et « SFAM » désignent la même clé : une seule ligne, sinon la fiche
    // proposerait deux fois le même système.
    int snesRows = 0;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.data(model.index(i, 0), GameDetailModel::PlatformKeyRole).toString()
            == QLatin1String("snes"))
            ++snesRows;
    }
    QCOMPARE(snesRows, 1);
}

void TestGameDetailModel::nonEmulatedPlatformIsExcluded()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setGame(QStringLiteral("igdb-1"));

    // La plateforme sans clé n'est ni verte, ni rouge, ni noire : elle n'a pas sa place
    // dans la liste des systèmes jouables.
    for (int i = 0; i < model.rowCount(); ++i) {
        QVERIFY(!model.data(model.index(i, 0), GameDetailModel::PlatformKeyRole)
                     .toString()
                     .isEmpty());
    }
    QCOMPARE(model.rowCount(), 3);
}

void TestGameDetailModel::defaultChoice_isTheFirstPlayableSystem()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") },
                            { QStringLiteral("psx"), system("psx", "PlayStation") } });
    model.setOwnedRoms({ { ownedKey("igdb-1", "snes"), QStringLiteral("/roms/snes/x.sfc") } });
    model.setGame(QStringLiteral("igdb-1"));

    // is_preferred vaut 1 partout dans le vrai export : il ne peut pas servir de défaut.
    // Le défaut est le premier système JOUABLE.
    int defaults = 0, defaultRow = -1;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.data(model.index(i, 0), GameDetailModel::DefaultChoiceRole).toBool()) {
            ++defaults;
            defaultRow = i;
        }
    }
    QCOMPARE(defaults, 1);
    QCOMPARE(model.data(model.index(defaultRow, 0), GameDetailModel::PlatformKeyRole).toString(),
             QStringLiteral("snes"));
}

void TestGameDetailModel::defaultChoice_fallsBackWhenNothingIsOwned()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setGame(QStringLiteral("igdb-1")); // rien d'installé, rien de possédé

    int defaults = 0;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.data(model.index(i, 0), GameDetailModel::DefaultChoiceRole).toBool())
            ++defaults;
    }
    QCOMPARE(defaults, 1); // une proposition, toujours — jamais zéro
}

void TestGameDetailModel::launch_refusesAnythingButGreen()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    BatoceraAdapter adapter(dir.path());
    GameDetailModel model;
    model.setCatalogue(&db);
    model.setAdapter(&adapter);
    model.setLocalSystems({ { QStringLiteral("psx"), system("psx", "PlayStation") } });
    model.setGame(QStringLiteral("igdb-1"));

    for (int i = 0; i < model.rowCount(); ++i) {
        const QString message = model.launch(i);
        // §7 : « le lancement se fait sur un système en vert ». Aucun ici.
        QVERIFY2(!message.isEmpty(), qPrintable(QStringLiteral("ligne %1").arg(i)));
    }
}

void TestGameDetailModel::commandPreview_resolvesTheRealBatoceraCommand()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    BatoceraAdapter adapter(dir.path());
    GameDetailModel model;
    model.setCatalogue(&db);
    model.setAdapter(&adapter);
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") } });
    model.setOwnedRoms(
        { { ownedKey("igdb-1", "snes"), QStringLiteral("/roms/snes/Mega Man X3.sfc") } });
    model.setGame(QStringLiteral("igdb-1"));

    int green = -1;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.data(model.index(i, 0), GameDetailModel::StatusRole).toInt()
            == GameDetailModel::Green)
            green = i;
    }
    QVERIFY(green >= 0);

    const QString preview = model.commandPreview(green);
    QVERIFY(preview.startsWith(QStringLiteral("emulatorlauncher")));
    QVERIFY(preview.contains(QStringLiteral("/roms/snes/Mega Man X3.sfc")));
    // %SYSTEMNAME% doit porter le LIBELLÉ, pas la clé.
    QVERIFY(preview.contains(QStringLiteral("Super Nintendo")));
    // Aucun placeholder ne doit subsister.
    QVERIFY(!preview.contains(QStringLiteral("%")));
}

void TestGameDetailModel::launchWarning_tellsWhatIsMissing()
{
    GameDetailModel model;
    // Sans adaptateur : le dire, plutôt que d'offrir un bouton qui ne ferait rien (§1).
    QVERIFY(!model.launchAvailable());
    QVERIFY(model.launchWarning().contains(QStringLiteral("distribution")));

    QTemporaryDir   dir;
    BatoceraAdapter adapter(dir.path());
    model.setAdapter(&adapter);
    QVERIFY(model.launchWarning().contains(QStringLiteral("description")));

    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") } });
    // Capacité ControllerMapping non déclarée : l'avertissement le signale.
    QVERIFY(model.launchWarning().contains(QStringLiteral("manettes")));
}

QTEST_MAIN(TestGameDetailModel)
#include "test_game_detail_model.moc"
