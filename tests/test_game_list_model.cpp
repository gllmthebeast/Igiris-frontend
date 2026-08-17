// Tests des lots 5 et 6 — modèle de la liste d'accueil et ses filtres.
//
// Le point sensible n'est pas l'affichage, c'est la RECHERCHE : elle doit porter sur
// search_key, le nom normalisé par le serveur, et jamais sur le titre affiché. Se tromper
// de champ produirait une recherche qui « marche » sur les titres simples et échoue en
// silence sur tous les autres.

#include "ui/GameListModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace igiris::ui;
using igiris::catalog::Game;

namespace {

QList<Game> sampleGames()
{
    return {
        { QStringLiteral("igdb-1"), QStringLiteral("Pokémon Rouge"),
          QStringLiteral("pokemon rouge"), 1996, 88 },
        { QStringLiteral("igdb-2"), QStringLiteral("The Legend of Zelda"),
          QStringLiteral("legend of zelda"), 1986, 92 },
        { QStringLiteral("igdb-3"), QStringLiteral("Super Mario World"),
          QStringLiteral("super mario world"), 1990, 96 },
    };
}

// Alimente le modèle avec les index statiques : plateformes par jeu, et la liste des
// plateformes d'arcade telle que l'export la donnerait.
void feed(GameListModel &model)
{
    const QHash<QString, QStringList> platforms = {
        { QStringLiteral("igdb-1"), { QStringLiteral("gb"), QStringLiteral("gbc") } },
        { QStringLiteral("igdb-2"), { QStringLiteral("nes") } },
        { QStringLiteral("igdb-3"), { QStringLiteral("snes"), QStringLiteral("mame") } },
    };
    model.setCatalogue(sampleGames(), platforms, { QStringLiteral("mame") });
}

// Le référentiel de langues tel que l'export 1.4.0 le donne, y compris le cas qui casse
// en silence : une langue SANS bit (bit_index NULL côté backend).
QList<igiris::catalog::Language> sampleLanguages()
{
    using igiris::catalog::Language;
    return {
        { QStringLiteral("en"), QStringLiteral("Anglais"), {}, 0 },
        { QStringLiteral("fr"), QStringLiteral("Français"), {}, 1 },
        { QStringLiteral("de"), QStringLiteral("Allemand"), {}, 3 },
        { QStringLiteral("ja"), QStringLiteral("Japonais"), {}, 5 },
        { QStringLiteral("xx"), QStringLiteral("Sans bit"), {}, -1 },
    };
}

// igdb-1 : ja seul · igdb-2 : en, fr, de, ja · igdb-3 : en, fr
void feedLanguages(GameListModel &model)
{
    model.setLanguages(sampleLanguages(),
                       { { QStringLiteral("igdb-1"), 0b100000 },
                         { QStringLiteral("igdb-2"), 0b101011 },
                         { QStringLiteral("igdb-3"), 0b000011 } });
}

// Le référentiel de modes de l'export 1.6.0, réduit à ce qu'il faut pour le filtre.
QList<igiris::catalog::GameMode> sampleModes()
{
    using igiris::catalog::GameMode;
    return {
        { QStringLiteral("solo"), QStringLiteral("Un joueur"), 0 },
        { QStringLiteral("multi"), QStringLiteral("Multijoueur"), 1 },
        { QStringLiteral("coop"), QStringLiteral("Coopératif"), 2 },
    };
}

// igdb-1 : solo · igdb-2 : solo + multi · igdb-3 : AUCUN mode connu.
//
// Ce troisième cas n'est pas un remplissage : 2,9 % du catalogue réel n'a pas de masque,
// et un filtre qui les retiendrait serait faux.
void feedModes(GameListModel &model)
{
    auto games = sampleGames();
    games[0].modeMask = 0b001;
    games[1].modeMask = 0b011;
    games[2].modeMask = 0;

    const QHash<QString, QStringList> platforms = {
        { QStringLiteral("igdb-1"), { QStringLiteral("gb") } },
        { QStringLiteral("igdb-2"), { QStringLiteral("nes") } },
        { QStringLiteral("igdb-3"), { QStringLiteral("snes") } },
    };
    model.setCatalogue(games, platforms, {});
    model.setGameModes(sampleModes());
}

} // namespace

