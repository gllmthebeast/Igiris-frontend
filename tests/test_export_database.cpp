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

// Ajoute les tables de langues du 1.4.0 à un export déjà construit.
//
// Volontairement SÉPARÉ de buildExport() : c'est ce qui permet de tester les deux formes
// d'export avec le même code, et donc de vérifier qu'un 1.3.0 reste lisible — c'est toute
// la définition d'une mineure additive (§2).
bool addLanguages(const QString &path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    // « xx » n'a PAS de bit : c'est le cas que le backend annonce (bit_index NULL) et il
    // doit être traité comme « pas de bit », jamais comme le bit 0.
    const char *sql = R"(
        ALTER TABLE exp_game ADD COLUMN lang_mask INTEGER;
        CREATE TABLE exp_language(lang_code TEXT PRIMARY KEY, label TEXT NOT NULL,
                                  badge_asset TEXT, bit_index INTEGER);
        INSERT INTO exp_language VALUES('en','Anglais','lang/en.png',0),
                                       ('fr','Français','lang/fr.png',1),
                                       ('ja','Japonais','lang/ja.png',5),
                                       ('xx','Sans bit','lang/xx.png',NULL);

        CREATE TABLE exp_game_language(game_key TEXT NOT NULL, lang_code TEXT NOT NULL,
                                       batocera_system TEXT NOT NULL, crc32 TEXT NOT NULL,
                                       PRIMARY KEY(game_key, lang_code, batocera_system, crc32))
            WITHOUT ROWID;
        -- igdb-1 : anglais et français sur snes, japonais sur nes seulement.
        INSERT INTO exp_game_language VALUES
            ('igdb-1','en','snes','B19ED489'),
            ('igdb-1','fr','snes','B19ED489'),
            ('igdb-1','ja','nes','DEADBEEF'),
            ('igdb-1','xx','snes','B19ED489');
        -- en(0) | fr(1) | ja(5) = 35. « xx » n'entre pas dans le masque.
        UPDATE exp_game SET lang_mask = 35 WHERE game_key = 'igdb-1';
    )";

    char      *errmsg = nullptr;
    const bool ok     = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) == SQLITE_OK;
    if (errmsg)
        sqlite3_free(errmsg);
    sqlite3_close(db);
    return ok;
}

