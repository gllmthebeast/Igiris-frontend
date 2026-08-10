#pragma once

// Chargeur de l'export SQLite — CLAUDE.md §2 et §3.
//
// La façade du §9.1 : à partir d'ici et vers le haut, le vocabulaire est « platformKey ».
// Le nom réel de la colonne ne vit que dans ExportSchema.h.
//
// L'export est ouvert en LECTURE SEULE IMMUABLE. Jamais de WAL, jamais d'écriture.

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

struct sqlite3;

namespace igiris::catalog {

// Clé composite d'une ROM du catalogue : un CRC seul n'identifie rien, c'est le couple
// (CRC, plateforme) qui est la clé de exp_rom_hash comme de exp_game_language.
// Définie ici pour que le scan et le catalogue parlent de la MÊME clé — deux conventions
// divergentes se seraient croisées sans jamais se rencontrer, silencieusement.
inline QString romKey(const QString &crc32, const QString &platformKey)
{
    return crc32.toUpper() + QLatin1Char('\x1f') + platformKey;
}

struct ExportMeta {
    QString schemaVersion; // « 1.3.0 »
    int     major = 0;
    int     minor = 0;
    int     patch = 0;
    QString generatedAt;
    int     games         = 0;
    int     platforms     = 0;
    int     romHashes     = 0;
    int     arcadeRomsets = 0;
    int     datSets       = 0;
    // Présents à partir de l'export 1.4.0 seulement ; 0 avant, ce qui n'est pas une erreur.
    int     languages     = 0;
    int     gameLanguages = 0;
};

struct Game {
    QString gameKey;
    QString title;
    // Nom NORMALISÉ, pour la recherche locale — jamais à afficher (§3).
    QString searchKey;
    int     year   = 0;
    int     rating = 0;
};

// Une plateforme sur laquelle le jeu existe. `platformKey` est vide quand l'export ne
// désigne aucune cible d'émulation : la plateforme d'origine est affichable, mais elle
// n'est pas lançable. Ne pas la confondre avec un statut noir.
struct GamePlatform {
    QString platformKey;
    QString displayName;
    int     emuScore    = 0;
    bool    isPreferred = false;

    bool isEmulationTarget() const { return !platformKey.isEmpty(); }
};

struct RomMatch {
    QString gameKey;
    QString title;
    int     headerSkip = 0;
};

// Une entrée d'index de hash, vue depuis le jeu.
struct RomHash {
    QString crc32;
    QString platformKey;
    int     headerSkip = 0;
};

struct Romset {
    QString romset;
    QString platformKey;
    QString hardware;
    QString emulators;
    QString driverStatus;
};

struct RomsetMatch {
    QString gameKey;
    QString title;
    QString hardware;
    QString emulators;
    QString driverStatus;
};

// Une langue du référentiel — export 1.4.0, §8.
//
// `bitIndex` vaut -1 quand l'export ne lui attribue AUCUN bit. Ce n'est pas une erreur :
// le backend ne donne un bit qu'aux langues affichables, et le §4 de sa réponse est
// explicite — « traitez NULL comme pas de bit, pas comme le bit 0 ». Une langue sans bit
// reste dans exp_game_language, donc visible en fiche, mais absente de lang_mask.
struct Language {
    QString code;  // ISO 639-1 minuscule, tel que l'export le donne
    QString label; // libellé affichable
    QString badgeAsset;
    int     bitIndex = -1;

    bool     hasBit() const { return bitIndex >= 0; }
    quint64  bit() const { return hasBit() ? (quint64(1) << bitIndex) : 0; }
};

// Quelle ROM apporte quelle langue (§8). Même granularité que exp_rom_hash : c'est ce qui
// permet de décider illuminé / grisé sans rien recalculer.
struct GameLanguage {
    QString langCode;
    QString platformKey;
    QString crc32;
};

class ExportDatabase
{
public:
    ExportDatabase() = default;
    ~ExportDatabase();

    ExportDatabase(const ExportDatabase &)            = delete;
    ExportDatabase &operator=(const ExportDatabase &) = delete;

    // Ouvre l'export et contrôle sa version. Refuse une MAJEURE inconnue plutôt que de
    // casser en silence (§2). `error` reçoit le message complet (§15).
    bool open(const QString &path, QString *error);
    bool isOpen() const { return m_db != nullptr; }