class TestGameListModel : public QObject
{
    Q_OBJECT

private slots:
    void model_respectsAbstractItemModelContract();
    void model_exposesAllGamesByDefault();
    void roles_areNamedForQml();
    void filter_matchesOnNormalisedSearchKeyNotTitle();
    void filter_isCaseInsensitive();
    void filter_emptyRestoresEverything();
    void filter_noMatchLeavesEmptyButKnowsTotal();
    void filter_emitsSignalsOnce();

    // --- lot 6 : filtres ---
    void staticFilters_areAvailableWithoutAnyScan();
    void platformFilter_keepsGamesOnThatPlatform();
    void decadeFilter_coversTheWholeDecade();
    void arcadeFilter_usesTheExportsArcadeKeys();
    void filters_combineWithAnd();
    void ownership_isUnavailableUntilAScanHappens();
    void ownership_doesNotFilterWhenUnavailable();
    void ownership_splitsOwnedAndMissingAfterScan();
    void ownership_emptyScanStillMeansAvailable();
    void clearFilters_restoresEverything();

    // --- lot 8 : badges et filtres de langue ---
    void languages_areUnavailableOnAnExportWithoutThem();
    void languageFilter_static_findsGamesThatExistInThatLanguage();
    void languageFilter_multipleCodesRequireThemAll();
    void languageFilter_dynamic_doesNotFilterBeforeAnyScan();
    void languageFilter_dynamic_keepsOnlyPlayableOnes();
    void languageFilter_reportsCodesItCannotHonour();
    void badges_orderOwnedFirstThenCatalogue();
    void badges_areBoundedAndCountTheRest();
    void badges_ownedNeverExceedsTheCatalogue();

    // --- export 1.6.0 : filtre des modes de jeu ---
    void modes_areUnavailableOnAnExportWithoutThem();
    void modeFilter_keepsOnlyGamesCarryingThatBit();
    void modeFilter_multipleModesRequireThemAll();
    void modeFilter_isClearedByClearFilters();

    // --- export 1.7.0 : langues de catalogue ---
    void catalogLanguages_widenExistsButNeverPlayable();
    void catalogLanguages_doNotLightUpBadges();

    // --- export 1.8.0 : alias de noms ---
    void aliases_findGamesTheTitleAloneCannot();
    void aliases_reportWhichOneMatched();
    void aliases_preferTheExactOneOverASubstring();
    void aliases_areAbsentFromAnOlderExportWithoutBreakingIt();
};

void TestGameListModel::languages_areUnavailableOnAnExportWithoutThem()
{
    GameListModel model;
    feed(model);

    // Export 1.3.0 : pas de langues. L'interface doit pouvoir le DIRE, pas afficher un
    // filtre vide et des lignes muettes sans explication.
    QVERIFY(!model.languagesAvailable());
    QVERIFY(model.availableLanguages().isEmpty());
    QVERIFY(!model.ownedLanguagesAvailable());
    QCOMPARE(model.rowCount(), 3);

    const QModelIndex index = model.index(0, 0);
    QVERIFY(index.data(GameListModel::LanguagesRole).toList().isEmpty());
    QCOMPARE(index.data(GameListModel::ExtraLanguageCountRole).toInt(), 0);
}

