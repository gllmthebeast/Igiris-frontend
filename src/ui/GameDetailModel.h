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
#include <QStringList>
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
    Q_PROPERTY(int year READ year NOTIFY gameChanged)
    // URL de jaquette (§7, §11). Vide si inconnue — la fiche réserve la place quand même.
    Q_PROPERTY(QString coverRef READ coverRef NOTIFY gameChanged)
    // Image du BANDEAU de la fiche. Distincte de la jaquette par nature : une jaquette est
    // verticale et porte du texte, un bandeau est large et illustratif.
    //
    // Depuis l'export 1.5.0, c'est une VRAIE illustration (artwork_ref) sur 95,6 % du
    // catalogue. Pour les 4,4 % restants on retombe sur la jaquette recadrée — un pis-aller
    // que la fiche assume en l'adoucissant, plutôt que de laisser un trou.
    Q_PROPERTY(QString bannerRef READ bannerRef NOTIFY gameChanged)
    // Vrai quand le bandeau montre une image RÉELLEMENT prévue pour ça. Faux quand on
    // recadre une jaquette : l'interface adoucit alors le rendu au lieu de prétendre.
    Q_PROPERTY(bool hasRealBanner READ hasRealBanner NOTIFY gameChanged)
    // SYNOPSIS (export 1.6.0). TOUJOURS EN ANGLAIS, y compris sur une interface en
    // français : IGDB n'en fournit pas de traduit, et le backend a écarté une colonne
    // summary_lang qui aurait porté « en » sur 100 % des lignes. Vide sur 0,3 % du
    // catalogue, auquel cas la fiche n'affiche simplement rien.
    Q_PROPERTY(QString summary READ summary NOTIFY gameChanged)
    // Modes de jeu du titre, libellés déjà traduits par l'export : [« Un joueur »,
    // « Coopératif »…]. Vide quand l'export n'en porte pas.
    Q_PROPERTY(QStringList modeLabels READ modeLabels NOTIFY gameChanged)
    // Langues connues du CATALOGUE seul (export 1.7.0), et qu'aucune ROM ne fournit.
    //
    // Volontairement SÉPARÉES des badges par plateforme : celles-ci se rangent sur l'axe
    // illuminé / grisé, qui promet qu'un téléchargement peut allumer le badge. Une langue
    // de catalogue ne le peut pas — IGDB ne connaît ni release ni CRC. Les mêler ferait de
    // la moitié des badges des promesses invérifiables.
    Q_PROPERTY(QStringList catalogLanguages READ catalogLanguages NOTIFY gameChanged)
    // Plateformes du catalogue qu'on ne sait PAS émuler — PC, PS4, Switch… Vide avant
    // l'export 1.7.0, où elles n'existaient tout simplement pas.
    //
    // La fiche s'en sert pour expliquer une liste de systèmes vide : 9 679 jeux sur 17 260
    // sont dans ce cas, et une zone vide sans un mot se lit comme une panne.
    Q_PROPERTY(QStringList nonEmulablePlatforms READ nonEmulablePlatforms NOTIFY gameChanged)
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
        // ARCADE seulement, et lu dans exp_romset : le matériel réel (« neogeo », « cps2 »)
        // qu'IGDB ne connaît pas — il n'a qu'une plateforme « Arcade » globale (§4) — et
        // l'état du pilote MAME. Vides pour une console.
        HardwareRole,
        EmulatorsRole,
        DriverStatusRole,
        // Année de sortie SUR CETTE PLATEFORME (export 1.5.0), 0 si inconnue. À ne pas
        // confondre avec l'année du jeu affichée en tête de fiche : 42 % des lignes en
        // diffèrent, et c'est précisément ce que cette liste sert à montrer.
        ReleaseYearRole,
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

    // Le référentiel de modes de jeu, pour les libellés. Injecté plutôt que requêté à
    // chaque ouverture de fiche, comme les langues : c'est une constante de l'export.
    void setGameModes(QList<catalog::GameMode> modes);

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
    int     year() const { return m_year; }
    QString coverRef() const { return m_coverRef; }
    // La vraie illustration si l'export en a une, la jaquette sinon — et hasRealBanner dit
    // laquelle des deux, pour que la fiche ne mente pas sur ce qu'elle montre.
    QString bannerRef() const { return m_artworkRef.isEmpty() ? m_coverRef : m_artworkRef; }
    bool    hasRealBanner() const { return !m_artworkRef.isEmpty(); }
    QString     summary() const { return m_summary; }
    QStringList modeLabels() const { return m_modeLabels; }
    QStringList catalogLanguages() const { return m_catalogLanguages; }
    QStringList nonEmulablePlatforms() const { return m_nonEmulablePlatforms; }

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
        QString      hardware;
        QString      emulators;
        QString      driverStatus;
        int          releaseYear = 0;
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
    QList<catalog::GameMode>              m_modes;

    QString    m_gameKey;
    QString    m_title;
    int        m_rating = 0;
    int        m_year   = 0;
    QString     m_coverRef;
    QString     m_artworkRef;
    QString     m_summary;
    QStringList m_modeLabels;
    QStringList m_catalogLanguages;
    QStringList m_nonEmulablePlatforms;
    QList<Row>  m_rows;
};

} // namespace igiris::ui