// Ajoute les colonnes des 1.5.0 et 1.6.0 : bandeau, synopsis, modes, année par plateforme.
//
// Séparé de buildExport() pour la même raison qu'addLanguages() : c'est ce qui permet de
// vérifier qu'un export ANTÉRIEUR reste lisible avec le même code.
//
// ⚠️ Les colonnes sont ajoutées ICI dans un ordre différent de celui de la production —
// artwork_ref y est au milieu d'exp_game, ici à la fin. C'est délibéré : le chargeur ne
// doit dépendre que des NOMS de colonnes, jamais de leur position. C'est précisément le
// décalage positionnel qui avait mis lang_mask à 0 sur tout le catalogue côté backend.
bool addFiches(const QString &path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;

    const char *sql = R"(
        ALTER TABLE exp_game ADD COLUMN summary TEXT;
        ALTER TABLE exp_game ADD COLUMN lang_catalog_mask INTEGER;
        ALTER TABLE exp_game ADD COLUMN mode_mask INTEGER;
        ALTER TABLE exp_game ADD COLUMN artwork_ref TEXT;
        ALTER TABLE exp_game_platform ADD COLUMN release_year INTEGER;

        CREATE TABLE exp_game_mode(mode_key TEXT PRIMARY KEY, label TEXT NOT NULL,
                                   bit_index INTEGER NOT NULL);
        INSERT INTO exp_game_mode VALUES('solo','Un joueur',0),
                                        ('multi','Multijoueur',1),
                                        ('coop','Coopératif',2);

        -- igdb-1 : bandeau, synopsis, solo|multi = 3.
        UPDATE exp_game SET artwork_ref = 'https://img/art1.jpg',
                            summary     = 'A plumber saves a dinosaur island.',
                            mode_mask   = 3
                        WHERE game_key = 'igdb-1';
        -- igdb-2 : PAS de bandeau ni de synopsis, et solo|coop = 5. C'est le cas des 4,4 %
        -- du catalogue sans illustration : la fiche doit retomber sur la jaquette.
        UPDATE exp_game SET mode_mask = 5 WHERE game_key = 'igdb-2';

        -- LANGUES DE CATALOGUE (1.7.0). igdb-1 : en(0) | ja(5) | de(3) = 41.
        -- « en » et « ja » sont DÉJÀ fournis par une ROM, « de » ne l'est par aucune :
        -- c'est le recouvrement partiel, le seul cas qui distingue les deux masques.
        UPDATE exp_game SET lang_catalog_mask = 41 WHERE game_key = 'igdb-1';
        -- igdb-2 n'a aucune langue de ROM : fr(1) ne vient QUE du catalogue.
        UPDATE exp_game SET lang_catalog_mask = 2 WHERE game_key = 'igdb-2';

        -- L'année diffère de celle du jeu (1990) sur une plateforme et pas sur l'autre :
        -- c'est tout l'intérêt de la colonne, et le cas où un doublon se verrait.
        UPDATE exp_game_platform SET release_year = 1990
            WHERE game_key = 'igdb-1' AND display_name = 'Super Nintendo';
        UPDATE exp_game_platform SET release_year = 1992
            WHERE game_key = 'igdb-1' AND batocera_system IS NULL;
    )";

    char      *errmsg = nullptr;
    const bool ok     = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) == SQLITE_OK;
    if (errmsg)
        sqlite3_free(errmsg);
    sqlite3_close(db);
    return ok;
}

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
            -- emu_score NULL, comme en production depuis le 1.7.0 : un taux de fidélité
            -- d'émulation n'a aucun sens sur une plateforme qu'on n'émule pas.
            ('igdb-1',NULL,'Game Boy Advance (non émulé ici)',NULL,0),
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

    // --- lot 8 : les langues ---
    void languages_absentFromOlderExportWithoutBreakingIt();
    void languages_orderedByBitAndNullIsNotBitZero();
    void langMask_isTheOrOfTheBitsOfItsLanguages();
    void ownedLangMask_countsOnlyOwnedRoms();
    void ownedLangMask_isPerPlatformNotPerCrcAlone();
    void languagesForGame_keepsCodesWithoutBit();

    // --- exports 1.5.0 et 1.6.0 : bandeau, synopsis, modes, année par plateforme ---
    void fiches_absentFromOlderExportWithoutBreakingIt();
    void fiches_readColumnsByNameNotByPosition();
    void gameModes_orderedByBit();
    void releaseYear_isPerPlatformAndZeroWhenUnknown();

    // --- export 1.7.0 : catalogue élargi et langues de catalogue ---
    void catalogLanguages_areSeparateFromRomLanguages();
    void catalogLanguages_absentFromOlderExportWithoutBreakingIt();

    void realExport_ifPresent();
};

void TestExportDatabase::languages_absentFromOlderExportWithoutBreakingIt()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.3.0"));

    ExportDatabase db;
    QString        error;
    // Un export sans tables de langues doit s'ouvrir NORMALEMENT : c'est la définition
    // d'une mineure additive. Refuser ici serait une régression du §2.
    QVERIFY2(db.open(path, &error), qPrintable(error));
    QVERIFY(!db.hasLanguages());

    // Et les accesseurs doivent rendre du vide, pas planter sur une table absente.
    QVERIFY(db.languages().isEmpty());
    QVERIFY(db.langMaskByGame().isEmpty());
    QVERIFY(db.languagesForGame(QStringLiteral("igdb-1")).isEmpty());
}

