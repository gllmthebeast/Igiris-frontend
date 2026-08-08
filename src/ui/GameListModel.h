#pragma once

// Le modèle de la liste d'accueil — CLAUDE.md §6.
//
// C'est le peu de C++ que le §12 autorise : exposer les données à QML, rien d'autre.
// Aucune règle d'affichage ici.
//
// Le §0 impose d'afficher TOUS les jeux du catalogue, ROM présente ou non : le modèle
// charge donc l'intégralité de l'export, et le filtrage se fait en mémoire. À 7 581 jeux,
// c'est instantané ; interroger SQLite à chaque frappe ne le serait pas, et le §6 exige
// que la combinaison reste interactive à la manette.

#include "catalog/ExportDatabase.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace igiris::ui {

class GameListModel : public QAbstractListModel
{
    Q_OBJECT

    // Nombre de jeux affichés après filtrage, et total du catalogue.
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    enum Role {
        GameKeyRole = Qt::UserRole + 1,
        TitleRole,
        YearRole,
        RatingRole,
    };

    explicit GameListModel(QObject *parent = nullptr);

    void setGames(QList<catalog::Game> games);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int visibleCount() const { return static_cast<int>(m_visible.size()); }
    int totalCount() const { return static_cast<int>(m_games.size()); }

    QString filter() const { return m_filter; }
    void    setFilter(const QString &filter);

signals:
    void countsChanged();
    void filterChanged();

private:
    void rebuild();

    QList<catalog::Game> m_games;
    QList<int>           m_visible; // index dans m_games
    QString              m_filter;
};

} // namespace igiris::ui
