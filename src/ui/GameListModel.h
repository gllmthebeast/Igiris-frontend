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
// Les filtres de LANGUE du §6 sont les DEUX natures appliquées au même axe, et c'est ce
// qui les rend faciles à confondre dans l'interface :
//
//   « existe en fr »   STATIQUE   exp_game.lang_mask                → sert la découverte
//   « jouable en fr »  DYNAMIQUE  masque restreint aux ROMs possédées → la valeur d'usage
//
// Le §8 l'impose : « ce qui illumine un badge est exactement ce qui fait passer un jeu à
// travers le filtre ». Une seule règle — un ET binaire de masques — deux points d'appel.

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

    // --- langue : un axe, deux natures (§6, §8) -----------------------------------------
    // Liste vide = pas de filtre. Plusieurs codes = TOUS exigés, par ET binaire sur le
    // masque, jamais par jointures répétées (§8).
    Q_PROPERTY(QStringList languageFilter READ languageFilter WRITE setLanguageFilter
                   NOTIFY languageFilterChanged)
    // Faux : « existe au catalogue » (statique). Vrai : « jouable » (dynamique).
    Q_PROPERTY(bool languageOwnedOnly READ languageOwnedOnly WRITE setLanguageOwnedOnly
                   NOTIFY languageOwnedOnlyChanged)
    // Codes proposables, dans l'ordre du catalogue — celui des bits, pas l'alphabétique.
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY languagesChanged)
    // Codes DEMANDÉS au filtre mais impossibles à honorer, faute de bit dans le masque.
    // Le filtre les ignore — un masque ne peut pas exprimer ce qu'il ne contient pas — et
    // « ignorer » veut dire renvoyer TOUT le catalogue. Sans cette propriété, l'appelant
    // lirait ce « tout » comme un résultat de recherche : le filtre paraîtrait marcher.
    Q_PROPERTY(QStringList unfilterableLanguages READ unfilterableLanguages
                   NOTIFY languageFilterChanged)
    // Faux sur un export sans tables de langues : l'interface le dit au lieu d'afficher un
    // filtre vide et des lignes sans badge sans explication.
    Q_PROPERTY(bool languagesAvailable READ languagesAvailable NOTIFY languagesChanged)
    // Faux tant qu'aucun scan n'a eu lieu : « jouable en fr » n'est alors pas calculable.
    Q_PROPERTY(bool ownedLanguagesAvailable READ ownedLanguagesAvailable
                   NOTIFY ownedLanguagesAvailableChanged)
    // Nombre de badges affichés par ligne, CONNU À L'AVANCE : le §8 interdit un calcul de
    // layout variable pendant le défilement.
    Q_PROPERTY(int maxBadges READ maxBadges CONSTANT)

    // --- modes de jeu (export 1.6.0) -----------------------------------------------------
    //
    // Le filtre « jouable à plusieurs » du §6, enfin calculable. Un seul filtre et non deux
    // comme pour la langue : un mode est une propriété du TITRE, pas de la ROM possédée —
    // il n'y a donc pas de version « dynamique » à croiser avec le scan.
    //
    // Liste vide = pas de filtre. Plusieurs clés = TOUS exigés, par ET binaire, exactement
    // comme les langues : le backend a livré ce masque sur le même patron, et c'est le même
    // code qui le lit.
    Q_PROPERTY(QStringList modeFilter READ modeFilter WRITE setModeFilter
                   NOTIFY modeFilterChanged)
    // Clés proposables, dans l'ordre des bits. Le référentiel est livré ENTIER par l'export
    // et non limité aux modes observés : le menu ne dépend donc pas du contenu du catalogue.
    Q_PROPERTY(QStringList availableModes READ availableModes NOTIFY modesChanged)
    // Faux sur un export antérieur au 1.6.0 : l'interface masque le filtre au lieu d'en
    // proposer un qui ne retiendrait rien.
    Q_PROPERTY(bool modesAvailable READ modesAvailable NOTIFY modesChanged)

    // Les jaquettes sont des URL distantes (§11) : c'est la SEULE entorse au hors-ligne de
    // tout le projet. Sur un appareil sans réseau elles ne chargeront jamais, et empiler
    // des requêtes qui échouent au défilement ne rend service à personne — d'où
    // l'interrupteur, et non un simple échec silencieux.
    Q_PROPERTY(bool coversEnabled READ coversEnabled WRITE setCoversEnabled
                   NOTIFY coversEnabledChanged)
    // Combien de vignettes sont DÉJÀ sur l'appareil. C'est ce qui permet à l'interface de
    // proposer — ou non — de constituer le cache, et de dire où il en est.
    Q_PROPERTY(int localCoverCount READ localCoverCount NOTIFY coversChanged)

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
        // URL de jaquette — la SEULE donnée qui exige le réseau (§11). Vide si inconnue.
        CoverRole,
        // Badges de la ligne : liste bornée de { code, owned }. Deux états et deux
        // seulement — illuminé si une ROM possédée fournit la langue, grisé sinon (§8).
        LanguagesRole,
        // Le « +N » : les langues du jeu qui ne tiennent pas dans la ligne.
        ExtraLanguageCountRole,
        // L'alias qui a produit ce résultat, VIDE quand c'est le titre qui a mordu
        // (export 1.8.0).
        //
        // Sans lui, taper « lttp » ferait apparaître « The Legend of Zelda: A Link to the
        // Past » — un titre qui ne contient aucun des caractères tapés. À l'écran, ça ne se
        // distingue pas d'un bug.
        MatchedAliasRole,
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

    // Le référentiel de langues et le masque STATIQUE de chaque jeu. Appelable avant ou
    // après setCatalogue() : les masques sont conservés et réappliqués.
    void setLanguages(QList<catalog::Language> languages, QHash<QString, quint64> maskByGame);

    // Le masque DYNAMIQUE, issu du croisement export × ROMs possédées. Active « jouable ».
    void setOwnedLanguageMasks(QHash<QString, quint64> maskByGame);

    // Le cache LOCAL de vignettes, constitué par l'appareil lui-même depuis IGDB.
    //
    // Rend la borne autonome : sans lui, la liste s'affiche sans une seule jaquette dès
    // qu'il n'y a pas de réseau — la seule entorse au hors-ligne du §11.
    //
    // Les clés présentes sont relevées UNE fois, pas à chaque ligne affichée : 17 000
    // appels à stat() pendant un défilement seraient exactement le genre de coût que le
    // §8 interdit d'introduire sans le mesurer.
    void setCoversDirectory(const QString &directory);

    // Relit le répertoire. Appelé après que le cache a été complété, sans quoi la liste
    // continuerait d'aller chercher sur le réseau des images désormais locales.
    Q_INVOKABLE void refreshCovers();

    // Le référentiel de modes de jeu. Pas de masque à passer en second argument,
    // contrairement aux langues : il voyage déjà dans catalog::Game::modeMask.
    void setGameModes(QList<catalog::GameMode> modes);

    // Les autres noms de chaque jeu (export 1.8.0). Appelable avant ou après
    // setCatalogue() : les alias sont conservés et réappliqués.
    void setAliases(QHash<QString, QList<catalog::GameAlias>> aliasesByGame);

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

    QStringList languageFilter() const { return m_languageFilter; }
    void        setLanguageFilter(const QStringList &codes);
    bool        languageOwnedOnly() const { return m_languageOwnedOnly; }
    void        setLanguageOwnedOnly(bool ownedOnly);
    QStringList availableLanguages() const { return m_availableLanguages; }
    QStringList unfilterableLanguages() const { return m_unfilterableLanguages; }
    bool        languagesAvailable() const { return !m_languages.isEmpty(); }
    bool        ownedLanguagesAvailable() const { return m_ownedLanguagesAvailable; }
    int         maxBadges() const { return kMaxBadges; }
    bool        coversEnabled() const { return m_coversEnabled; }
    int         localCoverCount() const { return static_cast<int>(m_localCovers.size()); }
    void        setCoversEnabled(bool enabled);

    QStringList modeFilter() const { return m_modeFilter; }
    void        setModeFilter(const QStringList &keys);
    QStringList availableModes() const { return m_availableModes; }
    bool        modesAvailable() const { return !m_modes.isEmpty(); }

    // Libellé affichable d'un code — pour l'interface, qui ne doit pas inventer de table.
    Q_INVOKABLE QString languageLabel(const QString &code) const;

    // Idem pour un mode : le libellé vient de l'export, jamais d'une table écrite ici.
    Q_INVOKABLE QString modeLabel(const QString &key) const;

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
    void languageFilterChanged();
    void languageOwnedOnlyChanged();
    void languagesChanged();
    void ownedLanguagesAvailableChanged();
    void coversEnabledChanged();
    void coversChanged();
    void modeFilterChanged();
    void modesChanged();