void TestExportDatabase::languages_orderedByBitAndNullIsNotBitZero()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));
    QVERIFY(db.hasLanguages());

    const auto languages = db.languages();
    QCOMPARE(languages.size(), 4);

    // Ordre des BITS, pas alphabétique : « ja » (bit 5) après « fr » (bit 1).
    QCOMPARE(languages.at(0).code, QStringLiteral("en"));
    QCOMPARE(languages.at(1).code, QStringLiteral("fr"));
    QCOMPARE(languages.at(2).code, QStringLiteral("ja"));

    // NULL est l'ABSENCE de bit. Le confondre avec le bit 0 ferait passer « xx » pour de
    // l'anglais sur tout le catalogue, sans qu'aucune requête n'échoue.
    QCOMPARE(languages.at(3).code, QStringLiteral("xx"));
    QVERIFY(!languages.at(3).hasBit());
    QCOMPARE(languages.at(3).bit(), quint64(0));
    QCOMPARE(languages.at(0).bit(), quint64(1)); // en = bit 0
}

void TestExportDatabase::langMask_isTheOrOfTheBitsOfItsLanguages()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto masks = db.langMaskByGame();
    QCOMPARE(masks.value(QStringLiteral("igdb-1")), quint64(0b100011)); // en|fr|ja
    // Un jeu sans langue n'a pas d'entrée : ce n'est pas un masque à zéro à interpréter.
    QVERIFY(!masks.contains(QStringLiteral("igdb-2")));
}

void TestExportDatabase::ownedLangMask_countsOnlyOwnedRoms()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    // On ne possède que la ROM snes : anglais et français illuminés, japonais non — il
    // n'existe que sur la ROM nes, absente. C'est toute la règle du §8.
    const QSet<QString> owned = { romKey(QStringLiteral("B19ED489"), QStringLiteral("snes")) };
    const auto          masks = db.ownedLangMaskByGame(owned);
    QCOMPARE(masks.value(QStringLiteral("igdb-1")), quint64(0b000011));

    // Sans aucune ROM, aucun badge illuminé — et surtout pas « tout ».
    QVERIFY(db.ownedLangMaskByGame({}).isEmpty());
}

void TestExportDatabase::ownedLangMask_isPerPlatformNotPerCrcAlone()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    // Le bon CRC sur la MAUVAISE plateforme ne doit rien allumer. Un CRC seul n'identifie
    // rien : c'est le couple qui est la clé, ici comme dans exp_rom_hash.
    const QSet<QString> owned = { romKey(QStringLiteral("B19ED489"), QStringLiteral("nes")) };
    QVERIFY(db.ownedLangMaskByGame(owned).isEmpty());
}

void TestExportDatabase::languagesForGame_keepsCodesWithoutBit()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto languages = db.languagesForGame(QStringLiteral("igdb-1"));
    QCOMPARE(languages.size(), 4);

    // « xx » est hors masque mais PRÉSENT ici : la fiche de jeu ne passe pas par le
    // masque, elle n'a donc pas à perdre ces langues.
    QStringList codes;
    for (const auto &language : languages)
        codes.append(language.langCode);
    QVERIFY(codes.contains(QStringLiteral("xx")));

    // Et chaque ligne porte bien SA plateforme et SON crc — sans quoi l'illumination par
    // plateforme du §7 serait incalculable.
    for (const auto &language : languages) {
        if (language.langCode == QStringLiteral("ja")) {
            QCOMPARE(language.platformKey, QStringLiteral("nes"));
            QCOMPARE(language.crc32, QStringLiteral("DEADBEEF"));
        }
    }
}

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

    // Et elle ne porte AUCUN score. -1, pas 0 : « 0 » se lirait « émulation
    // catastrophique » là où la bonne réponse est « sans objet » (§5).
    QVERIFY(!platforms.last().hasEmuScore());
    QCOMPARE(platforms.last().emuScore, -1);
    QVERIFY(platforms.first().hasEmuScore());
    QCOMPARE(platforms.first().emuScore, 95);
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