void TestGameListModel::languageFilter_static_findsGamesThatExistInThatLanguage()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);

    QVERIFY(model.languagesAvailable());
    // Les langues sans bit ne sont pas proposables : le masque ne peut pas les porter.
    QCOMPARE(model.availableLanguages(),
             QStringList({ QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de"),
                           QStringLiteral("ja") }));

    // « existe en français » : STATIQUE, disponible sans le moindre scan.
    model.setLanguageFilter({ QStringLiteral("fr") });
    QCOMPARE(model.rowCount(), 2); // igdb-2 et igdb-3

    model.setLanguageFilter({ QStringLiteral("ja") });
    QCOMPARE(model.rowCount(), 2); // igdb-1 et igdb-2

    model.setLanguageFilter({});
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::languageFilter_multipleCodesRequireThemAll()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);

    // ET binaire, pas OU : le §8 fournit lang_mask précisément pour ça.
    model.setLanguageFilter({ QStringLiteral("fr"), QStringLiteral("ja") });
    QCOMPARE(model.rowCount(), 1); // igdb-2 seul porte les deux

    model.setLanguageFilter({ QStringLiteral("fr"), QStringLiteral("en") });
    QCOMPARE(model.rowCount(), 2);
}

void TestGameListModel::languageFilter_dynamic_doesNotFilterBeforeAnyScan()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);

    QVERIFY(!model.ownedLanguagesAvailable());
    model.setLanguageFilter({ QStringLiteral("fr") });
    model.setLanguageOwnedOnly(true);

    // Sans scan, « jouable en français » n'est pas calculable. Répondre « aucun jeu »
    // serait une affirmation fausse ; on ne filtre pas — même règle que la possession.
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::languageFilter_dynamic_keepsOnlyPlayableOnes()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);

    // igdb-2 : on possède une ROM anglaise seulement. igdb-3 : rien.
    model.setOwnedLanguageMasks({ { QStringLiteral("igdb-2"), 0b000001 } });
    QVERIFY(model.ownedLanguagesAvailable());

    model.setLanguageFilter({ QStringLiteral("en") });
    QCOMPARE(model.rowCount(), 2); // existe : igdb-2, igdb-3

    model.setLanguageOwnedOnly(true);
    QCOMPARE(model.rowCount(), 1); // jouable : igdb-2 seul
    QCOMPARE(model.index(0, 0).data(GameListModel::GameKeyRole).toString(),
             QStringLiteral("igdb-2"));

    // Et une langue possédée par personne ne rend rien jouable.
    model.setLanguageFilter({ QStringLiteral("fr") });
    QCOMPARE(model.rowCount(), 0);
}

void TestGameListModel::languageFilter_reportsCodesItCannotHonour()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);

    // Une langue SANS bit ne peut pas entrer dans un ET binaire. Le modèle ne filtre donc
    // pas — mais il le DIT. Sans ça, l'appelant lirait le catalogue entier comme un
    // résultat de recherche, et le filtre paraîtrait fonctionner.
    model.setLanguageFilter({ QStringLiteral("xx") });
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.unfilterableLanguages(), QStringList({ QStringLiteral("xx") }));

    // Un code inconnu du référentiel tombe dans le même cas, et pour la même raison.
    model.setLanguageFilter({ QStringLiteral("zz") });
    QCOMPARE(model.unfilterableLanguages(), QStringList({ QStringLiteral("zz") }));

    // Une langue honorable ne laisse rien derrière elle.
    model.setLanguageFilter({ QStringLiteral("fr") });
    QVERIFY(model.unfilterableLanguages().isEmpty());
}

void TestGameListModel::badges_orderOwnedFirstThenCatalogue()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);
    // igdb-2 porte en|fr|de|ja ; on ne possède que l'allemand.
    model.setOwnedLanguageMasks({ { QStringLiteral("igdb-2"), 0b001000 } });

    // igdb-2 = « The Legend of Zelda », deuxième par titre.
    int row = -1;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.index(i, 0).data(GameListModel::GameKeyRole).toString()
            == QStringLiteral("igdb-2"))
            row = i;
    }
    QVERIFY(row >= 0);

    const QVariantList badges = model.index(row, 0).data(GameListModel::LanguagesRole).toList();
    QCOMPARE(badges.size(), 4);

    // Possédée d'abord (§8), puis l'ordre STABLE du catalogue — celui des bits.
    QCOMPARE(badges.at(0).toMap().value("code").toString(), QStringLiteral("de"));
    QCOMPARE(badges.at(0).toMap().value("owned").toBool(), true);
    QCOMPARE(badges.at(1).toMap().value("code").toString(), QStringLiteral("en"));
    QCOMPARE(badges.at(1).toMap().value("owned").toBool(), false);
    QCOMPARE(badges.at(2).toMap().value("code").toString(), QStringLiteral("fr"));
    QCOMPARE(badges.at(3).toMap().value("code").toString(), QStringLiteral("ja"));
}

