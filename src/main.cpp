// igiris-frontend — point d'entrée.
//
// Lot 0 : le squelette. Il démarre, il annonce ses versions, il charge une vue QML vide.
// Rien d'autre — surtout pas de logique métier ici.
//
// Note d'architecture (CLAUDE.md §12) : très peu de C++. Cette couche expose les données
// à QML et rien de plus ; toute l'interface est en QML.

#include <cstdio>
#include <cstring>

#include <sqlite3.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QTimer>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QUrl>

#include "catalog/ExportDatabase.h"
#include "scan/RomScanner.h"
#include "scan/ScanCache.h"
#include "platform/AdapterRegistry.h"
#include "systems/EsSystemsParser.h"
#include "ui/GameDetailModel.h"
#include "ui/GameListModel.h"
#include "version.h"

namespace {

// --version doit répondre sans écran : c'est le seul contrôle possible sur une machine
// de build headless, et c'est le livrable vérifiable du lot 0.
void printVersion()
{
    std::printf("igiris-frontend %s\n", IGIRIS_FRONTEND_VERSION);
    std::printf("  Qt                        %s\n", qVersion());
    std::printf("  SQLite                    %s\n", sqlite3_libversion());
    std::printf("  export : schéma majeur    %d\n", IGIRIS_SUPPORTED_EXPORT_MAJOR);
}

// Livrable du lot 2 : lister les systèmes et leurs commandes de lancement.
//
// Sans écran, donc utilisable sur une machine de build comme sur l'appareil. Le chemin
// peut être donné en argument — c'est ce qui permet d'inspecter le fichier d'une autre
// distribution sans être dessus.
int runSystemsCommand(const QString &explicitPath)
{
    QString path = explicitPath;

    if (path.isEmpty()) {
        const auto adapter = igiris::platform::detectAdapter();
        if (!adapter) {
            std::fprintf(stderr,
                         "✗ aucune distribution reconnue ici (connues : %s).\n"
                         "  Donnez un chemin : igiris-frontend --systems <fichier>\n",
                         qPrintable(igiris::platform::knownAdapterIds().join(u", ")));
            return 1;
        }
        std::printf("distribution détectée : %s\n", qPrintable(adapter->displayName()));
        path = adapter->systemsFilePath();
        if (path.isEmpty()) {
            std::fprintf(stderr, "✗ fichier de description des systèmes introuvable.\n");
            return 1;
        }
    }

    const auto result = igiris::systems::parseEsSystemsFile(path);
    if (!result.ok()) {
        // §15 : verbatim, avec la position.
        std::fprintf(stderr, "✗ %s (ligne %lld, colonne %lld)\n",
                     qPrintable(result.error.message),
                     static_cast<long long>(result.error.line),
                     static_cast<long long>(result.error.column));
        return 1;
    }

    std::printf("%s\n%lld systèmes\n\n", qPrintable(path),
                static_cast<long long>(result.systems.size()));

    for (const auto &system : result.systems) {
        std::printf("%-18s %s\n", qPrintable(system.name), qPrintable(system.fullName));
        std::printf("    roms       %s\n", qPrintable(system.romPath));
        std::printf("    extensions %lld\n",
                    static_cast<long long>(system.extensions.size()));
        for (const auto &option : system.launchOptions) {
            std::printf("    [%s] %s\n",
                        option.label.isEmpty() ? "défaut" : qPrintable(option.label),
                        qPrintable(option.command));
        }
        std::printf("\n");
    }

    // Diagnostic : ce qu'un adaptateur devra savoir substituer pour CE fichier.
    const auto placeholders = igiris::systems::collectCommandPlaceholders(result.systems);
    std::printf("placeholders rencontrés (%lld) : %s\n",
                static_cast<long long>(placeholders.size()),
                qPrintable(placeholders.join(u" ")));

    for (const QString &warning : result.warnings)
        std::fprintf(stderr, "⚠ %s\n", qPrintable(warning));

    return 0;
}

// Livrable du lot 3 : rejouer en C++ les quatre requêtes types du §3, sur le vrai export.
// C'est l'équivalent de tools/probe.py, mais avec le code que l'appareil exécutera.
int runExportCommand(const QString &path)
{
    igiris::catalog::ExportDatabase db;
    QString                         error;

    if (!db.open(path, &error)) {
        std::fprintf(stderr, "✗ %s\n", qPrintable(error)); // verbatim (§15)
        return 1;
    }

    const auto &meta = db.meta();
    std::printf("%s\n", qPrintable(path));
    std::printf("  schéma %s · généré %s\n", qPrintable(meta.schemaVersion),
                qPrintable(meta.generatedAt));
    std::printf("  %d jeux · %d plateformes · %d hashes · %d romsets arcade\n\n", meta.games,
                meta.platforms, meta.romHashes, meta.arcadeRomsets);

    const auto keys = db.allPlatformKeys();
    std::printf("clés de plateforme émulables : %lld\n\n",
                static_cast<long long>(keys.size()));

    // Requête 3 — recherche par nom. Sert d'amorce aux trois autres.
    const auto games = db.searchByName(QStringLiteral("mario"), 3);
    std::printf("[3] recherche « mario » → %lld résultats\n",
                static_cast<long long>(games.size()));
    for (const auto &game : games)
        std::printf("    %-40s %d  note %d\n", qPrintable(game.title), game.year,
                    game.rating);

    if (games.isEmpty()) {
        std::fprintf(stderr, "\n✗ aucun jeu trouvé : export vide ou inattendu.\n");
        return 1;
    }

    // Requête 4 — plateformes du premier résultat.
    const auto &first     = games.first();
    const auto  platforms = db.platformsForGame(first.gameKey);
    std::printf("\n[4] plateformes de « %s »\n", qPrintable(first.title));
    for (const auto &platform : platforms) {
        std::printf("    %-32s %-12s emu %3d%s\n", qPrintable(platform.displayName),
                    platform.isEmulationTarget() ? qPrintable(platform.platformKey)
                                                 : "(non émulée)",
                    platform.emuScore, platform.isPreferred ? "  ← élue" : "");
    }

    // Requête 1 — aller-retour réel : on prend un CRC de l'export et on le relit par le
    // chemin exact que suivra l'appareil. Un échec ici signifierait que le lookup à chaud
    // est cassé, ce qu'aucune requête d'affichage ne révélerait.
    int checked = 0, matched = 0;
    for (const auto &game : games) {
        for (const auto &hash : db.romHashesForGame(game.gameKey)) {
            ++checked;
            const auto found = db.findByCrc(hash.crc32, hash.platformKey);
            if (found && found->gameKey == game.gameKey)
                ++matched;
            if (checked == 1)
                std::printf("\n[1] lookup CRC %s sur « %s » → %s (header_skip %d)\n",
                            qPrintable(hash.crc32), qPrintable(hash.platformKey),
                            found ? qPrintable(found->title) : "AUCUN", hash.headerSkip);
        }
    }
    std::printf("    aller-retour CRC : %d/%d cohérents\n", matched, checked);

    // Requête 2 — même principe pour l'arcade, identifiée par NOM et jamais par CRC (§4).
    int romsetsChecked = 0, romsetsMatched = 0;
    for (const auto &game : db.searchByName(QStringLiteral("street"), 20)) {
        for (const auto &romset : db.romsetsForGame(game.gameKey)) {
            ++romsetsChecked;
            const auto found = db.findByRomset(romset.romset, romset.platformKey);
            if (found && found->gameKey == game.gameKey)
                ++romsetsMatched;
            if (romsetsChecked == 1)
                std::printf("\n[2] lookup romset « %s » sur « %s » → %s [%s, %s]\n",
                            qPrintable(romset.romset), qPrintable(romset.platformKey),
                            found ? qPrintable(found->title) : "AUCUN",
                            qPrintable(romset.hardware), qPrintable(romset.driverStatus));
        }
    }
    std::printf("    aller-retour romset : %d/%d cohérents\n", romsetsMatched,
                romsetsChecked);

    if (checked > 0 && matched != checked)
        return 1;
    if (romsetsChecked > 0 && romsetsMatched != romsetsChecked)
        return 1;

    return 0;
}

// Livrable du lot 4 : le rapport vert / rouge / noir (§7).
//
//   vert   système présent sur cette installation ET ROM présente
//   rouge  système présent, ROM absente
//   noir   système absent de cette installation
//
// « Présent sur cette installation » se lit ici dans les dossiers de ROMs. Au lot 7, ce
// sera le fichier de description des systèmes qui tranchera, comme l'exige le §1.
int runScanCommand(const QString &romsDir, const QString &exportPath)
{
    igiris::catalog::ExportDatabase db;
    QString                         error;
    if (!db.open(exportPath, &error)) {
        std::fprintf(stderr, "✗ %s\n", qPrintable(error));
        return 1;
    }

    const QStringList knownKeys = db.allPlatformKeys();

    // Un sous-dossier par système, à la façon EmulationStation.
    QList<igiris::scan::ScanTarget> targets;
    QStringList                     localSystems;
    const QDir                      root(romsDir);
    for (const QString &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!knownKeys.contains(entry))
            continue; // dossier qui ne correspond à aucune plateforme de l'export
        localSystems.append(entry);
        targets.append({ entry, root.absoluteFilePath(entry) });
    }