void TestExportDatabase::fiches_absentFromOlderExportWithoutBreakingIt()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QString        error;
    // Un export 1.4.0 s'ouvre normalement : la 1.5.0 et la 1.6.0 sont additives (§2).
    QVERIFY2(db.open(path, &error), qPrintable(error));
    QVERIFY(!db.hasModes());
    QVERIFY(db.gameModes().isEmpty());

    // Et les champs manquants rendent du vide, sans requête en échec : les SELECT émettent
    // « NULL » à la place de la colonne absente, donc rien ne se décale.
    const auto game = db.gameByKey(QStringLiteral("igdb-1"));
    QVERIFY(game.has_value());
    QVERIFY(game->artworkRef.isEmpty());
    QVERIFY(game->summary.isEmpty());
    QCOMPARE(game->modeMask, quint64(0));

    // Ce qui existait AVANT doit être intact : c'est le vrai risque d'une colonne ajoutée.
    QCOMPARE(game->title, QStringLiteral("Super Mario World"));
    QCOMPARE(game->year, 1990);
    QCOMPARE(game->rating, 96);

    const auto platforms = db.platformsForGame(QStringLiteral("igdb-1"));
    QCOMPARE(platforms.size(), 2);
    QCOMPARE(platforms.first().releaseYear, 0);
    QCOMPARE(platforms.first().emuScore, 95);
}

void TestExportDatabase::fiches_readColumnsByNameNotByPosition()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.6.0"));
    QVERIFY(addLanguages(path));
    QVERIFY(addFiches(path));

    ExportDatabase db;
    QString        error;
    QVERIFY2(db.open(path, &error), qPrintable(error));
    QVERIFY(db.hasModes());

    const auto game = db.gameByKey(QStringLiteral("igdb-1"));
    QVERIFY(game.has_value());
    QCOMPARE(game->artworkRef, QStringLiteral("https://img/art1.jpg"));
    QCOMPARE(game->summary, QStringLiteral("A plumber saves a dinosaur island."));
    QCOMPARE(game->modeMask, quint64(3));

    // LE test qui compte : les champs préexistants n'ont pas bougé alors que trois
    // colonnes se sont ajoutées, dans un ordre différent de la production. Un chargeur
    // qui lirait par position aurait ici year dans rating, silencieusement.
    QCOMPARE(game->title, QStringLiteral("Super Mario World"));
    QCOMPARE(game->searchKey, QStringLiteral("super mario world"));
    QCOMPARE(game->year, 1990);
    QCOMPARE(game->rating, 96);

    // Le jeu sans illustration : vide, et surtout pas la jaquette recopiée ici — c'est la
    // fiche qui décide du repli, et elle doit pouvoir distinguer les deux cas.
    const auto other = db.gameByKey(QStringLiteral("igdb-2"));
    QVERIFY(other.has_value());
    QVERIFY(other->artworkRef.isEmpty());
    QVERIFY(other->summary.isEmpty());
    QCOMPARE(other->modeMask, quint64(5));

    // Les trois requêtes qui produisent des Game partagent la même liste de colonnes :
    // elles doivent donc lire exactement la même chose.
    const auto searched = db.searchByName(QStringLiteral("mario"));
    QCOMPARE(searched.size(), 1);
    QCOMPARE(searched.first().artworkRef, game->artworkRef);
    QCOMPARE(searched.first().modeMask, game->modeMask);

    for (const auto &listed : db.allGames()) {
        if (listed.gameKey == game->gameKey) {
            QCOMPARE(listed.summary, game->summary);
            QCOMPARE(listed.modeMask, game->modeMask);
        }
    }
}

