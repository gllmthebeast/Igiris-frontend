#include "ui/GameListModel.h"

#include <QLocale>
#include <QVariantMap>

#include <algorithm>

namespace igiris::ui {

GameListModel::GameListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // « fr_FR » → « fr ». Sert uniquement à ORDONNER les badges : aucune langue n'est
    // déduite de la locale, et surtout aucune n'est masquée à cause d'elle.
    m_interfaceLanguage = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
}

void GameListModel::setCatalogue(QList<catalog::Game>        games,
                                 QHash<QString, QStringList> platformsByGame,
                                 QStringList                 arcadePlatformKeys)
{
    const QSet<QString> arcade(arcadePlatformKeys.cbegin(), arcadePlatformKeys.cend());

    beginResetModel();
    m_games.clear();
    m_games.reserve(games.size());

    QSet<QString> platforms;
    QSet<int>     decades;

    for (catalog::Game &game : games) {
        Entry entry;
        entry.platformKeys = platformsByGame.value(game.gameKey);
        // Un jeu est « arcade » dès qu'une de ses plateformes s'identifie par nom de
        // romset. La liste vient de l'export, jamais d'une énumération codée ici (§4).
        entry.isArcade = std::any_of(entry.platformKeys.cbegin(), entry.platformKeys.cend(),
                                     [&arcade](const QString &key) {
                                         return arcade.contains(key);
                                     });
        entry.decade = game.year > 0 ? (game.year / 10) * 10 : 0;
        entry.game   = std::move(game);

        for (const QString &key : entry.platformKeys)
            platforms.insert(key);
        if (entry.decade > 0)
            decades.insert(entry.decade);

        m_games.append(std::move(entry));
    }
    applyLanguageMasks();
    applyAliases();
    endResetModel();

    m_availablePlatforms = QStringList(platforms.cbegin(), platforms.cend());
    m_availablePlatforms.sort();

    m_availableDecades = QList<int>(decades.cbegin(), decades.cend());
    std::sort(m_availableDecades.begin(), m_availableDecades.end());

    emit catalogueChanged();
    rebuild();
}

void GameListModel::setOwnedGameKeys(QSet<QString> owned)
{
    m_owned = std::move(owned);

    // Le filtre dynamique n'existe qu'à partir du moment où un scan a eu lieu. Un scan
    // qui ne trouve rien reste un scan : c'est l'information « tu ne possèdes rien », pas
    // « le filtre est indisponible ».
    if (!m_ownershipAvailable) {
        m_ownershipAvailable = true;
        emit ownershipAvailableChanged();
    }
    rebuild();
}

void GameListModel::setLanguages(QList<catalog::Language> languages,
                                 QHash<QString, quint64>  maskByGame)
{
    m_languages      = std::move(languages);
    m_langMaskByGame = std::move(maskByGame);

    m_languageByBit.clear();
    m_availableLanguages.clear();
    for (int i = 0; i < m_languages.size(); ++i) {
        const catalog::Language &language = m_languages.at(i);
        if (!language.hasBit())
            continue; // sans bit : filtrable en fiche, mais hors masque (§8)
        m_languageByBit.insert(language.bitIndex, i);
        m_availableLanguages.append(language.code);
    }

    beginResetModel();
    applyLanguageMasks();
    endResetModel();

    recomputeRequiredMask();
    emit languagesChanged();
    rebuild();
}

void GameListModel::setOwnedLanguageMasks(QHash<QString, quint64> maskByGame)
{
    m_ownedLangMaskByGame = std::move(maskByGame);

    beginResetModel();
    applyLanguageMasks();
    endResetModel();

    // Comme pour la possession : un scan qui ne trouve aucune langue reste un scan. C'est
    // l'information « aucune ROM possédée ne fournit cette langue », pas « indisponible ».
    if (!m_ownedLanguagesAvailable) {
        m_ownedLanguagesAvailable = true;
        emit ownedLanguagesAvailableChanged();
    }
    rebuild();
}

void GameListModel::setAliases(QHash<QString, QList<catalog::GameAlias>> aliasesByGame)
{
    m_aliasesByGame = std::move(aliasesByGame);

    beginResetModel();
    applyAliases();
    endResetModel();

    rebuild();
}

