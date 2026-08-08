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
};

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

    model.clearFilters();
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.filter().isEmpty());
    QVERIFY(model.platformFilter().isEmpty());
    QCOMPARE(model.decadeFilter(), 0);
    QVERIFY(!model.arcadeOnly());
}

QTEST_MAIN(TestGameListModel)
#include "test_game_list_model.moc"
