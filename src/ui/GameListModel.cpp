#include "ui/GameListModel.h"

#include <algorithm>

namespace igiris::ui {

GameListModel::GameListModel(QObject *parent)
    : QAbstractListModel(parent)
{
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
}

bool GameListModel::matches(const Entry &entry) const
{
    // Recherche : sur search_key, le nom normalisé côté serveur — jamais sur le titre (§3).
    const QString needle = m_filter.trimmed().toLower();
    if (!needle.isEmpty() && !entry.game.searchKey.contains(needle))
        return false;

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
    };
}

} // namespace igiris::ui
