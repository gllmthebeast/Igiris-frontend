#pragma once

// Cache de scan — CLAUDE.md §4 : « le scan doit être incrémental, pas de rehash complet à
// chaque démarrage. Cache indexé par chemin + taille + date de modification. »
//
// Sur un Pi, rehasher une collection entière à chaque lancement est le genre de coût qui
// rend un frontend inutilisable. Les trois clés ensemble suffisent : un fichier dont le
// chemin, la taille ET la date n'ont pas bougé n'a pas changé en pratique.
//
// C'est la SEULE base de ce projet ouverte en écriture. L'export, lui, ne l'est jamais.

#include <QString>
#include <optional>

struct sqlite3;

namespace igiris::scan {

class ScanCache
{
public:
    ScanCache() = default;
    ~ScanCache();

    ScanCache(const ScanCache &)            = delete;
    ScanCache &operator=(const ScanCache &) = delete;

    bool open(const QString &path, QString *error);
    bool isOpen() const { return m_db != nullptr; }

    // CRC mémorisé si — et seulement si — taille et date concordent.
    std::optional<QString> lookup(const QString &path, qint64 size, qint64 mtime) const;

    void store(const QString &path, qint64 size, qint64 mtime, const QString &crc32);

    int count() const;

private:
    sqlite3 *m_db = nullptr;
};

} // namespace igiris::scan
