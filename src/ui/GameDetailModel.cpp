#include "ui/GameDetailModel.h"

#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace igiris::ui {

namespace {

// Clé composite (jeu, plateforme) pour l'index des ROMs possédées. Le séparateur est un
// caractère de contrôle, absent de toute clé réelle.
QString ownedKey(const QString &gameKey, const QString &platformKey)
{
    return gameKey + QLatin1Char('\x1f') + platformKey;
}

} // namespace

GameDetailModel::GameDetailModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void GameDetailModel::setCatalogue(const catalog::ExportDatabase *db)
{
    m_db = db;
}

void GameDetailModel::setAdapter(const platform::PlatformAdapter *adapter)
{
    m_adapter = adapter;
    emit capabilitiesChanged();
}

void GameDetailModel::setLocalSystems(QHash<QString, platform::SystemEntry> systems)
{
    m_localSystems = std::move(systems);
    if (!m_gameKey.isEmpty())
        setGame(m_gameKey); // recalculer les statuts
    emit capabilitiesChanged();
}

void GameDetailModel::setOwnedRoms(QHash<QString, QString> ownedRoms)
{
    m_ownedRoms = std::move(ownedRoms);
    if (!m_gameKey.isEmpty())
        setGame(m_gameKey);
}

void GameDetailModel::setLanguages(QList<catalog::Language> languages)
{
    m_languages = std::move(languages);
    if (!m_gameKey.isEmpty())
        setGame(m_gameKey);
}

void GameDetailModel::setOwnedRomKeys(QSet<QString> ownedRomKeys)
{
    m_ownedRomKeys = std::move(ownedRomKeys);
    if (!m_gameKey.isEmpty())
        setGame(m_gameKey);
}

int GameDetailModel::languageRank(const QString &code) const
{
    for (const catalog::Language &language : m_languages) {
        if (language.code != code)
            continue;
        // Sans bit : reléguée en fin, mais AFFICHÉE. La fiche lit exp_game_language
        // directement, elle n'a donc pas la limite du masque que subit la vue liste.
        return language.hasBit() ? language.bitIndex : 1000;
    }
    return 2000; // code absent du référentiel : dernier, mais jamais perdu
}

void GameDetailModel::setGame(const QString &gameKey)
{
    beginResetModel();
    m_rows.clear();
    m_gameKey = gameKey;
    m_title.clear();
    m_rating = 0;

    if (m_db && !gameKey.isEmpty()) {
        if (const auto game = m_db->gameByKey(gameKey)) {
            m_title  = game->title;
            m_rating = game->rating;
        }

        // Les langues du jeu, regroupées par plateforme. Une plateforme peut porter
        // PLUSIEURS ROMs pour la même langue (une européenne, une américaine) : la langue
        // est illuminée dès qu'UNE d'entre elles est possédée — d'où le OU, et non le
        // dernier vu qui l'écraserait.
        QHash<QString, QHash<QString, bool>> languagesByPlatform;
        for (const auto &language : m_db->languagesForGame(gameKey)) {
            const bool owned = m_ownedRomKeys.contains(
                catalog::romKey(language.crc32, language.platformKey));
            bool &state = languagesByPlatform[language.platformKey][language.langCode];
            state       = state || owned;
        }

        QSet<QString> seen; // exp_game_platform a pour clé (game_key, display_name)

        for (const auto &platform : m_db->platformsForGame(gameKey)) {
            if (!platform.isEmulationTarget())
                continue; // plateforme d'origine non émulée : ni verte, ni rouge, ni noire
            if (seen.contains(platform.platformKey))
                continue;
            seen.insert(platform.platformKey);

            Row row;
            row.platformKey = platform.platformKey;
            row.displayName = platform.displayName;
            row.emuScore    = platform.emuScore;
            row.isPreferred = platform.isPreferred;
            row.romPath     = m_ownedRoms.value(ownedKey(gameKey, platform.platformKey));

            const auto languages = languagesByPlatform.value(platform.platformKey);
            QStringList codes    = languages.keys();
            std::sort(codes.begin(), codes.end(),
                      [this, &languages](const QString &a, const QString &b) {
                          // Possédée d'abord, puis l'ordre du catalogue : le même ordre
                          // qu'en vue liste, pour que l'œil retrouve les mêmes badges.
                          if (languages.value(a) != languages.value(b))
                              return languages.value(a);
                          const int rankA = languageRank(a), rankB = languageRank(b);
                          return rankA != rankB ? rankA < rankB : a < b;
                      });
            for (const QString &code : codes) {
                QVariantMap badge;
                badge.insert(QStringLiteral("code"), code);
                badge.insert(QStringLiteral("owned"), languages.value(code));
                row.languages.append(badge);
            }

            const auto system = m_localSystems.constFind(platform.platformKey);
            if (system == m_localSystems.cend()) {
                // Absent du fichier de description : NOIR. C'est ce fichier qui tranche,
                // pas le catalogue (§1).
                row.status = Black;
            } else {
                row.status = row.romPath.isEmpty() ? Red : Green;
                if (!system->launchOptions.isEmpty())
                    row.launchLabel = system->launchOptions.first().label;
            }

            m_rows.append(row);
        }
    }
    // Le §7 veut « le système marqué is_preferred proposé par défaut ». Impossible en
    // l'état : l'export marque is_preferred sur 18 116 des 18 555 lignes, et 3 932 jeux
    // ont PLUSIEURS plateformes élues. Le champ ne discrimine rien.
    //
    // Le défaut retenu est donc le premier système JOUABLE — vert, puis meilleur score
    // d'émulation, l'ordre venant déjà de la requête. À défaut de vert, la première
    // ligne. Voir la note du lot 7 dans docs/LOTS.md.
    int defaultRow = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).status == Green) {
            defaultRow = i;
            break;
        }
    }
    if (defaultRow < 0 && !m_rows.isEmpty())
        defaultRow = 0;
    if (defaultRow >= 0)
        m_rows[defaultRow].isDefaultChoice = true;

    endResetModel();
    emit gameChanged();
}

