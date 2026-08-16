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
struct sqlite3_stmt;

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
    // URL de jaquette. AJOUTÉE EN FIN de structure volontairement : les tests construisent
    // des Game par initialisation agrégée, et l'insérer au milieu aurait décalé leurs
    // champs en silence — le compilateur n'aurait rien dit, year serait devenu rating.
    //
    // C'est la SEULE donnée du catalogue qui exige le réseau (§11). Vide pour un jeu sans
    // jaquette connue : 1 jeu sur 7 581 dans l'export actuel.
    QString coverRef;

    // --- export 1.5.0 / 1.6.0 -----------------------------------------------------------
    //
    // Ajoutés EN FIN, pour la même raison que coverRef ci-dessus : les tests construisent
    // des Game par initialisation agrégée. Cette règle a déjà sauvé une fois, et elle est
    // exactement celle que le backend a dû s'appliquer à lui-même après avoir inséré une
    // colonne au milieu de son SELECT — lang_mask était parti dans rating, sur tout le
    // catalogue et sans la moindre erreur.

    // ILLUSTRATION de bandeau (1.5.0). Horizontale et composée, SANS texte de pochette :
    // ce n'est pas une jaquette en plus grand. Vide sur 4,4 % du catalogue, auquel cas la
    // fiche retombe sur la jaquette en la sachant (voir GameDetailModel::hasRealBanner).
    QString artworkRef;

    // SYNOPSIS (1.6.0). TOUJOURS EN ANGLAIS — IGDB n'en fournit pas de traduit, et le
    // backend a délibérément écarté une colonne summary_lang qui aurait porté « en » sur
    // 100 % des lignes. Renseigné sur 99,7 % du catalogue.
    QString summary;

    // MODES DE JEU (1.6.0), masque de bits sur le même patron que lang_mask — voir
    // GameMode. Remplace le « players » demandé au format « 1-4 », que la source ne
    // couvrait qu'à 12 % : le nombre exact de joueurs n'est pas atteignable, le filtre
    // « jouable à plusieurs » l'est à 97 %.
    quint64 modeMask = 0;

    // LANGUES DU CATALOGUE (1.7.0), d'après IGDB — audio et sous-titres seulement.
    //
    // ⚠️ NE JAMAIS FUSIONNER AVEC lang_mask. Les deux masques emploient le MÊME registre
    // de bits, ce qui rend la confusion facile et silencieuse, mais ils ne disent pas la
    // même chose :
    //
    //   lang_mask        « une ROM du catalogue fournit cette langue » → peut s'illuminer
    //   langCatalogMask  « le jeu EXISTE dans cette langue »           → ne le peut JAMAIS
    //
    // IGDB ne connaît ni release ni CRC : il n'existe aucun hash auquel rattacher une
    // langue venue de là. L'ajouter au masque qui pilote les badges produirait des badges
    // définitivement gris, que l'utilisateur ne pourrait allumer quoi qu'il télécharge —
    // alors que le §8 promet deux états et pas trois.
    //
    // Sert donc le filtre statique « existe en <langue> », et rien d'autre en vue liste.
    // 8 121 jeux sur 17 260 (47 %), dont 6 879 où les dats sont muets.
    quint64 langCatalogMask = 0;
};

// Une plateforme sur laquelle le jeu existe. `platformKey` est vide quand l'export ne
// désigne aucune cible d'émulation : la plateforme d'origine est affichable, mais elle
// n'est pas lançable. Ne pas la confondre avec un statut noir.
struct GamePlatform {
    QString platformKey;
    QString displayName;
    // Fidélité d'émulation 0..100 (§5), ou -1 quand l'export n'en donne pas.
    //
    // -1 et non 0 : depuis le 1.7.0 le catalogue porte des plateformes NON ÉMULABLES —
    // PC, PS4, Switch — sur lesquelles emu_score vaut NULL, parce qu'un taux de fidélité
    // d'émulation n'y a aucun sens. « 0 » s'y lirait « émulation catastrophique », ce qui
    // est faux et pire qu'une absence. 40 511 lignes sur 59 066 sont dans ce cas.
    int     emuScore    = -1;
    bool    isPreferred = false;

    bool hasEmuScore() const { return emuScore >= 0; }

