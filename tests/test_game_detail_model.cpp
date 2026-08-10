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

// Les langues du 1.4.0, greffées sur l'export du lot 7.
//
// Le cas construit ici est CELUI qui distingue la fiche de la vue liste : le même jeu
// existe en anglais sur snes ET sur psx, mais seule la ROM snes est possédée. Le §7 exige
// que l'anglais soit illuminé sur snes et grisé sur psx — l'union du §8 ne suffit pas.
bool addLanguages(const QString &path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    const char *sql =
        "ALTER TABLE exp_game ADD COLUMN lang_mask INTEGER;"
        "CREATE TABLE exp_language(lang_code TEXT PRIMARY KEY, label TEXT NOT NULL,"
        " badge_asset TEXT, bit_index INTEGER);"
        "INSERT INTO exp_language VALUES('en','Anglais',NULL,0),('fr','Français',NULL,1),"
        " ('ja','Japonais',NULL,5),('xx','Sans bit',NULL,NULL);"
        "CREATE TABLE exp_game_language(game_key TEXT, lang_code TEXT, batocera_system TEXT,"
        " crc32 TEXT, PRIMARY KEY(game_key, lang_code, batocera_system, crc32)) WITHOUT ROWID;"
        "INSERT INTO exp_game_language VALUES"
        " ('igdb-1','en','snes','AAAA1111'),"
        " ('igdb-1','ja','snes','BBBB2222'),"   // autre ROM snes, NON possédée
        " ('igdb-1','xx','snes','AAAA1111'),"   // sans bit : doit rester visible en fiche
        " ('igdb-1','en','psx','CCCC3333'),"
        " ('igdb-1','fr','psx','CCCC3333');"
        "UPDATE exp_game SET lang_mask = 35 WHERE game_key = 'igdb-1';";

    const bool ok = sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

QList<igiris::catalog::Language> sampleLanguages()
{
    return { { QStringLiteral("en"), QStringLiteral("Anglais"), {}, 0 },
             { QStringLiteral("fr"), QStringLiteral("Français"), {}, 1 },
             { QStringLiteral("ja"), QStringLiteral("Japonais"), {}, 5 },
             { QStringLiteral("xx"), QStringLiteral("Sans bit"), {}, -1 } };
}

QStringList badgeCodes(const QVariantList &badges)
{
    QStringList codes;
    for (const QVariant &badge : badges) {
        const QVariantMap map = badge.toMap();
        codes.append(map.value("owned").toBool() ? map.value("code").toString().toUpper()
                                                 : map.value("code").toString());
    }
    return codes;
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

    // --- lot 8 : les langues, plateforme par plateforme ---
    void languages_areRestrictedToTheirOwnPlatform();
    void languages_surviveWithoutABitIndex();
    void languages_stayEmptyOnAnExportWithoutThem();
};

void TestGameDetailModel::languages_areRestrictedToTheirOwnPlatform()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    QVERIFY(addLanguages(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setLanguages(sampleLanguages());
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") },
                            { QStringLiteral("psx"), system("psx", "PlayStation") } });
    // Une seule ROM possédée : la snes anglaise.
    model.setOwnedRomKeys({ igiris::catalog::romKey(QStringLiteral("AAAA1111"),
                                                    QStringLiteral("snes")) });
    model.setGame(QStringLiteral("igdb-1"));

    const auto languagesAt = [&](const QString &platformKey) {
        for (int i = 0; i < model.rowCount(); ++i) {
            const QModelIndex index = model.index(i, 0);
            if (index.data(GameDetailModel::PlatformKeyRole).toString() == platformKey)
                return badgeCodes(index.data(GameDetailModel::LanguagesRole).toList());
        }
        return QStringList{ QStringLiteral("<plateforme absente>") };
    };

    // snes : l'anglais est ILLUMINÉ — la ROM possédée le fournit. « xx » l'est aussi, il
    // vient du MÊME crc : une langue hors masque reste parfaitement illuminable, puisque
    // la fiche décide sur le crc et non sur le masque. Le japonais existe sur snes mais
    // vient d'une autre ROM, absente : grisé. Les possédées passent devant (§8).
    QCOMPARE(languagesAt(QStringLiteral("snes")),
             QStringList({ QStringLiteral("EN"), QStringLiteral("XX"), QStringLiteral("ja") }));

    // psx : l'anglais existe AUSSI, mais aucune ROM psx n'est possédée. C'est LE test du
    // §7 — l'union du §8 illuminerait l'anglais partout, ce qui serait faux ici.
    QCOMPARE(languagesAt(QStringLiteral("psx")),
             QStringList({ QStringLiteral("en"), QStringLiteral("fr") }));

    // wiiu : présent au catalogue, aucune langue rattachée. Pas de badge, pas de vide à
    // interpréter.
    QVERIFY(languagesAt(QStringLiteral("wiiu")).isEmpty());
}

void TestGameDetailModel::languages_surviveWithoutABitIndex()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    QVERIFY(addLanguages(path));
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setLanguages(sampleLanguages());
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") } });
    model.setGame(QStringLiteral("igdb-1"));

    // La fiche lit exp_game_language directement : elle n'a pas la limite du masque, et
    // « xx » doit donc y apparaître — relégué en fin, mais jamais perdu.
    for (int i = 0; i < model.rowCount(); ++i) {
        const QModelIndex index = model.index(i, 0);
        if (index.data(GameDetailModel::PlatformKeyRole).toString() != QLatin1String("snes"))
            continue;
        const QStringList codes = badgeCodes(index.data(GameDetailModel::LanguagesRole).toList());
        QVERIFY(codes.contains(QStringLiteral("xx")));
        QCOMPARE(codes.last(), QStringLiteral("xx"));
    }
}

void TestGameDetailModel::languages_stayEmptyOnAnExportWithoutThem()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path)); // 1.3.0, sans tables de langues
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") } });
    model.setGame(QStringLiteral("igdb-1"));

    // La fiche reste parfaitement fonctionnelle sans langues : statuts, lancement, tout
    // le lot 7 continue de marcher. C'est la définition d'un ajout mineur.
    QVERIFY(model.rowCount() > 0);
    for (int i = 0; i < model.rowCount(); ++i)
        QVERIFY(model.index(i, 0).data(GameDetailModel::LanguagesRole).toList().isEmpty());
}

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