void GameListModel::applyAliases()
{
    for (Entry &entry : m_games) {
        entry.aliases      = m_aliasesByGame.value(entry.game.gameKey);
        entry.matchedAlias = -1;
    }
}

void GameListModel::applyLanguageMasks()
{
    for (Entry &entry : m_games) {
        entry.langMask      = m_langMaskByGame.value(entry.game.gameKey, 0);
        entry.ownedLangMask = m_ownedLangMaskByGame.value(entry.game.gameKey, 0);
        // Garde-fou : une langue possédée qui ne serait pas au catalogue signifierait que
        // les deux masques ne parlent pas du même référentiel de bits. On ne peut pas
        // l'afficher — un badge illuminé sans badge correspondant est un badge faux.
        entry.ownedLangMask &= entry.langMask;
    }
}

void GameListModel::recomputeRequiredMask()
{
    m_requiredLangMask = 0;
    m_unfilterableLanguages.clear();

    for (const QString &code : m_languageFilter) {
        const quint64 before = m_requiredLangMask;
        for (const catalog::Language &language : m_languages) {
            if (language.code == code) {
                m_requiredLangMask |= language.bit();
                break;
            }
        }
        // Rien n'a été allumé : code inconnu du référentiel, ou connu mais sans bit. Dans
        // les deux cas le masque ne peut pas porter la contrainte, et le silence serait
        // le pire des comportements — c'est exactement le décalage de bits « silencieux »
        // contre lequel le §8 met en garde, vu de l'autre côté.
        if (m_requiredLangMask == before)
            m_unfilterableLanguages.append(code);
    }
}

QString GameListModel::languageLabel(const QString &code) const
{
    for (const catalog::Language &language : m_languages) {
        if (language.code == code)
            return language.label;
    }
    return code;
}

QString GameListModel::modeLabel(const QString &key) const
{
    for (const catalog::GameMode &mode : m_modes) {
        if (mode.key == key)
            return mode.label;
    }
    return key;
}

void GameListModel::setGameModes(QList<catalog::GameMode> modes)
{
    m_modes = std::move(modes);

    m_availableModes.clear();
    for (const catalog::GameMode &mode : m_modes) {
        // Un mode sans bit ne serait pas filtrable : le proposer au menu donnerait un
        // filtre qui ne retient rien tout en paraissant marcher. Aucun n'est dans ce cas
        // aujourd'hui — les six en portent un — mais un mode ajouté plus tard le pourrait.
        if (mode.hasBit())
            m_availableModes.append(mode.key);
    }

    // Le filtre en cours peut désigner un mode que ce référentiel ne connaît pas : on le
    // recalcule plutôt que de le garder tel quel.
    setModeFilter(m_modeFilter);
    emit modesChanged();
}

void GameListModel::setModeFilter(const QStringList &keys)
{
    quint64 required = 0;
    for (const QString &key : keys) {
        for (const catalog::GameMode &mode : m_modes) {
            if (mode.key == key && mode.hasBit())
                required |= mode.bit();
        }
    }

    if (m_modeFilter == keys && m_requiredModeMask == required)
        return;

    m_modeFilter       = keys;
    m_requiredModeMask = required;
    emit modeFilterChanged();
    rebuild();
}

void GameListModel::setLanguageFilter(const QStringList &codes)
{
    if (m_languageFilter == codes)
        return;
    m_languageFilter = codes;
    recomputeRequiredMask();
    emit languageFilterChanged();
    rebuild();
}

void GameListModel::setLanguageOwnedOnly(bool ownedOnly)
{
    if (m_languageOwnedOnly == ownedOnly)
        return;
    m_languageOwnedOnly = ownedOnly;
    emit languageOwnedOnlyChanged();
    rebuild();
}

void GameListModel::setCoversEnabled(bool enabled)
{
    if (m_coversEnabled == enabled)
        return;
    m_coversEnabled = enabled;
    emit coversEnabledChanged();
    // Aucun rebuild : les jaquettes ne filtrent rien, elles s'affichent ou non.
}

void GameListModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void GameListModel::setPlatformFilter(const QString &platformKey)
{
    if (m_platformFilter == platformKey)
        return;
    m_platformFilter = platformKey;
    emit platformFilterChanged();
    rebuild();
}

