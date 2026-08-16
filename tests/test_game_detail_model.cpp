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

    // --- exports 1.5.0 et 1.6.0 : bandeau, synopsis, modes, année par plateforme ---
    void banner_fallsBackToTheCoverAndSaysSo();
    void banner_usesTheArtworkWhenTheExportHasOne();
    void releaseYear_isExposedPerPlatformRow();

    // --- export 1.7.0 : catalogue élargi ---
    void catalogLanguages_excludeThoseAlreadyProvidedByARom();
    void nonEmulablePlatforms_areKeptToExplainAnEmptyList();
};

// Ajoute les colonnes des 1.5.0 / 1.6.0 à l'export de test. `artwork` vide simule les
// 4,4 % du catalogue réel qui n'ont pas d'illustration dédiée.
namespace {
bool addFiches(const QString &path, const QString &artwork)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    const QString sql =
        QStringLiteral(
            "ALTER TABLE exp_game ADD COLUMN artwork_ref TEXT;"
            "ALTER TABLE exp_game ADD COLUMN summary TEXT;"
            "ALTER TABLE exp_game ADD COLUMN mode_mask INTEGER;"
            "ALTER TABLE exp_game_platform ADD COLUMN release_year INTEGER;"
            "CREATE TABLE exp_game_mode(mode_key TEXT PRIMARY KEY, label TEXT NOT NULL,"
            " bit_index INTEGER NOT NULL);"
            "INSERT INTO exp_game_mode VALUES('solo','Un joueur',0),('coop','Coopératif',2);"
            "UPDATE exp_game SET artwork_ref = %1, summary = 'A blue robot fights again.',"
            " mode_mask = 5;"
            // La SNES sort en 1995 comme le jeu, la PS1 deux ans plus tard : c'est l'écart
            // qui justifie la colonne, et un test sans écart ne prouverait rien.
            // SNES et SFAM sont la MÊME clé de plateforme et fusionnent en une ligne : les
            // dater toutes les deux, sinon le test dépend de laquelle survit à la fusion.
            "UPDATE exp_game_platform SET release_year = 1995"
            " WHERE display_name IN ('SNES','SFAM');"
            "UPDATE exp_game_platform SET release_year = 1997 WHERE display_name = 'PS1';")
            .arg(artwork.isEmpty() ? QStringLiteral("NULL")
                                   : QStringLiteral("'%1'").arg(artwork));

    char      *errmsg = nullptr;
    const bool ok = sqlite3_exec(db, sql.toUtf8().constData(), nullptr, nullptr, &errmsg)
                    == SQLITE_OK;
    if (errmsg)
        sqlite3_free(errmsg);
    sqlite3_close(db);
    return ok;
}
} // namespace

void TestGameDetailModel::catalogLanguages_excludeThoseAlreadyProvidedByARom()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    QVERIFY(addLanguages(path));

    // en(0) et de(3) au catalogue. « en » est DÉJÀ fourni par une ROM sur snes ; « de »
    // ne l'est par aucune. Seul « de » doit ressortir : afficher « en » ici le dédoublerait
    // avec son badge de plateforme, et brouillerait ce que la ligne veut dire.
    //
    // « de » est ajouté au référentiel ICI et non dans la fixture partagée : c'est une
    // langue qu'AUCUNE ROM ne fournit, ce qui n'a de sens que pour ce test.
    sqlite3 *raw = nullptr;
    QVERIFY(sqlite3_open(path.toUtf8().constData(), &raw) == SQLITE_OK);
    QVERIFY(sqlite3_exec(raw,
                         "INSERT INTO exp_language VALUES('de','Allemand',NULL,3);"
                         "ALTER TABLE exp_game ADD COLUMN lang_catalog_mask INTEGER;"
                         "UPDATE exp_game SET lang_catalog_mask = 9;",
                         nullptr, nullptr, nullptr)
            == SQLITE_OK);
    sqlite3_close(raw);

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));
    QVERIFY(db.hasCatalogLanguages());

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setLanguages(db.languages());
    model.setGame(QStringLiteral("igdb-1"));

    QCOMPARE(model.catalogLanguages(), QStringList({ QStringLiteral("de") }));

    // Et les badges de plateforme, eux, n'ont pas bougé d'un pouce : la langue de
    // catalogue ne doit RIEN ajouter à l'axe illuminé / grisé.
    bool sawEnglish = false;
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto badges = model.data(model.index(row, 0),
                                       GameDetailModel::LanguagesRole).toList();
        for (const auto &badge : badges) {
            const auto map = badge.toMap();
            QVERIFY(map.value(QStringLiteral("code")).toString() != QStringLiteral("de"));
            if (map.value(QStringLiteral("code")).toString() == QStringLiteral("en"))
                sawEnglish = true;
        }
    }
    QVERIFY(sawEnglish);
}

