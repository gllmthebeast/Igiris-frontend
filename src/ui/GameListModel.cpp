#include "ui/GameListModel.h"

namespace igiris::ui {

GameListModel::GameListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void GameListModel::setGames(QList<catalog::Game> games)
{
    beginResetModel();
    m_games = std::move(games);
    endResetModel();
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

void GameListModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    m_visible.reserve(m_games.size());

    // La comparaison porte sur search_key, le nom NORMALISÉ côté serveur — jamais sur le
    // titre affiché. C'est ce qui fait que « pokemon » retrouve « Pokémon » sans que
    // l'appareil ait la moindre règle de normalisation (§0).
    const QString needle = m_filter.trimmed().toLower();

    for (int i = 0; i < m_games.size(); ++i) {
        if (needle.isEmpty() || m_games.at(i).searchKey.contains(needle))
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

    const catalog::Game &game = m_games.at(m_visible.at(index.row()));

    switch (role) {
    case GameKeyRole:
        return game.gameKey;
    case TitleRole:
        return game.title;
    case YearRole:
        return game.year;
    case RatingRole:
        return game.rating;
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
    };
}

} // namespace igiris::ui