void GameListModel::setDecadeFilter(int decade)
{
    if (m_decadeFilter == decade)
        return;
    m_decadeFilter = decade;
    emit decadeFilterChanged();
    rebuild();
}

void GameListModel::setArcadeOnly(bool only)
{
    if (m_arcadeOnly == only)
        return;
    m_arcadeOnly = only;
    emit arcadeOnlyChanged();
    rebuild();
}

void GameListModel::setOwnership(int ownership)
{
    if (m_ownership == ownership)
        return;
    m_ownership = ownership;
    emit ownershipChanged();
    rebuild();
}

void GameListModel::clearFilters()
{
    setFilter(QString());
    setPlatformFilter(QString());
    setDecadeFilter(0);
    setArcadeOnly(false);
    setOwnership(AnyOwnership);
    setLanguageFilter({});
    setLanguageOwnedOnly(false);
    setModeFilter({});
}

bool GameListModel::matches(const Entry &entry) const
{
    // Recherche : sur search_key, le nom normalisé côté serveur — jamais sur le titre (§3).
    entry.matchedAlias = -1;
    const QString needle = m_filter.trimmed().toLower();
    if (!needle.isEmpty() && !entry.game.searchKey.contains(needle)) {
        // Le titre ne mord pas : les AUTRES noms du jeu ont leur chance (export 1.8.0).
        //
        // Testés seulement ici, donc le cas courant — un titre qui correspond — ne coûte
        // rien de plus. Les clés sont normalisées par le serveur avec exactement la même
        // fonction que search_key : l'appareil ne normalise rien (§0).
        // L'alias EXACT l'emporte sur celui qui contient seulement la saisie. Un jeu porte
        // souvent « LTTP » et « TLoZ: ALttP » : taper « lttp » doit montrer le premier,
        // pas celui qui l'englobe par hasard.
        int found = -1;
        for (int i = 0; i < entry.aliases.size(); ++i) {
            if (!entry.aliases.at(i).key.contains(needle))
                continue;
            if (entry.aliases.at(i).key == needle) {
                found = i;
                break;
            }
            if (found < 0)
                found = i;
        }
        if (found < 0)
            return false;
        // Mémorisé pour que la ligne puisse DIRE pourquoi elle est là. Une ligne trouvée
        // par « lttp » n'affiche aucun des caractères tapés dans son titre : sans cette
        // explication, la recherche a l'air de renvoyer n'importe quoi.
        entry.matchedAlias = found;
    }

    if (!m_platformFilter.isEmpty() && !entry.platformKeys.contains(m_platformFilter))
        return false;

    if (m_decadeFilter > 0 && entry.decade != m_decadeFilter)
        return false;

    if (m_arcadeOnly && !entry.isArcade)
        return false;

    if (m_ownership != AnyOwnership) {
        // Sans scan, le filtre dynamique ne peut rien affirmer : on ne filtre pas plutôt
        // que d'affirmer à tort que tout est manquant.
        if (!m_ownershipAvailable)
            return true;
        const bool owned = m_owned.contains(entry.game.gameKey);
        if (m_ownership == OwnedOnly && !owned)
            return false;
        if (m_ownership == MissingOnly && owned)
            return false;
    }

    if (m_requiredLangMask != 0) {
        // « jouable en X » n'a pas de sens tant qu'aucun scan n'a eu lieu : on ne filtre
        // pas, plutôt que d'affirmer à tort que rien n'est jouable — même règle que la
        // possession, ci-dessus.
        if (m_languageOwnedOnly && !m_ownedLanguagesAvailable)
            return true;

        // Les DEUX sources se cumulent pour « existe », et une seule pour « jouable ».
        //
        // C'est la distinction que l'export 1.7.0 rend nécessaire : les dats disent quelle
        // ROM fournit quelle langue, IGDB dit seulement dans quelles langues le jeu existe.
        // La seconde source ne peut rien illuminer — aucun CRC ne s'y rattache — mais elle
        // répond parfaitement à « existe en français », et elle double la couverture du
        // filtre. Les mélanger dans l'autre sens produirait des badges ineffaçables.
        const quint64 available = m_languageOwnedOnly
                                      ? entry.ownedLangMask
                                      : (entry.langMask | entry.game.langCatalogMask);
        // ET binaire, et TOUTES les langues exigées : c'est le §8, et c'est aussi ce qui
        // rend la combinaison multi-langues gratuite.
        if ((available & m_requiredLangMask) != m_requiredLangMask)
            return false;
    }

    // Modes de jeu — même ET binaire, sur un masque livré sur le même patron. Pas de
    // variante « possédée » : le mode est une propriété du titre, pas de la ROM, donc
    // aucun scan n'entre ici et le filtre reste purement statique.
    if (m_requiredModeMask != 0
        && (entry.game.modeMask & m_requiredModeMask) != m_requiredModeMask)
        return false;

    return true;
}

void GameListModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    m_visible.reserve(m_games.size());

    for (int i = 0; i < m_games.size(); ++i) {
        if (matches(m_games.at(i)))
            m_visible.append(i);
    }

    endResetModel();
    emit countsChanged();
}

QList<int> GameListModel::orderedLanguageBits(const Entry &entry) const
{
    QList<int> bits;
    if (entry.langMask == 0)
        return bits;

    // Parcours par bit croissant : c'est l'ordre du catalogue, et il est STABLE d'un
    // export à l'autre puisque bit_index est attribué à vie.
    for (int bit = 0; bit < 64; ++bit) {
        if ((entry.langMask & (quint64(1) << bit)) == 0)
            continue;
        if (m_languageByBit.contains(bit))
            bits.append(bit);
        // Un bit allumé qu'aucune langue du référentiel ne réclame est ignoré : il
        // signalerait un référentiel plus ancien que le masque. L'ignorer perd un badge ;
        // l'afficher en inventerait un.
    }

    const QString &uiLanguage = m_interfaceLanguage;
    const auto     rank       = [this, &entry, &uiLanguage](int bit) {
        if ((entry.ownedLangMask & (quint64(1) << bit)) != 0)
            return 0; // possédée
        if (m_languages.at(m_languageByBit.value(bit)).code == uiLanguage)
            return 1; // langue de l'interface
        return 2;
    };

    std::stable_sort(bits.begin(), bits.end(), [&rank](int a, int b) {
        return rank(a) < rank(b);
    });
    return bits;
}

int GameListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};

    const Entry &entry = m_games.at(m_visible.at(index.row()));

    switch (role) {
    case GameKeyRole:
        return entry.game.gameKey;
    case TitleRole:
        return entry.game.title;
    case YearRole:
        return entry.game.year;
    case RatingRole:
        return entry.game.rating;
    case OwnedRole:
        return m_ownershipAvailable && m_owned.contains(entry.game.gameKey);
    case CoverRole:
        return entry.game.coverRef;
    case LanguagesRole: {
        // Construit à la demande, donc SEULEMENT pour les lignes visibles : le §8 met en
        // garde contre le coût des badges au défilement. Rien n'est stocké par jeu.
        QVariantList badges;
        const QList<int> bits = orderedLanguageBits(entry);
        for (int i = 0; i < bits.size() && i < kMaxBadges; ++i) {
            const int bit = bits.at(i);
            QVariantMap badge;
            badge.insert(QStringLiteral("code"),
                         m_languages.at(m_languageByBit.value(bit)).code);
            // Illuminé / grisé : la règle du §8, appliquée telle quelle.
            badge.insert(QStringLiteral("owned"),
                         (entry.ownedLangMask & (quint64(1) << bit)) != 0);
            badges.append(badge);
        }
        return badges;
    }
    case MatchedAliasRole:
        // Vide quand c'est le titre qui a mordu : la ligne n'a alors rien à expliquer, et
        // afficher un alias là serait du bruit.
        return entry.matchedAlias >= 0 && entry.matchedAlias < entry.aliases.size()
                   ? entry.aliases.at(entry.matchedAlias).name
                   : QString();
    case ExtraLanguageCountRole: {
        const int total = static_cast<int>(orderedLanguageBits(entry).size());
        return total > kMaxBadges ? total - kMaxBadges : 0;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
    return {
        { GameKeyRole, "gameKey" },
        { TitleRole, "title" },
        { YearRole, "year" },
        { RatingRole, "rating" },
        { OwnedRole, "owned" },
        { CoverRole, "coverRef" },
        { LanguagesRole, "languages" },
        { ExtraLanguageCountRole, "extraLanguages" },
        { MatchedAliasRole, "matchedAlias" },
    };
}

} // namespace igiris::ui