void TestExportDatabase::gameModes_orderedByBit()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.6.0"));
    QVERIFY(addFiches(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto modes = db.gameModes();
    QCOMPARE(modes.size(), 3);
    QCOMPARE(modes.at(0).key, QStringLiteral("solo"));
    QCOMPARE(modes.at(1).key, QStringLiteral("multi"));
    QCOMPARE(modes.at(2).key, QStringLiteral("coop"));
    QCOMPARE(modes.at(0).label, QStringLiteral("Un joueur"));

    // Le bit se lit dans l'export, il ne se déduit JAMAIS de la position dans la liste —
    // même règle que pour les langues (§8), et pour la même raison : un décalage
    // produirait des filtres faux sans rien signaler.
    QCOMPARE(modes.at(2).bit(), quint64(4));

    // igdb-1 est solo|multi : coop ne doit PAS ressortir.
    const auto game = db.gameByKey(QStringLiteral("igdb-1"));
    QVERIFY(game.has_value());
    QVERIFY(game->modeMask & modes.at(0).bit());
    QVERIFY(game->modeMask & modes.at(1).bit());
    QVERIFY(!(game->modeMask & modes.at(2).bit()));
}

void TestExportDatabase::releaseYear_isPerPlatformAndZeroWhenUnknown()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.6.0"));
    QVERIFY(addFiches(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));

    const auto platforms = db.platformsForGame(QStringLiteral("igdb-1"));
    QCOMPARE(platforms.size(), 2);

    // L'ordre vient de la requête : is_preferred d'abord. La SNES est l'élue.
    QCOMPARE(platforms.at(0).displayName, QStringLiteral("Super Nintendo"));
    QCOMPARE(platforms.at(0).releaseYear, 1990);

    // La seconde porte 1992 alors que le JEU est de 1990 : c'est exactement ce que la
    // colonne apporte, et une année recopiée depuis exp_game passerait ce test à côté.
    QCOMPARE(platforms.at(1).releaseYear, 1992);

    // Année inconnue = 0, jamais une valeur inventée : la fiche n'affiche alors rien.
    const auto arcade = db.platformsForGame(QStringLiteral("igdb-2"));
    QCOMPARE(arcade.size(), 1);
    QCOMPARE(arcade.first().releaseYear, 0);
}

void TestExportDatabase::catalogLanguages_areSeparateFromRomLanguages()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.7.0"));
    QVERIFY(addLanguages(path));
    QVERIFY(addFiches(path));

    ExportDatabase db;
    QString        error;
    QVERIFY2(db.open(path, &error), qPrintable(error));
    QVERIFY(db.hasCatalogLanguages());

    const auto game = db.gameByKey(QStringLiteral("igdb-1"));
    QVERIFY(game.has_value());

    // LE point de cette version : les deux masques cohabitent SANS se mélanger.
    // lang_mask = en|fr|ja = 35, lang_catalog_mask = en|de|ja = 41. Un chargeur qui les
    // confondrait rendrait la même valeur pour les deux, et personne ne le verrait.
    QCOMPARE(db.langMaskByGame().value(QStringLiteral("igdb-1")), quint64(35));
    QCOMPARE(game->langCatalogMask, quint64(41));
    QVERIFY(game->langCatalogMask != db.langMaskByGame().value(QStringLiteral("igdb-1")));

    // Un jeu qu'aucun dat ne documente peut n'exister QUE par le catalogue : c'est le cas
    // de 6 879 jeux en production, et c'est tout l'apport de la colonne.
    const auto other = db.gameByKey(QStringLiteral("igdb-2"));
    QVERIFY(other.has_value());
    QCOMPARE(db.langMaskByGame().value(QStringLiteral("igdb-2"), 0), quint64(0));
    QCOMPARE(other->langCatalogMask, quint64(2));

    // Le référentiel de bits est COMMUN aux deux masques — c'est ce qui rend le mélange
    // possible, donc dangereux, et c'est pourquoi rien ne les fusionne côté chargeur.
    for (const auto &language : db.languages()) {
        if (language.code == QStringLiteral("de"))
            QVERIFY(game->langCatalogMask & language.bit());
    }
}

void TestExportDatabase::catalogLanguages_absentFromOlderExportWithoutBreakingIt()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("games.db");
    QVERIFY(buildExport(path, "1.4.0"));
    QVERIFY(addLanguages(path));

    ExportDatabase db;
    QVERIFY(db.open(path, nullptr));
    QVERIFY(!db.hasCatalogLanguages());

    // Absente ⇒ 0, et surtout pas une valeur reprise de lang_mask : le filtre « existe »
    // doit alors retomber exactement sur le comportement du 1.4.0.
    const auto game = db.gameByKey(QStringLiteral("igdb-1"));
    QVERIFY(game.has_value());
    QCOMPARE(game->langCatalogMask, quint64(0));
    QCOMPARE(db.langMaskByGame().value(QStringLiteral("igdb-1")), quint64(35));
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