    // Année de sortie SUR CETTE PLATEFORME (export 1.5.0), et non celle du jeu. 0 quand
    // l'export ne la porte pas.
    //
    // Ce n'est pas un doublon de Game::year, qui ne connaît que la PREMIÈRE sortie du
    // titre : 7 711 lignes sur 18 555 portent une année différente. La fiche affiche
    // justement ces plateformes côte à côte, et ne pouvait en dater aucune.
    int releaseYear = 0;

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

// Un mode de jeu du référentiel — export 1.6.0.
//
// Même patron qu'exp_language, DÉLIBÉRÉMENT : table de référence, bit_index attribué à
// vie, masque sur exp_game, filtrage par ET binaire. Le code de filtre des langues se
// réutilise donc tel quel.
//
// Une différence tient : ici `bitIndex` n'est jamais NULL — les six modes en portent un.
// On garde malgré tout la même convention défensive que Language, parce qu'un mode ajouté
// plus tard sans bit ne doit pas devenir le bit 0 par accident.
struct GameMode {
    QString key;   // « solo », « multi », « coop »…
    QString label; // libellé affichable, fourni par l'export
    int     bitIndex = -1;

    bool    hasBit() const { return bitIndex >= 0; }
    quint64 bit() const { return hasBit() ? (quint64(1) << bitIndex) : 0; }
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

    // --- modes de jeu — export 1.6.0 -----------------------------------------------------
    //
    // Détecté comme les langues : sur la présence de la table ET de la colonne, pas sur le
    // numéro de mineure.
    bool hasModes() const { return m_hasModes; }

    // Vrai à partir de l'export 1.7.0. L'interface s'en sert pour dire à l'utilisateur que
    // le filtre « existe en <langue> » couvre le catalogue et pas seulement les ROMs.
    bool hasCatalogLanguages() const { return m_hasCatalogLanguages; }

    // Le référentiel complet, ordonné par bit_index. Le backend le livre ENTIER et non
    // limité aux modes observés : le menu de filtres se construit donc sans dépendre du
    // contenu du catalogue.
    QList<GameMode> gameModes() const;

    // Pas de modeMaskByGame() en pendant de langMaskByGame() : le masque de modes voyage
    // déjà dans Game::modeMask, parce que la fiche l'affiche autant que le filtre s'en
    // sert. lang_mask, lui, n'a jamais eu besoin de descendre jusqu'au Game.
    //
    // Et il n'y aura jamais d'équivalent « possédé » : un mode de jeu est une propriété du
    // TITRE, pas de la ROM. C'est ce qui le distingue d'une langue, qui varie d'une release
    // à l'autre — donc un seul filtre ici, là où la langue en a deux.

private:
    bool readMeta(QString *error);
    bool detectLanguageTables() const;

    // Vrai si `table` porte bien `column`. Sert à dégrader proprement sur un export plus
    // ancien : les mineures sont additives (§2), donc un binaire récent doit lire un vieil
    // export sans broncher — simplement sans les colonnes qui n'existaient pas.
    bool hasColumn(const QString &table, const QString &column) const;

    // Le nom de colonne, ou « NULL » quand l'export ne la porte pas. Émis dans le SELECT
    // pour que le NOMBRE et l'ORDRE des colonnes lues soient les MÊMES quelle que soit la
    // version de l'export — donc que les indices de lecture restent des constantes.
    //
    // C'est exactement le piège dans lequel le générateur est tombé en 1.5.0 : une colonne
    // insérée au milieu, des indices positionnels décalés, et lang_mask à 0 sur tout le
    // catalogue sans une seule erreur. Ici la position ne peut pas bouger.
    QString columnOrNull(bool present, const QString &column) const;

    // La liste de colonnes d'exp_game, et la lecture d'une ligne, écrites UNE fois pour
    // les trois requêtes qui produisent des Game (recherche, catalogue entier, jeu par
    // clé). Trois copies de la même énumération, c'est trois occasions de les désaccorder
    // à la prochaine colonne — et le désaccord serait muet.
    QString     gameColumns() const;
    static Game readGame(sqlite3_stmt *stmt);

    sqlite3   *m_db = nullptr;
    ExportMeta m_meta;
    bool       m_hasLanguages = false;
    bool       m_hasArtwork   = false;
    bool       m_hasSummary   = false;
    bool       m_hasReleaseYear = false;
    bool       m_hasModes       = false;
    bool       m_hasCatalogLanguages = false;
};

} // namespace igiris::catalog
