#include "ui/GameDetailModel.h"

#include <QFileInfo>
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

void GameDetailModel::setCoversDirectory(const QString &directory)
{
    m_coversDir = directory;
    if (!m_gameKey.isEmpty())
        setGame(m_gameKey);
}

void GameDetailModel::setGameModes(QList<catalog::GameMode> modes)
{
    m_modes = std::move(modes);
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
    m_year   = 0;
    m_coverRef.clear();
    m_artworkRef.clear();
    m_summary.clear();
    m_modeLabels.clear();
    m_catalogLanguages.clear();
    m_nonEmulablePlatforms.clear();

    quint64 catalogMask = 0;

    if (m_db && !gameKey.isEmpty()) {
        if (const auto game = m_db->gameByKey(gameKey)) {
            catalogMask = game->langCatalogMask;
            m_title      = game->title;
            m_rating     = game->rating;
            m_year       = game->year;
            m_coverRef   = game->coverRef;

            // La vignette LOCALE d'abord : c'est ce qui rend la fiche lisible sans réseau.
            if (!m_coversDir.isEmpty()) {
                const QString local =
                    QStringLiteral("%1/%2.jpg").arg(m_coversDir, gameKey);
                if (QFileInfo::exists(local))
                    m_coverRef = QStringLiteral("file://") + local;
            }
            m_artworkRef = game->artworkRef;

            // Le fond local, s'il a été mis en cache. Rangé sous « <clé>-fond.jpg » et non
            // dans un second répertoire : le nom du fichier reste la clé du jeu, et il n'y
            // a toujours qu'un lookup à faire (§0).
            //
            // Pris PAR DÉFAUT depuis la 1.15.0, en t_screenshot_med : 500 Mo, contre 66
            // pour les vignettes. C'est ce qui achève le hors-ligne de la fiche — une
            // seule chose manquante suffisait à le faire mentir. Qui n'a pas la place
            // installe avec IGIRIS_WITH_ARTWORK=0, et la fiche s'affiche quand même.
            if (!m_coversDir.isEmpty() && !m_artworkRef.isEmpty()) {
                const QString local =
                    QStringLiteral("%1/%2-fond.jpg").arg(m_coversDir, gameKey);
                if (QFileInfo::exists(local))
                    m_artworkRef = QStringLiteral("file://") + local;
            }
            m_summary    = game->summary;

            // Les libellés viennent du référentiel, jamais d'une table écrite ici : le
            // bit_index est attribué à vie par le backend, et déduire un mode de sa
            // position produirait un libellé faux, silencieusement — exactement le piège
            // déjà posé pour les langues (§8).
            for (const auto &mode : m_modes) {
                if (mode.hasBit() && (game->modeMask & mode.bit()))
                    m_modeLabels.append(mode.label);
            }
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

        // Les langues que SEUL le catalogue connaît (export 1.7.0).
        //
        // On retire celles qu'une ROM fournit déjà : elles ont leur badge sur la ligne de
        // leur plateforme, avec l'état illuminé / grisé qui va avec. Ne restent ici que
        // celles qu'aucun CRC ne porte — donc celles qu'aucun téléchargement n'allumera.
        // Les afficher à part est la seule façon de les montrer sans promettre ça.
        QSet<QString> providedByRoms;
        for (const auto &byPlatform : languagesByPlatform) {
            for (auto it = byPlatform.cbegin(); it != byPlatform.cend(); ++it)
                providedByRoms.insert(it.key());
        }
        for (const auto &language : m_languages) {
            if (!language.hasBit() || !(catalogMask & language.bit()))
                continue;
            if (providedByRoms.contains(language.code))
                continue;
            m_catalogLanguages.append(language.code);
        }

        // Les informations d'arcade, par plateforme. Elles existent dans l'export depuis le
        // début (§4) et n'étaient affichées nulle part : le matériel réel, les émulateurs
        // capables de lancer le romset, et l'état du pilote.
        QHash<QString, catalog::Romset> romsetsByPlatform;
        for (const auto &romset : m_db->romsetsForGame(gameKey))
            romsetsByPlatform.insert(romset.platformKey, romset);

        QSet<QString> seen; // exp_game_platform a pour clé (game_key, display_name)

        for (const auto &platform : m_db->platformsForGame(gameKey)) {
            if (!platform.isEmulationTarget()) {
                // Plateforme d'origine non émulée : ni verte, ni rouge, ni noire — elle
                // n'entre pas dans la liste des systèmes, qui ne parle que de lançable.
                //
                // On la GARDE de côté malgré tout. Depuis le 1.7.0 elles sont 40 511, et
                // 9 679 jeux n'ont QUE celles-là : sans elles, leur fiche montrerait une
                // zone vide sans un mot d'explication.
                if (!m_nonEmulablePlatforms.contains(platform.displayName))
                    m_nonEmulablePlatforms.append(platform.displayName);
                continue;
            }
            if (seen.contains(platform.platformKey))
                continue;
            seen.insert(platform.platformKey);

            Row row;
            row.platformKey = platform.platformKey;
            row.displayName = platform.displayName;
            row.emuScore    = platform.emuScore;
            row.isPreferred = platform.isPreferred;
            row.releaseYear = platform.releaseYear;
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

            const auto romset = romsetsByPlatform.constFind(platform.platformKey);
            if (romset != romsetsByPlatform.cend()) {
                row.hardware     = romset->hardware;
                row.emulators    = romset->emulators;
                row.driverStatus = romset->driverStatus;
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
    // Le §7 veut « le système marqué is_preferred proposé par défaut ». Le champ est
    // désormais fiable — le backend l'a corrigé en 1.4.0, il ne marque plus que 31 lignes,
    // sans jeu à double élection — mais il ne tranche donc que pour 31 lignes sur 18 555.
    //
    // Le défaut reste le premier système JOUABLE : vert, puis is_preferred, puis meilleur
    // score d'émulation — l'ordre vient déjà de la requête, donc une plateforme élue ET
    // verte sort naturellement en tête. À défaut de vert, la première ligne.
    //
    // Ce n'est plus un pis-aller : proposer par défaut un système dont l'utilisateur n'a
    // pas la ROM ne serait pas lançable, alors que c'est l'unique geste de cette fiche.
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
    case HardwareRole:
        return row.hardware;
    case EmulatorsRole:
        return row.emulators;
    case DriverStatusRole:
        return row.driverStatus;
    case ReleaseYearRole:
        return row.releaseYear;
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
        { LanguagesRole, "languages" },   { HardwareRole, "hardware" },
        { EmulatorsRole, "emulators" },   { DriverStatusRole, "driverStatus" },
        { ReleaseYearRole, "releaseYear" },
    };
}

} // namespace igiris::ui