void TestGameListModel::badges_areBoundedAndCountTheRest()
{
    GameListModel model;
    feed(model);

    // Un jeu à 8 langues, alors que la ligne en affiche 6 : le §8 exige une borne CONNUE
    // À L'AVANCE, sinon la largeur d'une ligne dépend de son contenu.
    QList<igiris::catalog::Language> many;
    quint64                          mask = 0;
    for (int bit = 0; bit < 8; ++bit) {
        many.append({ QStringLiteral("l%1").arg(bit), QStringLiteral("Langue %1").arg(bit), {},
                      bit });
        mask |= quint64(1) << bit;
    }
    model.setLanguages(many, { { QStringLiteral("igdb-1"), mask } });

    int row = -1;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.index(i, 0).data(GameListModel::GameKeyRole).toString()
            == QStringLiteral("igdb-1"))
            row = i;
    }
    QVERIFY(row >= 0);

    const QModelIndex index = model.index(row, 0);
    QCOMPARE(index.data(GameListModel::LanguagesRole).toList().size(), model.maxBadges());
    QCOMPARE(index.data(GameListModel::ExtraLanguageCountRole).toInt(), 8 - model.maxBadges());
}

void TestGameListModel::badges_ownedNeverExceedsTheCatalogue()
{
    GameListModel model;
    feed(model);
    feedLanguages(model);

    // igdb-3 n'existe qu'en en|fr. Un masque possédé qui prétendrait au japonais
    // signalerait deux référentiels de bits différents : le badge serait illuminé sans
    // badge correspondant au catalogue, donc faux et invérifiable.
    model.setOwnedLanguageMasks({ { QStringLiteral("igdb-3"), 0b100001 } });

    int row = -1;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.index(i, 0).data(GameListModel::GameKeyRole).toString()
            == QStringLiteral("igdb-3"))
            row = i;
    }
    QVERIFY(row >= 0);

    const QVariantList badges = model.index(row, 0).data(GameListModel::LanguagesRole).toList();
    QCOMPARE(badges.size(), 2); // en, fr — jamais ja
    for (const QVariant &badge : badges)
        QVERIFY(badge.toMap().value("code").toString() != QStringLiteral("ja"));
}

void TestGameListModel::model_respectsAbstractItemModelContract()
{
    GameListModel model;
    // Un modèle mal formé casse les vues QML de façon obscure : autant le vérifier ici.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Warning);
    feed(model);
    model.setFilter(QStringLiteral("mario"));
    model.setFilter(QString());
}

void TestGameListModel::model_exposesAllGamesByDefault()
{
    GameListModel model;
    feed(model);

    // §0 : tous les jeux du catalogue sont affichés, ROM présente ou non.
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.totalCount(), 3);
    QCOMPARE(model.visibleCount(), 3);
}

void TestGameListModel::roles_areNamedForQml()
{
    GameListModel model;
    feed(model);

    const auto roles = model.roleNames();
    QVERIFY(roles.values().contains(QByteArray("title")));
    QVERIFY(roles.values().contains(QByteArray("gameKey")));

    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, GameListModel::TitleRole).toString(),
             QStringLiteral("Pokémon Rouge"));
    QCOMPARE(model.data(first, GameListModel::RatingRole).toInt(), 88);
}

