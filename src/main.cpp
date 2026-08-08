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
#include <QUrl>

#include "catalog/ExportDatabase.h"
#include "platform/AdapterRegistry.h"
#include "systems/EsSystemsParser.h"
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

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            printVersion();
            return 0;
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

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("igiris-frontend"));
    QGuiApplication::setApplicationVersion(QStringLiteral(IGIRIS_FRONTEND_VERSION));

    QQmlApplicationEngine engine;

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

    return app.exec();
}