    const ExportMeta &meta() const { return m_meta; }

    // §3, requête 1 — identifier un fichier de console par son CRC.
    std::optional<RomMatch> findByCrc(const QString &crc32, const QString &platformKey) const;

    // §3, requête 2 — identifier un jeu d'arcade par son nom de romset.
    std::optional<RomsetMatch> findByRomset(const QString &romset,
                                            const QString &platformKey) const;

    // §3, requête 3 — recherche par nom. `needle` est comparé au search_key normalisé.
    QList<Game> searchByName(const QString &needle, int limit = 50) const;

    // Tout le catalogue, trié par titre. C'est ce que charge la liste de l'accueil : le
    // §0 impose d'afficher TOUS les jeux, ROM présente ou non.
    QList<Game> allGames() const;

    // Un jeu par sa clé. La fiche en a besoin : elle reçoit une clé, pas un titre.
    std::optional<Game> gameByKey(const QString &gameKey) const;

    // §3, requête 4 — meilleure version d'un jeu, et sa fidélité d'émulation.
    QList<GamePlatform> platformsForGame(const QString &gameKey) const;

    // Toutes les clés de plateforme émulables connues de l'export. Croisée avec les
    // systèmes locaux, elle donne le statut noir du §7.
    QStringList allPlatformKeys() const;

    // Clés de plateforme de chaque jeu, en une seule requête. Le filtre « plateforme » du
    // §6 est STATIQUE : il se résout sur un index de l'export, sans toucher au disque.
    QHash<QString, QStringList> platformKeysByGame() const;

    // Plateformes identifiées par NOM DE ROMSET et non par CRC (§4). Déduit de l'export
    // lui-même plutôt que d'une liste en dur : si le backend ajoute une plateforme
    // d'arcade, le scanner la traite sans qu'on touche au code.
    QStringList arcadePlatformKeys() const;

    // Octets d'en-tête à ignorer, par plateforme. Vient également de l'export : aujourd'hui
    // nes=16, atari7800=128, lynx=64. Aucune table codée en dur ici.
    QHash<QString, int> headerSkipByPlatform() const;

    // Sens inverse des requêtes 1 et 2 : quelles ROMs fournissent ce jeu.
    // La fiche de jeu (§7) en a besoin pour dire quelle release apporte quoi.
    QList<RomHash> romHashesForGame(const QString &gameKey) const;
    QList<Romset>  romsetsForGame(const QString &gameKey) const;

    // --- langues (§8) — export 1.4.0 -----------------------------------------------------
    //
    // Détecté à l'ouverture, et sur la PRÉSENCE DES TABLES plutôt que sur le numéro de
    // version mineure : c'est la seule vérification qui reste vraie si l'ordre des
    // livraisons change. Un export 1.3.0 reste lisible, sans badges (§2 : les mineures
    // sont additives, elles ne cassent pas ce binaire).
    bool hasLanguages() const { return m_hasLanguages; }

    // Le référentiel, ordonné par bit_index — les langues sans bit à la fin.
    QList<Language> languages() const;

    // Filtre STATIQUE « existe en <langue> » : lu directement dans exp_game.lang_mask,
    // une ligne par jeu. C'est précisément ce à quoi le masque sert (§8) — passer par
    // exp_game_language coûterait douze fois plus de lignes pour le même résultat.
    QHash<QString, quint64> langMaskByGame() const;

    // Filtre DYNAMIQUE « jouable en <langue> » : le même masque, restreint aux langues
    // qu'une ROM RÉELLEMENT POSSÉDÉE fournit. `ownedRomKeys` contient des romKey().
    //
    // Un seul parcours de exp_game_language, et non une requête par ROM possédée : la clé
    // primaire de cette table commence par game_key, donc chercher par crc32 y déclencherait
    // un balayage complet — autant n'en faire qu'un.
    QHash<QString, quint64> ownedLangMaskByGame(const QSet<QString> &ownedRomKeys) const;

    // Le détail par plateforme de la fiche de jeu (§7).
    QList<GameLanguage> languagesForGame(const QString &gameKey) const;

private:
    bool readMeta(QString *error);
    bool detectLanguageTables() const;

    sqlite3   *m_db = nullptr;
    ExportMeta m_meta;
    bool       m_hasLanguages = false;
};

} // namespace igiris::catalog