void TestGameListModel::filter_matchesOnNormalisedSearchKeyNotTitle()
{
    GameListModel model;
    feed(model);

    // « pokemon » sans accent doit retrouver « Pokémon Rouge » — c'est tout l'intérêt du
    // search_key normalisé par le serveur. Filtrer sur le titre échouerait ici.
    model.setFilter(QStringLiteral("pokemon"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("Pokémon Rouge"));

    // « The » ne figure pas dans le search_key de Zelda : le serveur l'a normalisé.
    model.setFilter(QStringLiteral("legend"));
    QCOMPARE(model.rowCount(), 1);
}

void TestGameListModel::filter_isCaseInsensitive()
{
    GameListModel model;
    feed(model);

    model.setFilter(QStringLiteral("MARIO"));
    QCOMPARE(model.rowCount(), 1);
}

void TestGameListModel::filter_emptyRestoresEverything()
{
    GameListModel model;
    feed(model);

    model.setFilter(QStringLiteral("mario"));
    QCOMPARE(model.rowCount(), 1);
    model.setFilter(QStringLiteral("   ")); // des espaces ne sont pas un filtre
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::filter_noMatchLeavesEmptyButKnowsTotal()
{
    GameListModel model;
    feed(model);

    model.setFilter(QStringLiteral("zzzz"));
    QCOMPARE(model.rowCount(), 0);
    // L'interface doit pouvoir dire « aucun résultat sur N jeux », pas « catalogue vide ».
    QCOMPARE(model.totalCount(), 3);
}

void TestGameListModel::filter_emitsSignalsOnce()
{
    GameListModel model;
    feed(model);

    QSignalSpy filterSpy(&model, &GameListModel::filterChanged);
    model.setFilter(QStringLiteral("mario"));
    QCOMPARE(filterSpy.count(), 1);

    // Reposer le même filtre ne doit rien réémettre : sinon chaque frappe reconstruirait
    // la vue sans raison, et le §6 exige que ça reste interactif à la manette.
    model.setFilter(QStringLiteral("mario"));
    QCOMPARE(filterSpy.count(), 1);
}

// ------------------------------------------------------------------- lot 6 : filtres

void TestGameListModel::staticFilters_areAvailableWithoutAnyScan()
{
    GameListModel model;
    feed(model);

    // Un filtre STATIQUE se résout sur un index de l'export : il existe dès le démarrage,
    // sans avoir touché au disque (§6).
    QCOMPARE(model.availablePlatforms(),
             (QStringList{ "gb", "gbc", "mame", "nes", "snes" })); // trié
    QCOMPARE(model.availableDecades(), (QList<int>{ 1980, 1990 }));
    QVERIFY(!model.ownershipAvailable()); // le dynamique, lui, n'existe pas encore
}

void TestGameListModel::platformFilter_keepsGamesOnThatPlatform()
{
    GameListModel model;
    feed(model);

    model.setPlatformFilter(QStringLiteral("gbc"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("Pokémon Rouge"));

    model.setPlatformFilter(QString()); // chaîne vide = pas de filtre
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::decadeFilter_coversTheWholeDecade()
{
    GameListModel model;
    feed(model);

    // 1990 doit retenir 1990 ET 1996, pas seulement l'année pile.
    model.setDecadeFilter(1990);
    QCOMPARE(model.rowCount(), 2);

    model.setDecadeFilter(1980);
    QCOMPARE(model.rowCount(), 1);
}

void TestGameListModel::arcadeFilter_usesTheExportsArcadeKeys()
{
    GameListModel model;
    feed(model);

    // « mame » est arcade parce que l'export le dit, pas parce que le code le sait (§4).
    model.setArcadeOnly(true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("Super Mario World"));
}

void TestGameListModel::filters_combineWithAnd()
{
    GameListModel model;
    feed(model);

    model.setDecadeFilter(1990);
    model.setPlatformFilter(QStringLiteral("snes"));
    QCOMPARE(model.rowCount(), 1); // Mario : 1990 ET snes

    model.setPlatformFilter(QStringLiteral("nes")); // Zelda est 1986, pas 1990
    QCOMPARE(model.rowCount(), 0);
}

void TestGameListModel::ownership_isUnavailableUntilAScanHappens()
{
    GameListModel model;
    feed(model);

    QSignalSpy spy(&model, &GameListModel::ownershipAvailableChanged);
    QVERIFY(!model.ownershipAvailable());

    model.setOwnedGameKeys({ QStringLiteral("igdb-1") });
    QVERIFY(model.ownershipAvailable());
    QCOMPARE(spy.count(), 1);
}

void TestGameListModel::ownership_doesNotFilterWhenUnavailable()
{
    GameListModel model;
    feed(model);

    // Sans scan, on ne peut RIEN affirmer sur la possession. Filtrer quand même
    // afficherait « tout est manquant », ce qui serait faux et invérifiable.
    model.setOwnership(GameListModel::OwnedOnly);
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::ownership_splitsOwnedAndMissingAfterScan()
{
    GameListModel model;
    feed(model);
    model.setOwnedGameKeys({ QStringLiteral("igdb-1"), QStringLiteral("igdb-3") });

    model.setOwnership(GameListModel::OwnedOnly);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::OwnedRole).toBool(), true);

    model.setOwnership(GameListModel::MissingOnly);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("The Legend of Zelda"));
}

void TestGameListModel::ownership_emptyScanStillMeansAvailable()
{
    GameListModel model;
    feed(model);

    // Un scan qui ne trouve rien reste un scan : l'information est « tu ne possèdes
    // rien », pas « le filtre est indisponible ».
    model.setOwnedGameKeys({});
    QVERIFY(model.ownershipAvailable());

    model.setOwnership(GameListModel::OwnedOnly);
    QCOMPARE(model.rowCount(), 0);
    model.setOwnership(GameListModel::MissingOnly);
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::clearFilters_restoresEverything()
{
    GameListModel model;
    feed(model);
    model.setOwnedGameKeys({ QStringLiteral("igdb-1") });

    model.setFilter(QStringLiteral("mario"));
    model.setPlatformFilter(QStringLiteral("snes"));
    model.setDecadeFilter(1990);
    model.setArcadeOnly(true);
    model.setOwnership(GameListModel::MissingOnly);

    // Les filtres de langue partent avec les autres : en oublier un laisserait une liste
    // filtrée derrière une barre qui affiche « tout ».
    feedLanguages(model);
    model.setLanguageFilter({ QStringLiteral("ja") });
    model.setLanguageOwnedOnly(true);

    model.clearFilters();
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.filter().isEmpty());
    QVERIFY(model.platformFilter().isEmpty());
    QCOMPARE(model.decadeFilter(), 0);
    QVERIFY(!model.arcadeOnly());
    QVERIFY(model.languageFilter().isEmpty());
    QVERIFY(!model.languageOwnedOnly());
}

void TestGameListModel::modes_areUnavailableOnAnExportWithoutThem()
{
    GameListModel model;
    feed(model); // catalogue sans setGameModes()

    QVERIFY(!model.modesAvailable());
    QVERIFY(model.availableModes().isEmpty());

    // Et un filtre posé sur un référentiel vide ne doit RIEN retenir de travers : il ne
    // filtre pas, plutôt que de vider la liste sans explication.
    model.setModeFilter({ QStringLiteral("multi") });
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::modeFilter_keepsOnlyGamesCarryingThatBit()
{
    GameListModel model;
    feedModes(model);

    QVERIFY(model.modesAvailable());
    QCOMPARE(model.availableModes(),
             QStringList({ QStringLiteral("solo"), QStringLiteral("multi"),
                           QStringLiteral("coop") }));

    // Seul igdb-2 porte le bit « multi ». igdb-3, qui n'a AUCUN masque, ne doit pas
    // passer : un jeu dont on ignore les modes n'est pas un jeu multijoueur.
    model.setModeFilter({ QStringLiteral("multi") });
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("The Legend of Zelda"));

    model.setModeFilter({ QStringLiteral("solo") });
    QCOMPARE(model.rowCount(), 2);

    // Un mode que personne ne porte donne zéro, et c'est la bonne réponse.
    model.setModeFilter({ QStringLiteral("coop") });
    QCOMPARE(model.rowCount(), 0);

    // Le libellé vient du référentiel, jamais d'une table écrite dans l'interface.
    QCOMPARE(model.modeLabel(QStringLiteral("coop")), QStringLiteral("Coopératif"));
    // Un mode inconnu rend sa clé plutôt que du vide : mieux vaut afficher « coop » qu'une
    // case blanche si le référentiel change sous nos pieds.
    QCOMPARE(model.modeLabel(QStringLiteral("mmo")), QStringLiteral("mmo"));
}

void TestGameListModel::modeFilter_multipleModesRequireThemAll()
{
    GameListModel model;
    feedModes(model);

    // ET binaire, comme les langues : solo ET multi ne laisse passer qu'igdb-2, alors que
    // chacun pris seul en laisserait passer davantage.
    model.setModeFilter({ QStringLiteral("solo"), QStringLiteral("multi") });
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("The Legend of Zelda"));

    model.setModeFilter({ QStringLiteral("solo"), QStringLiteral("coop") });
    QCOMPARE(model.rowCount(), 0);
}

void TestGameListModel::modeFilter_isClearedByClearFilters()
{
    GameListModel model;
    feedModes(model);

    model.setModeFilter({ QStringLiteral("multi") });
    QCOMPARE(model.rowCount(), 1);

    model.clearFilters();
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.modeFilter().isEmpty());
}