void TestGameDetailModel::nonEmulablePlatforms_areKeptToExplainAnEmptyList()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setGame(QStringLiteral("igdb-1"));

    // La plateforme non émulable reste HORS de la liste des systèmes — celle-ci ne parle
    // que de lançable — mais elle est conservée à part.
    for (int row = 0; row < model.rowCount(); ++row) {
        QVERIFY(!model.data(model.index(row, 0),
                            GameDetailModel::PlatformKeyRole).toString().isEmpty());
    }
    QCOMPARE(model.nonEmulablePlatforms(),
             QStringList({ QStringLiteral("Plateforme non émulée") }));

    // Sans elle, la fiche des 9 679 jeux qui n'ont QUE des plateformes non émulables
    // n'aurait rien à afficher ni rien à expliquer — un blanc qui se lit comme une panne.
    QVERIFY(!model.nonEmulablePlatforms().isEmpty());
}

void TestGameDetailModel::banner_fallsBackToTheCoverAndSaysSo()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    // Export 1.3.0 tel quel : aucune des colonnes n'existe.
    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setGame(QStringLiteral("igdb-1"));

    // Sans illustration, la fiche retombe sur la jaquette ET LE DIT : c'est hasRealBanner
    // qui pilote l'adoucissement du rendu. Le mensonge serait de renvoyer true ici.
    QVERIFY(!model.hasRealBanner());
    QCOMPARE(model.bannerRef(), model.coverRef());
    QVERIFY(model.summary().isEmpty());
    QVERIFY(model.modeLabels().isEmpty());
}

void TestGameDetailModel::banner_usesTheArtworkWhenTheExportHasOne()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    QVERIFY(addFiches(path, QStringLiteral("https://img/art.jpg")));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setGameModes(db.gameModes());
    model.setGame(QStringLiteral("igdb-1"));

    QVERIFY(model.hasRealBanner());
    QCOMPARE(model.bannerRef(), QStringLiteral("https://img/art.jpg"));
    QVERIFY(model.bannerRef() != model.coverRef());
    QCOMPARE(model.summary(), QStringLiteral("A blue robot fights again."));

    // mode_mask = 5 = solo(0) | coop(2). Les libellés viennent du référentiel, et l'ordre
    // est celui des bits — jamais celui de la table ni l'alphabétique.
    QCOMPARE(model.modeLabels(),
             QStringList({ QStringLiteral("Un joueur"), QStringLiteral("Coopératif") }));

    // Le cas des 4,4 % : colonne présente mais vide → repli, et la fiche le sait.
    QTemporaryDir bare;
    const QString barePath = bare.filePath("games.db");
    QVERIFY(buildExport(barePath));
    QVERIFY(addFiches(barePath, QString()));
    ExportDatabase bareDb;
    QVERIFY(bareDb.open(barePath, nullptr));

    GameDetailModel bareModel;
    bareModel.setCatalogue(&bareDb);
    bareModel.setGame(QStringLiteral("igdb-1"));
    QVERIFY(!bareModel.hasRealBanner());
    QCOMPARE(bareModel.bannerRef(), bareModel.coverRef());
}

void TestGameDetailModel::releaseYear_isExposedPerPlatformRow()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path));
    QVERIFY(addFiches(path, QStringLiteral("https://img/art.jpg")));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    GameDetailModel model;
    model.setCatalogue(&db);
    model.setLocalSystems({ { QStringLiteral("snes"), system("snes", "Super Nintendo") },
                            { QStringLiteral("psx"), system("psx", "PlayStation") } });
    model.setGame(QStringLiteral("igdb-1"));

    QHash<QString, int> yearByPlatform;
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row, 0);
        yearByPlatform.insert(
            model.data(index, GameDetailModel::PlatformKeyRole).toString(),
            model.data(index, GameDetailModel::ReleaseYearRole).toInt());
    }

    // L'année du JEU est 1995 ; la PS1 porte 1997. Une fiche qui recopierait exp_game.year
    // afficherait 1995 partout et passerait ce test à côté.
    QCOMPARE(model.year(), 1995);
    QCOMPARE(yearByPlatform.value(QStringLiteral("snes")), 1995);
    QCOMPARE(yearByPlatform.value(QStringLiteral("psx")), 1997);

    // Année inconnue = 0 : la fiche n'affiche alors rien plutôt qu'une date inventée.
    QCOMPARE(yearByPlatform.value(QStringLiteral("wiiu")), 0);
}

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