bool GameDetailModel::launchAvailable() const
{
    return m_adapter && m_adapter->supports(platform::Capability::Launch)
        && !m_localSystems.isEmpty();
}

QString GameDetailModel::launchWarning() const
{
    if (!m_adapter)
        return tr("aucune distribution reconnue : lancement impossible");
    if (m_localSystems.isEmpty())
        return tr("fichier de description des systèmes absent : statuts et lancement "
                  "indisponibles");
    // Capacité NON déclarée : l'émulateur retombera sur sa configuration par défaut.
    // Le §1 impose de le dire plutôt que de laisser croire que tout est transmis.
    if (!m_adapter->supports(platform::Capability::ControllerMapping))
        return tr("manettes non transmises : l'émulateur utilisera sa configuration "
                  "par défaut");
    return {};
}

QString GameDetailModel::commandPreview(int row) const
{
    if (!m_adapter || row < 0 || row >= m_rows.size())
        return {};

    const Row  &entry  = m_rows.at(row);
    const auto  system = m_localSystems.constFind(entry.platformKey);
    if (system == m_localSystems.cend())
        return tr("système absent de cette installation");
    if (entry.romPath.isEmpty())
        return tr("aucune ROM possédée pour cette plateforme");

    const platform::LaunchCommand command =
        m_adapter->buildLaunchCommand(*system, entry.romPath, { m_title, 0 });

    if (!command.unresolvedPlaceholders.isEmpty())
        return tr("placeholders non reconnus : %1")
            .arg(command.unresolvedPlaceholders.join(QStringLiteral(", ")));

    // Les arguments sont affichés séparés par des « ␣ » visibles plutôt que par des
    // espaces : c'est justement leur séparation qui est la propriété à vérifier.
    return command.program + QStringLiteral("\n    ")
         + command.arguments.join(QStringLiteral("\n    "));
}

QString GameDetailModel::launch(int row)
{
    if (!m_adapter)
        return tr("aucune distribution reconnue");
    if (row < 0 || row >= m_rows.size())
        return tr("ligne invalide");

    const Row  &entry  = m_rows.at(row);
    const auto  system = m_localSystems.constFind(entry.platformKey);
    if (system == m_localSystems.cend())
        return tr("système absent de cette installation");
    if (entry.status != Green)
        return tr("le lancement ne part que d'un système en vert (§7)");

    QString error;
    if (!m_adapter->launch(*system, entry.romPath, { m_title, 0 }, &error))
        return error; // verbatim (§15)
    return {};
}

int GameDetailModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant GameDetailModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());

    switch (role) {
    case PlatformKeyRole:
        return row.platformKey;
    case DisplayNameRole:
        return row.displayName;
    case EmuScoreRole:
        return row.emuScore;
    case PreferredRole:
        return row.isPreferred;
    case StatusRole:
        return static_cast<int>(row.status);
    case RomPathRole:
        return row.romPath;
    case LaunchLabelRole:
        return row.launchLabel;
    case DefaultChoiceRole:
        return row.isDefaultChoice;
    case LanguagesRole:
        return row.languages;
    default:
        return {};
    }
}

QHash<int, QByteArray> GameDetailModel::roleNames() const
{
    return {
        { PlatformKeyRole, "platformKey" }, { DisplayNameRole, "displayName" },
        { EmuScoreRole, "emuScore" },       { PreferredRole, "isPreferred" },
        { StatusRole, "status" },           { RomPathRole, "romPath" },
        { LaunchLabelRole, "launchLabel" },
        { DefaultChoiceRole, "isDefaultChoice" },
        { LanguagesRole, "languages" },
    };
}

} // namespace igiris::ui