// Catalogue où lang_catalog_mask apporte ce que les dats ignorent.
//
//   igdb-1  ROM : ja(5)          catalogue : —
//   igdb-2  ROM : en,fr,de,ja    catalogue : —
//   igdb-3  ROM : —              catalogue : fr(1)   ← le cas des 6 879 jeux de production
void feedCatalogLanguages(GameListModel &model)
{
    auto games = sampleGames();
    games[2].langCatalogMask = 0b000010; // fr, connu du seul catalogue

    const QHash<QString, QStringList> platforms = {
        { QStringLiteral("igdb-1"), { QStringLiteral("gb") } },
        { QStringLiteral("igdb-2"), { QStringLiteral("nes") } },
        { QStringLiteral("igdb-3"), { QStringLiteral("snes") } },
    };
    model.setCatalogue(games, platforms, {});
    model.setLanguages(sampleLanguages(),
                       { { QStringLiteral("igdb-1"), 0b100000 },
                         { QStringLiteral("igdb-2"), 0b101011 } });
}

void TestGameListModel::catalogLanguages_widenExistsButNeverPlayable()
{
    GameListModel model;
    feedCatalogLanguages(model);

    // « EXISTE en français » : igdb-2 par ses ROMs, igdb-3 par le seul catalogue. Sans la
    // seconde source, igdb-3 serait invisible alors que le jeu existe bien en français —
    // c'est exactement la moitié du catalogue de production qui serait perdue.
    model.setLanguageFilter({ QStringLiteral("fr") });
    QCOMPARE(model.rowCount(), 2);

    // « JOUABLE en français » : rien ne change pour igdb-3, et c'est le point. IGDB ne
    // connaît ni release ni CRC ; aucune ROM ne peut donc rendre ce jeu jouable en
    // français, et le prétendre serait un mensonge invérifiable par l'utilisateur.
    model.setOwnedLanguageMasks({ { QStringLiteral("igdb-2"), 0b000010 } });
    model.setLanguageOwnedOnly(true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("The Legend of Zelda"));

    // Et le retour à « existe » réélargit : les deux filtres restent bien distincts.
    model.setLanguageOwnedOnly(false);
    QCOMPARE(model.rowCount(), 2);
}