    if (targets.isEmpty()) {
        std::fprintf(stderr, "✗ aucun dossier de système reconnu sous %s\n",
                     qPrintable(romsDir));
        return 1;
    }

    igiris::scan::ScanCache cache;
    if (!cache.open(romsDir + QStringLiteral("/.igiris-scan-cache.db"), &error))
        std::fprintf(stderr, "⚠ %s — scan sans cache\n", qPrintable(error));

    igiris::scan::RomScanner scanner(db, cache.isOpen() ? &cache : nullptr);
    const auto               report = scanner.scan(targets);

    std::printf("%s\n  %lld systèmes · %d fichiers · %d hachés · %d depuis le cache\n\n",
                qPrintable(romsDir), static_cast<long long>(targets.size()),
                report.filesSeen, report.hashed, report.cacheHits);

    // Quelles ROMs possède-t-on, et pour quelle plateforme.
    QSet<QString> ownedPairs;
    for (const auto &rom : report.identified)
        ownedPairs.insert(rom.gameKey + QLatin1Char('\x1f') + rom.platformKey);

    int green = 0, red = 0, black = 0;
    std::printf("jeux possédés :\n");

    for (const QString &gameKey : report.ownedGameKeys()) {
        QString title;
        QString line;
        // Dédoublonnage par platformKey : un même jeu peut avoir plusieurs lignes pour la
        // même plateforme (exp_game_platform a pour clé game_key + display_name, donc
        // « SNES » et « SFAM » coexistent). Sans ça, les pastilles et les compteurs
        // seraient doublés.
        QSet<QString> seenPlatforms;
        for (const auto &platform : db.platformsForGame(gameKey)) {
            if (!platform.isEmulationTarget())
                continue; // plateforme d'origine non émulée : ni verte, ni rouge, ni noire
            if (seenPlatforms.contains(platform.platformKey))
                continue;
            seenPlatforms.insert(platform.platformKey);

            const bool systemPresent = localSystems.contains(platform.platformKey);
            const bool romPresent =
                ownedPairs.contains(gameKey + QLatin1Char('\x1f') + platform.platformKey);

            const char *mark = nullptr;
            if (!systemPresent) {
                mark = "⬛";
                ++black;
            } else if (romPresent) {
                mark = "🟩";
                ++green;
            } else {
                mark = "🟥";
                ++red;
            }
            line += QStringLiteral(" %1%2").arg(QString::fromUtf8(mark), platform.platformKey);
        }
        for (const auto &rom : report.identified) {
            if (rom.gameKey == gameKey) {
                title = rom.title;
                break;
            }
        }
        std::printf("  %-44s%s\n", qPrintable(title), qPrintable(line));
    }

