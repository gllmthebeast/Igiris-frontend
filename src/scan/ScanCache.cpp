#include "scan/ScanCache.h"

#include <sqlite3.h>

#include <QDir>
#include <QFileInfo>

namespace igiris::scan {

namespace {

void bindText(sqlite3_stmt *stmt, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(stmt, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

} // namespace

ScanCache::~ScanCache()
{
    if (m_db)
        sqlite3_close(m_db);
}

bool ScanCache::open(const QString &path, QString *error)
{
    const auto fail = [error, this](const QString &message) {
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        if (error)
            *error = message;
        return false;
    };

    QDir().mkpath(QFileInfo(path).absolutePath());

    if (sqlite3_open_v2(path.toUtf8().constData(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)
        != SQLITE_OK) {
        return fail(QStringLiteral("cache de scan inutilisable (%1) : %2")
                        .arg(path, m_db ? QString::fromUtf8(sqlite3_errmsg(m_db))
                                        : QStringLiteral("échec d'ouverture")));
    }

    char *errmsg = nullptr;
    if (sqlite3_exec(m_db,
                     "CREATE TABLE IF NOT EXISTS scan_cache("
                     "  path TEXT PRIMARY KEY,"
                     "  size INTEGER NOT NULL,"
                     "  mtime INTEGER NOT NULL,"
                     "  crc32 TEXT NOT NULL)",
                     nullptr, nullptr, &errmsg)
        != SQLITE_OK) {
        const QString message = QString::fromUtf8(errmsg ? errmsg : "création de table refusée");
        sqlite3_free(errmsg);
        return fail(message);
    }

    if (error)
        error->clear();
    return true;
}

std::optional<QString> ScanCache::lookup(const QString &path, qint64 size, qint64 mtime) const
{
    if (!m_db)
        return std::nullopt;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
                           "SELECT crc32 FROM scan_cache WHERE path = ? AND size = ? AND mtime = ?",
                           -1, &stmt, nullptr)
        != SQLITE_OK)
        return std::nullopt;

    bindText(stmt, 1, path);
    sqlite3_bind_int64(stmt, 2, size);
    sqlite3_bind_int64(stmt, 3, mtime);

    std::optional<QString> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(stmt, 0);
        if (text)
            result = QString::fromUtf8(reinterpret_cast<const char *>(text));
    }
    sqlite3_finalize(stmt);
    return result;
}

void ScanCache::store(const QString &path, qint64 size, qint64 mtime, const QString &crc32)
{
    if (!m_db)
        return;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO scan_cache(path, size, mtime, crc32) VALUES(?,?,?,?) "
                           "ON CONFLICT(path) DO UPDATE SET size=excluded.size, "
                           "mtime=excluded.mtime, crc32=excluded.crc32",
                           -1, &stmt, nullptr)
        != SQLITE_OK)
        return;

    bindText(stmt, 1, path);
    sqlite3_bind_int64(stmt, 2, size);
    sqlite3_bind_int64(stmt, 3, mtime);
    bindText(stmt, 4, crc32);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int ScanCache::count() const
{
    if (!m_db)
        return 0;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM scan_cache", -1, &stmt, nullptr)
        != SQLITE_OK)
        return 0;

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        total = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return total;
}

} // namespace igiris::scan