void TestGameListModel::catalogLanguages_doNotLightUpBadges()
{
    GameListModel model;
    feedCatalogLanguages(model);

    // igdb-3 passe le filtre « existe en français », mais n'affiche AUCUN badge : un badge
    // gris qu'aucun téléchargement ne peut allumer serait un troisième état déguisé, et le
    // §8 n'en promet que deux.
    int row = -1;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.data(model.index(i, 0), GameListModel::TitleRole).toString()
            == QStringLiteral("Super Mario World"))
            row = i;
    }
    QVERIFY(row >= 0);
    QVERIFY(model.data(model.index(row, 0), GameListModel::LanguagesRole).toList().isEmpty());
    QCOMPARE(model.data(model.index(row, 0), GameListModel::ExtraLanguageCountRole).toInt(), 0);
}

// Les autres noms des trois jeux, tels que l'export 1.8.0 les donne.
//
//   igdb-3  « Super Mario World »  ← alias « smw » ET « smw usa », pour départager exact
//                                    et sous-chaîne
//   igdb-2  « The Legend of Zelda » ← alias « lttp »
//   igdb-1  aucun alias — plus d'un jeu sur deux est dans ce cas en production
QHash<QString, QList<igiris::catalog::GameAlias>> sampleAliases()
{
    using igiris::catalog::GameAlias;
    return {
        { QStringLiteral("igdb-2"), { { QStringLiteral("lttp"), QStringLiteral("LTTP") } } },
        { QStringLiteral("igdb-3"),
          { { QStringLiteral("smw usa"), QStringLiteral("SMW USA") },
            { QStringLiteral("smw"), QStringLiteral("SMW") } } },
    };
}

