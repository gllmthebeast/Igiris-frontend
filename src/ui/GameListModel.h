#pragma once

// Le modèle de la liste d'accueil et ses filtres — CLAUDE.md §6.
//
// C'est le peu de C++ que le §12 autorise : exposer les données à QML, rien d'autre.
//
// Le §6 distingue deux natures de filtre, et la distinction n'est pas cosmétique :
//
//   STATIQUE   se résout sur un index de l'export — plateforme, année, arcade.
//              Disponible dès le démarrage, sans toucher au disque.
//   DYNAMIQUE  impose un croisement avec le résultat du scan local — possédé / manquant.
//              Indisponible tant qu'aucun scan n'a eu lieu, et l'interface doit le DIRE
//              plutôt que de proposer un filtre qui ne filtre rien.
//
// Les filtres de LANGUE, également prévus au §6, sont absents : ils dépendent de
// exp_game_language, qui n'existe pas dans l'export 1.3.0 (§9.2). Ils arriveront au lot 8.

#include "catalog/ExportDatabase.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace igiris::ui {

class GameListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

    // --- filtres statiques -------------------------------------------------------------
    // Chaîne vide = pas de filtre. Volontairement pas d'énumération : les clés de
    // plateforme viennent de l'export, le code n'en connaît aucune (§9.1).
    Q_PROPERTY(QString platformFilter READ platformFilter WRITE setPlatformFilter
                   NOTIFY platformFilterChanged)
    Q_PROPERTY(QStringList availablePlatforms READ availablePlatforms NOTIFY catalogueChanged)

    // 0 = pas de filtre. Sinon, décennie pleine : 1990 retient 1990..1999.
    Q_PROPERTY(int decadeFilter READ decadeFilter WRITE setDecadeFilter
                   NOTIFY decadeFilterChanged)
    Q_PROPERTY(QList<int> availableDecades READ availableDecades NOTIFY catalogueChanged)

    Q_PROPERTY(bool arcadeOnly READ arcadeOnly WRITE setArcadeOnly NOTIFY arcadeOnlyChanged)

    // --- filtre dynamique --------------------------------------------------------------
    Q_PROPERTY(int ownership READ ownership WRITE setOwnership NOTIFY ownershipChanged)
    // Faux tant qu'aucun scan local n'a alimenté le modèle. L'interface s'adapte au lieu
    // de proposer un filtre inopérant.
    Q_PROPERTY(bool ownershipAvailable READ ownershipAvailable NOTIFY ownershipAvailableChanged)

public:
    enum Role {
        GameKeyRole = Qt::UserRole + 1,
        TitleRole,
        YearRole,
        RatingRole,
        OwnedRole,
    };

    enum Ownership {
        AnyOwnership = 0,
        OwnedOnly,
        MissingOnly,
    };
    Q_ENUM(Ownership)

    explicit GameListModel(QObject *parent = nullptr);

    // Alimente le catalogue et ses index statiques.
    void setCatalogue(QList<catalog::Game>          games,
                      QHash<QString, QStringList>   platformsByGame,
                      QStringList                   arcadePlatformKeys);

    // Résultat du scan local. Active le filtre dynamique.
    void setOwnedGameKeys(QSet<QString> owned);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int visibleCount() const { return static_cast<int>(m_visible.size()); }
    int totalCount() const { return static_cast<int>(m_games.size()); }

    QString filter() const { return m_filter; }
    void    setFilter(const QString &filter);

    QString     platformFilter() const { return m_platformFilter; }
    void        setPlatformFilter(const QString &platformKey);
    QStringList availablePlatforms() const { return m_availablePlatforms; }

    int        decadeFilter() const { return m_decadeFilter; }
    void       setDecadeFilter(int decade);
    QList<int> availableDecades() const { return m_availableDecades; }

    bool arcadeOnly() const { return m_arcadeOnly; }
    void setArcadeOnly(bool only);

    int  ownership() const { return m_ownership; }
    void setOwnership(int ownership);
    bool ownershipAvailable() const { return m_ownershipAvailable; }

    // Remet tous les filtres à zéro, recherche comprise.
    Q_INVOKABLE void clearFilters();

signals:
    void countsChanged();
    void filterChanged();
    void platformFilterChanged();
    void decadeFilterChanged();
    void arcadeOnlyChanged();
    void ownershipChanged();
    void ownershipAvailableChanged();
    void catalogueChanged();

private:
    struct Entry {
        catalog::Game game;
        QStringList   platformKeys;
        bool          isArcade = false;
        int           decade   = 0;
    };

    bool matches(const Entry &entry) const;
    void rebuild();

    QList<Entry> m_games;
    QList<int>   m_visible;

    QString m_filter;
    QString m_platformFilter;
    int     m_decadeFilter = 0;
    bool    m_arcadeOnly   = false;
    int     m_ownership    = AnyOwnership;

    QStringList   m_availablePlatforms;
    QList<int>    m_availableDecades;
    QSet<QString> m_owned;
    bool          m_ownershipAvailable = false;
};

} // namespace igiris::ui
