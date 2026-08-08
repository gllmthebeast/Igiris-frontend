#include "catalog/ExportDatabase.h"

#include "catalog/ExportSchema.h"

#include <sqlite3.h>

#include <QFileInfo>
#include <QUrl>

namespace igiris::catalog {

namespace {

QString sqliteError(sqlite3 *db)
{
    return db ? QString::fromUtf8(sqlite3_errmsg(db)) : QStringLiteral("(pas de base)");
}

QString columnText(sqlite3_stmt *stmt, int index)
{
    const auto *text = sqlite3_column_text(stmt, index);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

void bindText(sqlite3_stmt *stmt, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(stmt, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

// Prépare une requête et renvoie nullptr en cas d'échec.
sqlite3_stmt *prepare(sqlite3 *db, const QString &sql)
{
    sqlite3_stmt    *stmt = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(db, utf8.constData(), utf8.size(), &stmt, nullptr) != SQLITE_OK)
        return nullptr;
    return stmt;
}

// Le nom de colonne vient d'ExportSchema.h et n'apparaît nulle part ailleurs.
QString platformColumn()
{
    return QString::fromLatin1(schema::kPlatformKeyColumn);
}

} // namespace

ExportDatabase::~ExportDatabase()
{
    if (m_db)
        sqlite3_close(m_db);
}

bool ExportDatabase::open(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }

    if (!QFileInfo::exists(path))
        return fail(QStringLiteral("export introuvable : %1").arg(path));

    // §2 : ouverture en lecture seule immuable.
    //
    // immutable=1 fait sauter tout le verrouillage à SQLite : c'est le plus rapide, et le
    // seul mode qui fonctionne sur une partition montée en lecture seule. Le chemin est
    // encodé parce que la syntaxe URI donne un sens à « ? », « # » et « % ».
    const QString uri = QStringLiteral("file:%1?immutable=1")
                            .arg(QString::fromUtf8(
                                QUrl::toPercentEncoding(QFileInfo(path).absoluteFilePath(),
                                                        "/")));

    const QByteArray uriUtf8 = uri.toUtf8();
    const int        rc      = sqlite3_open_v2(uriUtf8.constData(), &m_db,
                                               SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
    if (rc != SQLITE_OK) {
        const QString message = sqliteError(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        return fail(QStringLiteral("ouverture de %1 refusée : %2").arg(path, message));
    }

    if (!readMeta(error)) {
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    if (m_meta.major != schema::kSupportedMajor) {
        const QString message =
            QStringLiteral("schéma d'export %1 : version MAJEURE %2 inconnue, ce binaire "
                           "sait lire la %3. Chargement refusé plutôt qu'interprété au "
                           "hasard.")
                .arg(m_meta.schemaVersion)
                .arg(m_meta.major)
                .arg(schema::kSupportedMajor);
        sqlite3_close(m_db);
        m_db = nullptr;
        return fail(message);
    }

    if (error)
        error->clear();
    return true;
}

bool ExportDatabase::readMeta(QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    sqlite3_stmt *stmt = prepare(m_db, QStringLiteral("SELECT key, value FROM exp_meta"));
    if (!stmt)
        return fail(QStringLiteral("exp_meta illisible : %1").arg(sqliteError(m_db)));

    m_meta = ExportMeta{};
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString key   = columnText(stmt, 0);
        const QString value = columnText(stmt, 1);

        if (key == QLatin1String("schema_version"))
            m_meta.schemaVersion = value;
        else if (key == QLatin1String("generated_at"))
            m_meta.generatedAt = value;
        else if (key == QLatin1String("games"))
            m_meta.games = value.toInt();
        else if (key == QLatin1String("platforms"))
            m_meta.platforms = value.toInt();
        else if (key == QLatin1String("rom_hashes"))
            m_meta.romHashes = value.toInt();
        else if (key == QLatin1String("arcade_romsets"))
            m_meta.arcadeRomsets = value.toInt();
        else if (key == QLatin1String("dat_sets"))
            m_meta.datSets = value.toInt();
    }
    sqlite3_finalize(stmt);

    if (m_meta.schemaVersion.isEmpty())
        return fail(QStringLiteral("exp_meta ne porte pas de schema_version : ce fichier "
                                   "n'est pas un export igiris."));

    const auto parts = m_meta.schemaVersion.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return fail(QStringLiteral("schema_version « %1 » n'est pas du SemVer")
                        .arg(m_meta.schemaVersion));

    bool okMajor = false, okMinor = false;
    m_meta.major = parts.at(0).toInt(&okMajor);
    m_meta.minor = parts.at(1).toInt(&okMinor);
    m_meta.patch = parts.size() > 2 ? parts.at(2).toInt() : 0;

    if (!okMajor || !okMinor)
        return fail(QStringLiteral("schema_version « %1 » n'est pas du SemVer")
                        .arg(m_meta.schemaVersion));

    return true;
}

std::optional<RomMatch> ExportDatabase::findByCrc(const QString &crc32,
                                                  const QString &platformKey) const
{
    if (!m_db)
        return std::nullopt;

    const QString sql = QStringLiteral(
                            "SELECT g.title, h.game_key, h.header_skip "
                            "FROM exp_rom_hash h JOIN exp_game g ON g.game_key = h.game_key "
                            "WHERE h.crc32 = ? AND h.%1 = ?")
                            .arg(platformColumn());

    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return std::nullopt;

    // Les CRC de l'export sont en majuscules : on normalise pour éviter un faux négatif.
    bindText(stmt, 1, crc32.toUpper());
    bindText(stmt, 2, platformKey);

    std::optional<RomMatch> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        RomMatch match;
        match.title      = columnText(stmt, 0);
        match.gameKey    = columnText(stmt, 1);
        match.headerSkip = sqlite3_column_int(stmt, 2);
        result           = match;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<RomsetMatch> ExportDatabase::findByRomset(const QString &romset,
                                                        const QString &platformKey) const
{
    if (!m_db)
        return std::nullopt;

    const QString sql =
        QStringLiteral("SELECT g.title, r.game_key, r.hardware, r.emulators, "
                       "r.driver_status "
                       "FROM exp_romset r JOIN exp_game g ON g.game_key = r.game_key "
                       "WHERE r.romset = ? AND r.%1 = ?")
            .arg(platformColumn());

    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return std::nullopt;

    // Le romset est un nom de fichier sans extension, en minuscules (§4).
    bindText(stmt, 1, romset.toLower());
    bindText(stmt, 2, platformKey);

    std::optional<RomsetMatch> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        RomsetMatch match;
        match.title        = columnText(stmt, 0);
        match.gameKey      = columnText(stmt, 1);
        match.hardware     = columnText(stmt, 2);
        match.emulators    = columnText(stmt, 3);
        match.driverStatus = columnText(stmt, 4);
        result             = match;
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Game> ExportDatabase::searchByName(const QString &needle, int limit) const
{
    QList<Game> games;
    if (!m_db || needle.isEmpty())
        return games;

    sqlite3_stmt *stmt = prepare(
        m_db, QStringLiteral("SELECT game_key, title, year, rating, search_key FROM exp_game "
                             "WHERE search_key LIKE '%' || ? || '%' "
                             "ORDER BY rating DESC LIMIT ?"));
    if (!stmt)
        return games;

    // search_key est normalisé côté serveur ; on compare en minuscules.
    bindText(stmt, 1, needle.toLower());
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Game game;
        game.gameKey = columnText(stmt, 0);
        game.title   = columnText(stmt, 1);
        game.year    = sqlite3_column_int(stmt, 2);
        game.rating    = sqlite3_column_int(stmt, 3);
        game.searchKey = columnText(stmt, 4);
        games.append(game);
    }
    sqlite3_finalize(stmt);
    return games;
}

QList<Game> ExportDatabase::allGames() const
{
    QList<Game> games;
    if (!m_db)
        return games;

    sqlite3_stmt *stmt = prepare(m_db,
                                 QStringLiteral("SELECT game_key, title, year, rating, "
                                                "search_key FROM exp_game ORDER BY title"));
    if (!stmt)
        return games;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Game game;
        game.gameKey   = columnText(stmt, 0);
        game.title     = columnText(stmt, 1);
        game.year      = sqlite3_column_int(stmt, 2);
        game.rating    = sqlite3_column_int(stmt, 3);
        game.searchKey = columnText(stmt, 4);
        games.append(game);
    }
    sqlite3_finalize(stmt);
    return games;
}

std::optional<Game> ExportDatabase::gameByKey(const QString &gameKey) const
{
    if (!m_db)
        return std::nullopt;

    sqlite3_stmt *stmt = prepare(m_db, QStringLiteral("SELECT game_key, title, year, "
                                                      "rating, search_key FROM exp_game "
                                                      "WHERE game_key = ?"));
    if (!stmt)
        return std::nullopt;
    bindText(stmt, 1, gameKey);

    std::optional<Game> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Game game;
        game.gameKey   = columnText(stmt, 0);
        game.title     = columnText(stmt, 1);
        game.year      = sqlite3_column_int(stmt, 2);
        game.rating    = sqlite3_column_int(stmt, 3);
        game.searchKey = columnText(stmt, 4);
        result         = game;
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<GamePlatform> ExportDatabase::platformsForGame(const QString &gameKey) const
{
    QList<GamePlatform> platforms;
    if (!m_db)
        return platforms;

    const QString sql = QStringLiteral(
                            "SELECT display_name, %1, emu_score, is_preferred "
                            "FROM exp_game_platform WHERE game_key = ? "
                            "ORDER BY is_preferred DESC, emu_score DESC")
                            .arg(platformColumn());

    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return platforms;

    bindText(stmt, 1, gameKey);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GamePlatform platform;
        platform.displayName = columnText(stmt, 0);
        platform.platformKey = columnText(stmt, 1); // NULL possible : plateforme non émulée
        platform.emuScore    = sqlite3_column_int(stmt, 2);
        platform.isPreferred = sqlite3_column_int(stmt, 3) != 0;
        platforms.append(platform);
    }
    sqlite3_finalize(stmt);
    return platforms;
}

QStringList ExportDatabase::allPlatformKeys() const
{
    QStringList keys;
    if (!m_db)
        return keys;

    const QString sql = QStringLiteral("SELECT DISTINCT %1 FROM exp_game_platform "
                                       "WHERE %1 IS NOT NULL ORDER BY %1")
                            .arg(platformColumn());

    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return keys;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        keys.append(columnText(stmt, 0));

    sqlite3_finalize(stmt);
    return keys;
}

QHash<QString, QStringList> ExportDatabase::platformKeysByGame() const
{
    QHash<QString, QStringList> byGame;
    if (!m_db)
        return byGame;

    const QString sql = QStringLiteral("SELECT game_key, %1 FROM exp_game_platform "
                                       "WHERE %1 IS NOT NULL")
                            .arg(platformColumn());
    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return byGame;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString gameKey     = columnText(stmt, 0);
        const QString platformKey = columnText(stmt, 1);
        // Dédoublonnage : exp_game_platform a pour clé (game_key, display_name), donc
        // « SNES » et « SFAM » produisent deux lignes pour la même clé de plateforme.
        QStringList &keys = byGame[gameKey];
        if (!keys.contains(platformKey))
            keys.append(platformKey);
    }
    sqlite3_finalize(stmt);
    return byGame;
}

QStringList ExportDatabase::arcadePlatformKeys() const
{
    QStringList keys;
    if (!m_db)
        return keys;

    const QString sql = QStringLiteral("SELECT DISTINCT %1 FROM exp_romset ORDER BY %1")
                            .arg(platformColumn());
    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return keys;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        keys.append(columnText(stmt, 0));
    sqlite3_finalize(stmt);
    return keys;
}

QHash<QString, int> ExportDatabase::headerSkipByPlatform() const
{
    QHash<QString, int> skips;
    if (!m_db)
        return skips;

    const QString sql = QStringLiteral("SELECT %1, MAX(header_skip) FROM exp_rom_hash "
                                       "WHERE header_skip > 0 GROUP BY %1")
                            .arg(platformColumn());
    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return skips;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        skips.insert(columnText(stmt, 0), sqlite3_column_int(stmt, 1));
    sqlite3_finalize(stmt);
    return skips;
}

QList<RomHash> ExportDatabase::romHashesForGame(const QString &gameKey) const
{
    QList<RomHash> hashes;
    if (!m_db)
        return hashes;

    const QString sql = QStringLiteral("SELECT crc32, %1, header_skip FROM exp_rom_hash "
                                       "WHERE game_key = ? ORDER BY %1, crc32")
                            .arg(platformColumn());

    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return hashes;

    bindText(stmt, 1, gameKey);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RomHash hash;
        hash.crc32       = columnText(stmt, 0);
        hash.platformKey = columnText(stmt, 1);
        hash.headerSkip  = sqlite3_column_int(stmt, 2);
        hashes.append(hash);
    }
    sqlite3_finalize(stmt);
    return hashes;
}

QList<Romset> ExportDatabase::romsetsForGame(const QString &gameKey) const
{
    QList<Romset> romsets;
    if (!m_db)
        return romsets;

    const QString sql =
        QStringLiteral("SELECT romset, %1, hardware, emulators, driver_status "
                       "FROM exp_romset WHERE game_key = ? ORDER BY romset")
            .arg(platformColumn());

    sqlite3_stmt *stmt = prepare(m_db, sql);
    if (!stmt)
        return romsets;

    bindText(stmt, 1, gameKey);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Romset romset;
        romset.romset       = columnText(stmt, 0);
        romset.platformKey  = columnText(stmt, 1);
        romset.hardware     = columnText(stmt, 2);
        romset.emulators    = columnText(stmt, 3);
        romset.driverStatus = columnText(stmt, 4);
        romsets.append(romset);
    }
    sqlite3_finalize(stmt);
    return romsets;
}

} // namespace igiris::catalog