void TestGameListModel::aliases_findGamesTheTitleAloneCannot()
{
    GameListModel model;
    feed(model);

    // Sans alias, « lttp » ne peut RIEN trouver : aucun search_key ne le contient. C'est
    // exactement l'état d'avant le 1.8.0, et le motif de la demande.
    model.setFilter(QStringLiteral("lttp"));
    QCOMPARE(model.rowCount(), 0);

    model.setAliases(sampleAliases());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::TitleRole).toString(),
             QStringLiteral("The Legend of Zelda"));
}

void TestGameListModel::aliases_reportWhichOneMatched()
{
    GameListModel model;
    feed(model);
    model.setAliases(sampleAliases());

    // Trouvé PAR UN ALIAS : la ligne doit pouvoir dire lequel, sinon elle affiche un titre
    // qui ne contient aucun des caractères tapés et ça se lit comme un bug.
    model.setFilter(QStringLiteral("lttp"));
    QCOMPARE(model.data(model.index(0, 0), GameListModel::MatchedAliasRole).toString(),
             QStringLiteral("LTTP"));

    // Trouvé PAR SON TITRE : rien à expliquer, donc rien à afficher. Montrer un alias ici
    // serait du bruit sur une liste qui doit rester dense (§6).
    model.setFilter(QStringLiteral("zelda"));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.data(model.index(0, 0), GameListModel::MatchedAliasRole)
                .toString()
                .isEmpty());

    // Et sans recherche du tout, aucune ligne ne prétend avoir été trouvée par un alias.
    model.setFilter(QString());
    QCOMPARE(model.rowCount(), 3);
    for (int row = 0; row < model.rowCount(); ++row) {
        QVERIFY(model.data(model.index(row, 0), GameListModel::MatchedAliasRole)
                    .toString()
                    .isEmpty());
    }
}

void TestGameListModel::aliases_preferTheExactOneOverASubstring()
{
    GameListModel model;
    feed(model);
    model.setAliases(sampleAliases());

    // « smw usa » vient AVANT « smw » dans la liste, et contient lui aussi la saisie. Sans
    // règle, c'est lui qui serait affiché — alors que l'utilisateur a tapé l'autre.
    model.setFilter(QStringLiteral("smw"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::MatchedAliasRole).toString(),
             QStringLiteral("SMW"));

    // Une saisie qui ne correspond exactement à aucun alias retombe sur la sous-chaîne.
    model.setFilter(QStringLiteral("smw u"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), GameListModel::MatchedAliasRole).toString(),
             QStringLiteral("SMW USA"));
}

void TestGameListModel::aliases_areAbsentFromAnOlderExportWithoutBreakingIt()
{
    GameListModel model;
    feed(model); // catalogue sans setAliases()

    // Un export antérieur au 1.8.0 : la recherche porte sur le titre seul, et rien ne
    // prétend le contraire. Les mineures sont additives (§2).
    model.setFilter(QStringLiteral("zelda"));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.data(model.index(0, 0), GameListModel::MatchedAliasRole)
                .toString()
                .isEmpty());
}

QTEST_MAIN(TestGameListModel)
#include "test_game_list_model.moc"