    std::printf("\n  🟩 %d possédés · 🟥 %d manquants sur système présent · ⬛ %d système absent\n",
                green, red, black);

    // Par quel chemin chaque ROM a été reconnue. C'est LE diagnostic de ce lot : une
    // collection identifiée uniquement par CRC direct signalerait que les reprises
    // d'en-tête ne se déclenchent pas, alors que rien ne planterait.
    QHash<int, int> byKind;
    for (const auto &rom : report.identified)
        byKind[static_cast<int>(rom.kind)]++;

    const struct {
        igiris::scan::MatchKind kind;
        const char             *label;
    } kinds[] = {
        { igiris::scan::MatchKind::Crc, "CRC direct" },
        { igiris::scan::MatchKind::CrcHeaderSkip, "CRC après en-tête ignoré (export)" },
        { igiris::scan::MatchKind::CrcSmcHeuristic, "CRC après en-tête SMC (heuristique)" },
        { igiris::scan::MatchKind::ZipEntryCrc, "CRC lu dans le zip, sans décompression" },
        { igiris::scan::MatchKind::Romset, "nom de romset (arcade)" },
    };
    std::printf("\nidentification :\n");
    for (const auto &entry : kinds) {
        const int count = byKind.value(static_cast<int>(entry.kind), 0);
        if (count > 0)
            std::printf("  %s %d\n",
                        qPrintable(QString::fromUtf8(entry.label).leftJustified(40)), count);
    }