private:
    // Borne d'affichage du §8. Mesurée sur l'export réel : la moitié du catalogue badgé
    // tient dans 5 langues, mais un jeu monte à 22. Sans borne, la largeur d'une ligne
    // dépendrait du jeu, donc le layout serait recalculé pendant le défilement.
    static constexpr int kMaxBadges = 6;

    struct Entry {
        catalog::Game game;
        QStringList   platformKeys;
        bool          isArcade = false;
        int           decade   = 0;
        // Deux masques, jamais de liste de chaînes : ce sont eux qui rendent le filtre et
        // le badge calculables au défilement sans allouer.
        quint64 langMask      = 0;
        quint64 ownedLangMask = 0;

        // Les autres noms du jeu, et l'indice de celui qui a fait passer la recherche.
        // -1 = c'est le titre qui a mordu, ou aucun filtre de recherche n'est actif.
        //
        // `mutable` parce que matches() est logiquement un prédicat : il ne change pas ce
        // que le modèle CONTIENT, seulement de quoi expliquer une correspondance.
        QList<catalog::GameAlias> aliases;
        mutable int               matchedAlias = -1;
    };

    // Reste un prédicat const, mais elle mémorise au passage l'alias qui a produit le
    // résultat (champ `mutable`) : le calcul est déjà fait au moment du filtrage, et le
    // refaire à l'affichage serait le faire deux fois par ligne visible.
    bool matches(const Entry &entry) const;
    void rebuild();
    // Les bits de langue d'un jeu, dans l'ordre d'affichage du §8 : possédées d'abord,
    // puis la langue de l'interface, puis l'ordre stable du catalogue.
    QList<int> orderedLanguageBits(const Entry &entry) const;
    void applyLanguageMasks();
    void applyAliases();
    // Le masque exigé par le filtre, recalculé quand le filtre ou le référentiel change.
    void recomputeRequiredMask();

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

    QList<catalog::Language> m_languages;         // ordre du catalogue = ordre des bits
    QHash<int, int>          m_languageByBit;     // bit → index dans m_languages
    QStringList              m_availableLanguages;
    QHash<QString, quint64>  m_langMaskByGame;
    QHash<QString, quint64>  m_ownedLangMaskByGame;
    QHash<QString, QList<catalog::GameAlias>> m_aliasesByGame;
    QString                                   m_coversDir;
    QSet<QString>                             m_localCovers;
    QStringList              m_languageFilter;
    QStringList              m_unfilterableLanguages;
    quint64                  m_requiredLangMask = 0;
    bool                     m_languageOwnedOnly = false;
    bool                     m_ownedLanguagesAvailable = false;
    // Langue de l'interface : elle passe devant les autres dans l'ordre des badges (§8),
    // après les langues possédées.
    QString m_interfaceLanguage;
    bool    m_coversEnabled = true;

    QList<catalog::GameMode> m_modes;
    QStringList              m_availableModes;
    QStringList              m_modeFilter;
    quint64                  m_requiredModeMask = 0;
};

} // namespace igiris::ui
