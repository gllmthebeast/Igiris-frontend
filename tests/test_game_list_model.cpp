// Tests du lot 5 — modèle de la liste d'accueil.
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
};

void TestGameListModel::model_respectsAbstractItemModelContract()
{
    GameListModel model;
    // Un modèle mal formé casse les vues QML de façon obscure : autant le vérifier ici.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Warning);
    model.setGames(sampleGames());
    model.setFilter(QStringLiteral("mario"));
    model.setFilter(QString());
}

void TestGameListModel::model_exposesAllGamesByDefault()
{
    GameListModel model;
    model.setGames(sampleGames());

    // §0 : tous les jeux du catalogue sont affichés, ROM présente ou non.
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.totalCount(), 3);
    QCOMPARE(model.visibleCount(), 3);
}

void TestGameListModel::roles_areNamedForQml()
{
    GameListModel model;
    model.setGames(sampleGames());

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
    model.setGames(sampleGames());

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
    model.setGames(sampleGames());

    model.setFilter(QStringLiteral("MARIO"));
    QCOMPARE(model.rowCount(), 1);
}

void TestGameListModel::filter_emptyRestoresEverything()
{
    GameListModel model;
    model.setGames(sampleGames());

    model.setFilter(QStringLiteral("mario"));
    QCOMPARE(model.rowCount(), 1);
    model.setFilter(QStringLiteral("   ")); // des espaces ne sont pas un filtre
    QCOMPARE(model.rowCount(), 3);
}

void TestGameListModel::filter_noMatchLeavesEmptyButKnowsTotal()
{
    GameListModel model;
    model.setGames(sampleGames());

    model.setFilter(QStringLiteral("zzzz"));
    QCOMPARE(model.rowCount(), 0);
    // L'interface doit pouvoir dire « aucun résultat sur N jeux », pas « catalogue vide ».
    QCOMPARE(model.totalCount(), 3);
}

void TestGameListModel::filter_emitsSignalsOnce()
{
    GameListModel model;
    model.setGames(sampleGames());

    QSignalSpy filterSpy(&model, &GameListModel::filterChanged);
    model.setFilter(QStringLiteral("mario"));
    QCOMPARE(filterSpy.count(), 1);

    // Reposer le même filtre ne doit rien réémettre : sinon chaque frappe reconstruirait
    // la vue sans raison, et le §6 exige que ça reste interactif à la manette.
    model.setFilter(QStringLiteral("mario"));
    QCOMPARE(filterSpy.count(), 1);
}

QTEST_MAIN(TestGameListModel)
#include "test_game_list_model.moc"