    if (!report.unidentified.isEmpty()) {
        std::printf("\nnon reconnus (%lld) :\n",
                    static_cast<long long>(report.unidentified.size()));
        for (int i = 0; i < qMin<qsizetype>(10, report.unidentified.size()); ++i)
            std::printf("  %s\n", qPrintable(report.unidentified.at(i)));
    }

    for (const QString &message : report.errors)
        std::fprintf(stderr, "⚠ %s\n", qPrintable(message)); // verbatim (§15)

    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            printVersion();
            return 0;
        }
        if (std::strcmp(argv[i], "--scan") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "usage : igiris-frontend --scan <dossier-roms> [export.db]\n");
                return 1;
            }
            const QString roms = QString::fromLocal8Bit(argv[i + 1]);
            const QString exp  = (i + 2 < argc) ? QString::fromLocal8Bit(argv[i + 2])
                                                : QStringLiteral("data/games.db");
            return runScanCommand(roms, exp);
        }
        if (std::strcmp(argv[i], "--export") == 0) {
            const QString path = (i + 1 < argc)
                                     ? QString::fromLocal8Bit(argv[i + 1])
                                     : QStringLiteral("data/games.db");
            return runExportCommand(path);
        }
        if (std::strcmp(argv[i], "--systems") == 0) {
            const QString path = (i + 1 < argc) ? QString::fromLocal8Bit(argv[i + 1])
                                                : QString();
            return runSystemsCommand(path);
        }
    }

    // Capture d'écran : rendu sans écran ni GPU, pour juger l'interface depuis une
    // machine de build. Doit être posé AVANT la construction de QGuiApplication.
    QString screenshotPath;
    QString initialFilter;
    for (int i = 1; i < argc; ++i) {
        // L'aperçu de lancement n'ouvre aucune fenêtre, mais il construit une
        // QGuiApplication : sans écran, il lui faut la plateforme offscreen.
        if (std::strcmp(argv[i], "--launch-preview") == 0)
            qputenv("QT_QPA_PLATFORM", "offscreen");
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = QString::fromLocal8Bit(argv[i + 1]);
            if (i + 2 < argc)
                initialFilter = QString::fromLocal8Bit(argv[i + 2]);
            qputenv("QT_QPA_PLATFORM", "offscreen");
            qputenv("QT_QUICK_BACKEND", "software");
        }
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("igiris-frontend"));
    QGuiApplication::setApplicationVersion(QStringLiteral(IGIRIS_FRONTEND_VERSION));

    // Chargement du catalogue. Le §17 fait de la liste nue le point de référence de
    // performance : on mesure donc ce que coûte l'alimentation du modèle.
    igiris::catalog::ExportDatabase catalogue;
    igiris::ui::GameListModel       games;
    igiris::ui::GameDetailModel     detail;
    QString                         catalogueError;

    // L'adaptateur : détecté sur la machine, ou forcé sur une racine factice (image
    // montée, clone de référence) via --root. C'est ce qui rend la fiche de jeu
    // vérifiable ici (§15).
    QString rootPrefix = QStringLiteral("/");
    QString systemsFileOverride;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc)
            rootPrefix = QString::fromLocal8Bit(argv[i + 1]);
        else if (std::strcmp(argv[i], "--systems-file") == 0 && i + 1 < argc)
            systemsFileOverride = QString::fromLocal8Bit(argv[i + 1]);
    }
    QString forcedDistro;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--distro") == 0 && i + 1 < argc)
            forcedDistro = QString::fromLocal8Bit(argv[i + 1]);
    }
    const auto adapter = forcedDistro.isEmpty()
                             ? igiris::platform::detectAdapter(rootPrefix)
                             : igiris::platform::adapterById(forcedDistro, rootPrefix);

    QElapsedTimer timer;
    timer.start();
    if (catalogue.open(QStringLiteral("data/games.db"), &catalogueError)) {
        const qint64 opened = timer.elapsed();
        // Les index des filtres STATIQUES sont construits ici, une fois : le §6 exige
        // qu'une combinaison de filtres reste interactive à la manette.
        games.setCatalogue(catalogue.allGames(), catalogue.platformKeysByGame(),
                           catalogue.arcadePlatformKeys());
        std::printf("catalogue : %d jeux · %lld plateformes · ouverture %lld ms · "
                    "chargement %lld ms\n",
                    games.totalCount(),
                    static_cast<long long>(games.availablePlatforms().size()),
                    static_cast<long long>(opened),
                    static_cast<long long>(timer.elapsed() - opened));

        // Filtre DYNAMIQUE : il n'existe qu'après un scan local. Sans --roms, l'interface
        // le signale au lieu de proposer un filtre qui ne filtrerait rien.
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--roms") != 0 || i + 1 >= argc)
                continue;
            const QString romsDir = QString::fromLocal8Bit(argv[i + 1]);

            QList<igiris::scan::ScanTarget> targets;
            const QStringList knownKeys = catalogue.allPlatformKeys();
            const QDir        root(romsDir);
            for (const QString &entry :
                 root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (knownKeys.contains(entry))
                    targets.append({ entry, root.absoluteFilePath(entry) });
            }

            igiris::scan::ScanCache cache;
            cache.open(romsDir + QStringLiteral("/.igiris-scan-cache.db"), nullptr);
            igiris::scan::RomScanner scanner(catalogue, cache.isOpen() ? &cache : nullptr);
            const auto               report = scanner.scan(targets);

            const QStringList owned = report.ownedGameKeys();
            games.setOwnedGameKeys(QSet<QString>(owned.cbegin(), owned.cend()));

            // La fiche a besoin du CHEMIN de chaque ROM, pas seulement de la liste des
            // jeux possédés : c'est lui qui part dans la commande de lancement.
            QHash<QString, QString> ownedRoms;
            for (const auto &rom : report.identified)
                ownedRoms.insert(rom.gameKey + QLatin1Char('\x1f') + rom.platformKey,
                                 rom.path);
            detail.setOwnedRoms(ownedRoms);

            std::printf("scan local : %d fichiers · %lld jeux possédés\n", report.filesSeen,
                        static_cast<long long>(owned.size()));
        }
    } else {
        std::fprintf(stderr, "⚠ %s — interface chargée sans catalogue\n",
                     qPrintable(catalogueError));
    }
    if (!initialFilter.isEmpty())
        games.setFilter(initialFilter);

    // Préréglages de filtres en ligne de commande : ils servent aux captures, mais aussi
    // à vérifier le comportement sans écran.
    for (int i = 1; i < argc; ++i) {
        // « --platform-key » et pas « --platform » : QGuiApplication RÉSERVE --platform
        // pour choisir son plugin de plateforme (offscreen, xcb…). Le nom court était
        // capté par Qt avant d'atteindre ce code, et l'application refusait de démarrer.
        if (std::strcmp(argv[i], "--platform-key") == 0 && i + 1 < argc)
            games.setPlatformFilter(QString::fromLocal8Bit(argv[i + 1]));
        else if (std::strcmp(argv[i], "--decade") == 0 && i + 1 < argc)
            games.setDecadeFilter(QString::fromLocal8Bit(argv[i + 1]).toInt());
        else if (std::strcmp(argv[i], "--arcade") == 0)
            games.setArcadeOnly(true);
        else if (std::strcmp(argv[i], "--owned") == 0)
            games.setOwnership(igiris::ui::GameListModel::OwnedOnly);
        else if (std::strcmp(argv[i], "--missing") == 0)
            games.setOwnership(igiris::ui::GameListModel::MissingOnly);
    }
    std::printf("filtres : %d / %d jeux affichés\n", games.visibleCount(),
                games.totalCount());

    // Systèmes réellement présents : c'est le fichier de description qui tranche, et donc
    // lui qui décide du statut noir (§1, §7).
    detail.setCatalogue(&catalogue);
    detail.setAdapter(adapter.get());

    // C'est l'ADAPTATEUR qui lit sa description, pas ce point d'entrée. Avant le lot 9,
    // main.cpp appelait lui-même le parser du format EmulationStation — il supposait donc
    // que toutes les distributions partagent ce format, ce que Recalbox dément.
    if (adapter) {
        QString    systemsError;
        const auto systems = systemsFileOverride.isEmpty()
                                 ? adapter->readSystems(&systemsError)
                                 : igiris::systems::parseEsSystemsFile(systemsFileOverride)
                                       .systems;

        if (systems.isEmpty() && !systemsError.isEmpty()) {
            std::fprintf(stderr, "⚠ %s\n", qPrintable(systemsError)); // verbatim (§15)
        } else {
            QHash<QString, igiris::platform::SystemEntry> byName;
            for (const auto &system : systems)
                byName.insert(system.name, system);
            detail.setLocalSystems(byName);
            std::printf("systèmes locaux : %lld (%s)\n",
                        static_cast<long long>(byName.size()),
                        qPrintable(systemsFileOverride.isEmpty()
                                       ? adapter->systemsFilePath()
                                       : systemsFileOverride));
        }
    } else {
        std::fprintf(stderr, "⚠ aucune distribution reconnue : statuts et lancement "
                             "indisponibles\n");
    }

    // Ouvrir directement la fiche d'un jeu, pour la capture comme pour l'inspection.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--open-detail") != 0 || i + 1 >= argc)
            continue;
        const auto found = catalogue.searchByName(QString::fromLocal8Bit(argv[i + 1]), 1);
        if (!found.isEmpty())
            detail.setGame(found.first().gameKey);
    }

    // Livrable du lot 7, vérifiable sans appareil : la fiche d'un jeu, ses statuts, et la
    // commande EXACTE qui serait exécutée — arguments séparés, un par ligne.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--launch-preview") != 0 || i + 1 >= argc)
            continue;

        const auto found = catalogue.searchByName(QString::fromLocal8Bit(argv[i + 1]), 1);
        if (found.isEmpty()) {
            std::fprintf(stderr, "✗ aucun jeu ne correspond\n");
            return 1;
        }

        detail.setGame(found.first().gameKey);
        std::printf("\n%s\n", qPrintable(detail.title()));

        const QString warning = detail.launchWarning();
        if (!warning.isEmpty())
            std::printf("⚠ %s\n", qPrintable(warning));

        static const char *const marks[] = { "⬛", "🟥", "🟩" };
        for (int row = 0; row < detail.rowCount(); ++row) {
            const QModelIndex index = detail.index(row, 0);
            const int status = index.data(igiris::ui::GameDetailModel::StatusRole).toInt();
            std::printf("\n  %s %-14s %-28s emu %3d%s\n", marks[status],
                        qPrintable(index.data(igiris::ui::GameDetailModel::PlatformKeyRole)
                                       .toString()),
                        qPrintable(index.data(igiris::ui::GameDetailModel::DisplayNameRole)
                                       .toString()),
                        index.data(igiris::ui::GameDetailModel::EmuScoreRole).toInt(),
                        index.data(igiris::ui::GameDetailModel::DefaultChoiceRole).toBool()
                            ? "  ← proposé par défaut"
                            : "");
            if (status == igiris::ui::GameDetailModel::Green)
                std::printf("      %s\n", qPrintable(detail.commandPreview(row)));
        }
        return 0;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("games"), &games);
    engine.rootContext()->setContextProperty(QStringLiteral("detail"), &detail);

    // CLAUDE.md §15 : « Erreurs remontées verbatim, jamais avalées ni reformulées. »
    // Un échec de création d'objet QML doit tuer le processus avec un code non nul,
    // sinon une fenêtre absente passerait pour un démarrage réussi.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() {
            std::fprintf(stderr, "✗ échec de création de l'objet QML racine\n");
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/IgirisFrontend/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (!screenshotPath.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window) {
            std::fprintf(stderr, "✗ l'objet racine QML n'est pas une fenêtre\n");
            return 1;
        }
        // Laisser le temps à la première image d'être composée avant de la saisir.
        QTimer::singleShot(700, &app, [window, screenshotPath, &app]() {
            const QImage image = window->grabWindow();
            if (image.isNull() || !image.save(screenshotPath)) {
                std::fprintf(stderr, "✗ capture impossible : %s\n",
                             qPrintable(screenshotPath));
                app.exit(1);
                return;
            }
            std::printf("capture %dx%d → %s\n", image.width(), image.height(),
                        qPrintable(screenshotPath));
            app.quit();
        });
    }

    return app.exec();
}
