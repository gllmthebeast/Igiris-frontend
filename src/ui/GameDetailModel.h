#pragma once

// La fiche de jeu — CLAUDE.md §7.
//
// Elle liste les systèmes sur lesquels le jeu existe, avec le code couleur :
//
//   VERT   système présent sur cette installation ET ROM présente localement
//   ROUGE  système présent, ROM absente
//   NOIR   système absent de cette installation
//
// Le statut noir est décidé par le FICHIER DE DESCRIPTION DES SYSTÈMES, pas par le
// catalogue (§1) : c'est lui la source de vérité des systèmes présents.
//
// Le système marqué is_preferred est proposé par défaut ; les autres sont des options
// secondaires. Le lancement ne part que d'une ligne verte.

#include "catalog/ExportDatabase.h"
#include "platform/PlatformAdapter.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantList>

namespace igiris::platform {
class PlatformAdapter;
}

namespace igiris::ui {

class GameDetailModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString gameKey READ gameKey NOTIFY gameChanged)
    Q_PROPERTY(QString title READ title NOTIFY gameChanged)
    Q_PROPERTY(int rating READ rating NOTIFY gameChanged)
    // URL de jaquette (§7, §11). Vide si inconnue — la fiche réserve la place quand même.
    Q_PROPERTY(QString coverRef READ coverRef NOTIFY gameChanged)
    Q_PROPERTY(bool launchAvailable READ launchAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString launchWarning READ launchWarning NOTIFY capabilitiesChanged)

public:
    enum Status {
        Black = 0, // système absent de cette installation
        Red,       // système présent, ROM absente
        Green,     // système présent et ROM présente
    };
    Q_ENUM(Status)

    enum Role {
        PlatformKeyRole = Qt::UserRole + 1,
        DisplayNameRole,
        EmuScoreRole,
        PreferredRole,
        StatusRole,
        RomPathRole,
        LaunchLabelRole,
        DefaultChoiceRole,
        // §7 : « c'est ici, et pas en vue liste, qu'on détaille quelle release apporte
        // quelles langues ». Liste de { code, owned } RESTREINTE à cette plateforme —
        // c'est la seule différence de sémantique avec les badges du §8.
        LanguagesRole,
    };

    explicit GameDetailModel(QObject *parent = nullptr);

    void setCatalogue(const catalog::ExportDatabase *db);
    void setAdapter(const platform::PlatformAdapter *adapter);

    // Systèmes réellement présents, lus dans le fichier de description (§1).
    void setLocalSystems(QHash<QString, platform::SystemEntry> systems);

    // ROMs identifiées par le scan : (gameKey, platformKey) → chemin.
    void setOwnedRoms(QHash<QString, QString> ownedRoms);

    // Le référentiel de langues, pour l'ordre d'affichage et les libellés.
    void setLanguages(QList<catalog::Language> languages);

    // Les ROMs possédées, désignées par catalog::romKey(crc32, platformKey). C'est CETTE
    // clé, et pas le chemin, qui décide de l'illumination : la règle du §8 porte sur le
    // crc32 de exp_game_language, pas sur le fichier.
    void setOwnedRomKeys(QSet<QString> ownedRomKeys);

    Q_INVOKABLE void setGame(const QString &gameKey);

    // Construit la commande sans rien exécuter — c'est ce qui rend le lancement
    // vérifiable depuis une machine de build (§15).
    Q_INVOKABLE QString commandPreview(int row) const;

    // Lance réellement. Retourne un message d'erreur COMPLET, vide en cas de succès.
    Q_INVOKABLE QString launch(int row);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString gameKey() const { return m_gameKey; }
    QString title() const { return m_title; }
    int     rating() const { return m_rating; }
    QString coverRef() const { return m_coverRef; }

    bool    launchAvailable() const;
    QString launchWarning() const;

signals:
    void gameChanged();
    void capabilitiesChanged();

private:
    struct Row {
        QString platformKey;
        QString displayName;
        int     emuScore    = 0;
        bool    isPreferred = false;
        Status  status      = Black;
        QString romPath;
        QString      launchLabel;
        bool         isDefaultChoice = false;
        QVariantList languages;
    };

    // Rang d'affichage d'une langue : possédée d'abord, puis l'ordre des bits, puis les
    // langues sans bit. Même ordre qu'en vue liste, restreint à une plateforme.
    int languageRank(const QString &code) const;

    const catalog::ExportDatabase   *m_db      = nullptr;
    const platform::PlatformAdapter *m_adapter = nullptr;

    QHash<QString, platform::SystemEntry> m_localSystems;
    QHash<QString, QString>               m_ownedRoms;
    QSet<QString>                         m_ownedRomKeys;
    QList<catalog::Language>              m_languages;

    QString    m_gameKey;
    QString    m_title;
    int        m_rating = 0;
    QString    m_coverRef;
    QList<Row> m_rows;
};

} // namespace igiris::ui
