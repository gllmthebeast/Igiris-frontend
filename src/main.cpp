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
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "catalog/ExportDatabase.h"
#include "scan/RomScanner.h"
#include "scan/ScanCache.h"
#include "platform/AdapterRegistry.h"
#include "systems/EsSystemsParser.h"
#include "ui/GameDetailModel.h"
#include "ui/GameListModel.h"
#include "ui/HostActions.h"
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

    // Lot 8 — les langues. Ce bloc VÉRIFIE le contrat plutôt que de l'afficher : deux
    // erreurs de ce schéma sont silencieuses à l'écran, et ce sont les deux plus graves.
    if (!db.hasLanguages()) {
        std::printf("\n[5] langues : absentes de cet export (schéma %s)\n",
                    qPrintable(meta.schemaVersion));
    } else {
        const auto languages = db.languages();
        std::printf("\n[5] langues : %lld codes · %d liens ROM↔langue\n",
                    static_cast<long long>(languages.size()), meta.gameLanguages);

        // a) bit_index UNIQUE. Deux langues sur le même bit rendraient tous les badges
        //    faux sans qu'aucune requête n'échoue.
        QHash<int, QString> byBit;
        int                 withoutBit = 0;
        for (const auto &language : languages) {
            if (!language.hasBit()) {
                ++withoutBit;
                continue;
            }
            if (byBit.contains(language.bitIndex)) {
                std::fprintf(stderr,
                             "\n✗ bit %d attribué à « %s » ET à « %s » : tous les masques "
                             "sont ininterprétables.\n",
                             language.bitIndex, qPrintable(byBit.value(language.bitIndex)),
                             qPrintable(language.code));
                return 1;
            }
            byBit.insert(language.bitIndex, language.code);
        }
        std::printf("    %lld avec bit · %d sans bit (hors masque, mais visibles en fiche)\n",
                    static_cast<long long>(byBit.size()), withoutBit);

        // b) COHÉRENCE du masque : lang_mask doit être le OU des bits des langues du jeu.
        //    Un décalage ici allumerait le badge d'une langue que le jeu n'a pas.
        const auto masks     = db.langMaskByGame();
        int        compared = 0, divergent = 0;
        for (const auto &game : db.searchByName(QStringLiteral("zelda"), 25)) {
            quint64 rebuilt = 0;
            for (const auto &language : db.languagesForGame(game.gameKey)) {
                for (const auto &known : languages) {
                    if (known.code == language.langCode)
                        rebuilt |= known.bit();
                }
            }
            if (rebuilt == 0)
                continue;
            ++compared;
            if (masks.value(game.gameKey, 0) != rebuilt) {
                ++divergent;
                std::fprintf(stderr, "✗ %s : lang_mask %llu, reconstruit %llu\n",
                             qPrintable(game.title),
                             static_cast<unsigned long long>(masks.value(game.gameKey, 0)),
                             static_cast<unsigned long long>(rebuilt));
            }
        }
        std::printf("    masque reconstruit depuis exp_game_language : %d/%d cohérents\n",
                    compared - divergent, compared);
        if (divergent > 0)
            return 1;
    }

    // [6] Les colonnes des exports 1.5.0 et 1.6.0. Compté sur les lignes réellement lues,
    // et pas sur ce qu'annonce exp_meta : c'est la seule façon de voir qu'une colonne a
    // été lue au mauvais indice. La régression que le backend a corrigée en 1.5.0 était
    // exactement de cette nature — lang_mask parti dans rating, sans une seule erreur.
    {
        const auto games   = db.allGames();
        const auto modes   = db.gameModes();
        int        artwork = 0, summaries = 0, moded = 0, outOfRegistry = 0;

        quint64 registry = 0;
        for (const auto &mode : modes)
            registry |= mode.bit();

        for (const auto &game : games) {
            artwork += game.artworkRef.isEmpty() ? 0 : 1;
            summaries += game.summary.isEmpty() ? 0 : 1;
            if (game.modeMask != 0) {
                ++moded;
                // Un bit hors référentiel ne s'afficherait nulle part et ne filtrerait
                // rien : il passerait donc totalement inaperçu.
                if ((game.modeMask & ~registry) != 0)
                    ++outOfRegistry;
            }
        }

        const auto pct = [&games](int n) {
            return games.isEmpty() ? 0.0 : 100.0 * n / games.size();
        };

        std::printf("\n[6] visuels et fiches\n");
        std::printf("    bandeaux (artwork_ref) : %d/%lld (%.1f %%)\n", artwork,
                    static_cast<long long>(games.size()), pct(artwork));
        std::printf("    synopsis               : %d/%lld (%.1f %%)\n", summaries,
                    static_cast<long long>(games.size()), pct(summaries));
        if (modes.isEmpty()) {
            std::printf("    modes de jeu           : absents de cet export\n");
        } else {
            std::printf("    modes de jeu           : %d/%lld (%.1f %%) · %lld au "
                        "référentiel\n",
                        moded, static_cast<long long>(games.size()), pct(moded),
                        static_cast<long long>(modes.size()));
            if (outOfRegistry > 0) {
                std::fprintf(stderr,
                             "✗ %d jeux portent un bit de mode absent du référentiel\n",
                             outOfRegistry);
                return 1;
            }
        }

        // Années par plateforme : la valeur ajoutée n'est pas la couverture, c'est
        // l'ÉCART avec l'année du jeu. Sans écart, la colonne serait un doublon.
        int withYear = 0, differing = 0, rows = 0;
        for (const auto &game : games) {
            for (const auto &platform : db.platformsForGame(game.gameKey)) {
                ++rows;
                if (platform.releaseYear <= 0)
                    continue;
                ++withYear;
                if (game.year > 0 && platform.releaseYear != game.year)
                    ++differing;
            }
        }
        std::printf("    années par plateforme  : %d/%d · %d diffèrent de l'année du jeu\n",
                    withYear, rows, differing);
    }

    // [7] Le catalogue élargi du 1.7.0. Ce n'est pas de la statistique d'agrément : chacun
    // de ces nombres était structurellement NUL avant cette version, donc chacun exerce un
    // chemin de code qui n'avait jamais servi une seule fois.
    {
        const auto games    = db.allGames();
        const auto langs    = db.languages();
        const auto romMasks = db.langMaskByGame();

        quint64 registry = 0;
        for (const auto &language : langs)
            registry |= language.bit();

        int romOnly = 0, catalogOnly = 0, both = 0, neither = 0, unknownBit = 0;
        for (const auto &game : games) {
            const quint64 fromRoms    = romMasks.value(game.gameKey, 0);
            const quint64 fromCatalog = game.langCatalogMask;
            if (fromCatalog & ~registry)
                ++unknownBit;
            if (fromRoms && fromCatalog)
                ++both;
            else if (fromRoms)
                ++romOnly;
            else if (fromCatalog)
                ++catalogOnly;
            else
                ++neither;
        }

        std::printf("\n[7] catalogue élargi\n");
        if (!db.hasCatalogLanguages()) {
            std::printf("    langues de catalogue   : absentes de cet export\n");
        } else {
            std::printf("    langues : %d par ROM seule · %d par catalogue seul · %d les "
                        "deux · %d aucune\n",
                        romOnly, catalogOnly, both, neither);
            const int couvert = romOnly + catalogOnly + both;
            std::printf("    couverture du filtre « existe en… » : %d/%lld (%.1f %%)\n",
                        couvert, static_cast<long long>(games.size()),
                        games.isEmpty() ? 0.0 : 100.0 * couvert / games.size());
            if (unknownBit > 0) {
                // Un bit hors référentiel ne s'afficherait nulle part ET ne filtrerait
                // rien : il passerait totalement inaperçu, ce qui est le pire des cas.
                std::fprintf(stderr,
                             "✗ %d jeux portent une langue de catalogue absente du "
                             "référentiel\n",
                             unknownBit);
                return 1;
            }
        }

        // Plateformes non émulables : le cas « rien à lancer » n'existait pas avant.
        int sansEmulable = 0, sansPlateforme = 0, scoreSurNonEmulable = 0;
        for (const auto &game : games) {
            const auto platforms = db.platformsForGame(game.gameKey);
            if (platforms.isEmpty()) {
                ++sansPlateforme;
                ++sansEmulable;
                continue;
            }
            bool emulable = false;
            for (const auto &platform : platforms) {
                if (platform.isEmulationTarget())
                    emulable = true;
                else if (platform.hasEmuScore())
                    ++scoreSurNonEmulable;
            }
            if (!emulable)
                ++sansEmulable;
        }
        std::printf("    jeux sans système lançable : %d · dont sans aucune plateforme : %d\n",
                    sansEmulable, sansPlateforme);

        // --- [8] alias de noms (1.8.0) ---------------------------------------------------
        std::printf("\n[8] alias de noms\n");
        if (!db.hasAliases()) {
            std::printf("    absents de cet export — recherche sur le titre seul\n");
        } else {
            const auto aliases = db.aliasesByGame();

            QHash<QString, QString> searchKeyByGame, searchKeyByTitle;
            for (const auto &game : games) {
                searchKeyByGame.insert(game.gameKey, game.searchKey);
                searchKeyByTitle.insert(game.title, game.searchKey);
            }

            long long total = 0;
            int malformes = 0, orphelins = 0, redondants = 0, verifies = 0, divergents = 0;
            for (auto it = aliases.cbegin(); it != aliases.cend(); ++it) {
                const auto own = searchKeyByGame.constFind(it.key());
                if (own == searchKeyByGame.cend())
                    ++orphelins;
                for (const auto &alias : it.value()) {
                    ++total;
                    // FORME de la clé, et non sa normalisation : le §0 interdit à l'appareil
                    // de normaliser quoi que ce soit, donc on ne peut pas la recalculer. Ce
                    // qu'on peut faire, c'est constater qu'elle a bien la forme d'une clé.
                    if (alias.key != alias.key.toLower() || alias.key.trimmed() != alias.key
                        || alias.key.contains(QStringLiteral("  ")))
                        ++malformes;
                    // Un alias qui normalise comme le titre de SON jeu ne peut jamais
                    // changer un résultat, et afficherait « trouvé par : <le titre> ».
                    if (own != searchKeyByGame.cend() && alias.key == *own)
                        ++redondants;
                    // LE contrôle du critère 3, dans la seule forme qui soit à notre
                    // portée : quand un alias porte le titre exact d'un jeu du catalogue,
                    // sa clé doit être celle de ce jeu. Deux normalisations divergentes se
                    // verraient ici, et nulle part ailleurs.
                    const auto same = searchKeyByTitle.constFind(alias.name);
                    if (same != searchKeyByTitle.cend()) {
                        ++verifies;
                        if (alias.key != *same)
                            ++divergents;
                    }
                }
            }

            std::printf("    %lld alias sur %lld jeux (%.1f %% du catalogue)\n", total,
                        static_cast<long long>(aliases.size()),
                        games.isEmpty() ? 0.0 : 100.0 * aliases.size() / games.size());
            std::printf("    clés recoupées avec un titre du catalogue : %d/%d concordantes\n",
                        verifies - divergents, verifies);

            if (malformes > 0 || orphelins > 0 || divergents > 0) {
                // Chacun de ces trois cas casse la recherche SANS lever d'erreur : une clé
                // mal formée ne peut jamais mordre, un orphelin ne désigne aucun jeu, et
                // une clé divergente échouerait précisément sur les jeux qu'elle vise.
                std::fprintf(stderr,
                             "✗ alias : %d clés mal formées · %d jeux orphelins · "
                             "%d clés divergentes\n",
                             malformes, orphelins, divergents);
                return 1;
            }
            if (redondants > 0)
                std::fprintf(stderr,
                             "⚠ %d alias identiques au titre de leur propre jeu : ils ne "
                             "peuvent rien changer, et s'afficheraient comme une redite\n",
                             redondants);
        }
        if (scoreSurNonEmulable > 0) {
            // emu_score est un taux de fidélité d'ÉMULATION : sur une plateforme qu'on
            // n'émule pas, TOUTE valeur est un mensonge, y compris 0.
            std::fprintf(stderr, "✗ %d plateformes non émulables portent un emu_score\n",
                         scoreSurNonEmulable);
            return 1;
        }
    }

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

// Livrable du lot 8, vérifiable sans écran : le référentiel de langues, la couverture du
// catalogue, et surtout les DEUX filtres du §8 côte à côte.
//
// Les afficher ensemble n'est pas une commodité de présentation : « existe » et « jouable »
// sont deux questions différentes qu'il est facile de confondre, et le seul moyen de voir
// qu'on ne les a pas interverties est de lire les deux nombres l'un sous l'autre.
int runLanguagesCommand(const QString &exportPath, const QString &romsDir)
{
    igiris::catalog::ExportDatabase db;
    QString                         error;
    if (!db.open(exportPath, &error)) {
        std::fprintf(stderr, "✗ %s\n", qPrintable(error)); // verbatim (§15)
        return 1;
    }

    if (!db.hasLanguages()) {
        std::fprintf(stderr,
                     "✗ cet export (schéma %s) ne porte pas les tables de langues.\n"
                     "  Le lot 8 exige un export 1.4.0 ou supérieur.\n",
                     qPrintable(db.meta().schemaVersion));
        return 1;
    }

    const auto languages = db.languages();
    std::printf("%s · schéma %s\n%lld langues · %d liens ROM↔langue\n\n", qPrintable(exportPath),
                qPrintable(db.meta().schemaVersion), static_cast<long long>(languages.size()),
                db.meta().gameLanguages);

    igiris::ui::GameListModel games;
    games.setCatalogue(db.allGames(), db.platformKeysByGame(), db.arcadePlatformKeys());
    games.setLanguages(languages, db.langMaskByGame());

    const int badged = [&db] {
        int count = 0;
        for (const auto &mask : db.langMaskByGame())
            count += mask != 0 ? 1 : 0;
        return count;
    }();
    std::printf("couverture : %d des %d jeux portent au moins une langue (%.0f %%)\n\n", badged,
                games.totalCount(), 100.0 * badged / qMax(1, games.totalCount()));

    // Le scan est OPTIONNEL : sans lui la colonne « jouable » n'a rien à dire, et le dire
    // vaut mieux que d'afficher des zéros qui passeraient pour un résultat.
    bool scanned = false;
    if (!romsDir.isEmpty()) {
        QList<igiris::scan::ScanTarget> targets;
        const QStringList               knownKeys = db.allPlatformKeys();
        const QDir                      root(romsDir);
        for (const QString &entry :
             root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (knownKeys.contains(entry))
                targets.append({ entry, root.absoluteFilePath(entry) });
        }

        igiris::scan::RomScanner scanner(db, nullptr);
        const auto               report = scanner.scan(targets);

        QSet<QString> ownedRomKeys;
        for (const auto &rom : report.identified) {
            if (!rom.crc32.isEmpty())
                ownedRomKeys.insert(igiris::catalog::romKey(rom.crc32, rom.platformKey));
        }
        games.setOwnedLanguageMasks(db.ownedLangMaskByGame(ownedRomKeys));
        scanned = true;

        std::printf("scan de %s : %d fichiers · %lld ROMs identifiées avec CRC\n\n",
                    qPrintable(romsDir), report.filesSeen,
                    static_cast<long long>(ownedRomKeys.size()));
    }

    std::printf("%-6s %-14s %4s %12s %12s\n", "code", "libellé", "bit", "existe", "jouable");
    for (const auto &language : languages) {
        games.setLanguageFilter({ language.code });

        // Une langue sans bit ne peut pas être filtrée par masque. Le modèle le DIT au lieu
        // de renvoyer discrètement le catalogue entier, qui se lirait comme un résultat.
        if (!games.unfilterableLanguages().isEmpty()) {
            std::printf("%-6s %s %4s %12s %12s\n", qPrintable(language.code),
                        qPrintable(language.label.leftJustified(14)), "—", "hors masque",
                        "hors masque");
            continue;
        }

        games.setLanguageOwnedOnly(false);
        const int exists = games.visibleCount();

        int playable = -1;
        if (scanned) {
            games.setLanguageOwnedOnly(true);
            playable = games.visibleCount();
        }

        std::printf("%-6s %s %4d %12d %12s\n", qPrintable(language.code),
                    qPrintable(language.label.leftJustified(14)), language.bitIndex, exists,
                    playable >= 0 ? qPrintable(QString::number(playable)) : "(pas de scan)");
    }

    // Contrôle de cohérence entre les deux filtres. « jouable » est par construction un
    // sous-ensemble d'« existe » : un jeu jouable en français qui n'existerait pas en
    // français signalerait deux référentiels de bits différents — l'erreur silencieuse
    // que le §8 redoute.
    if (scanned) {
        for (const auto &language : languages) {
            if (!language.hasBit())
                continue;
            games.setLanguageFilter({ language.code });
            games.setLanguageOwnedOnly(false);
            const int exists = games.visibleCount();
            games.setLanguageOwnedOnly(true);
            if (games.visibleCount() > exists) {
                std::fprintf(stderr,
                             "\n✗ « %s » : %d jouables pour %d existants. Les deux masques "
                             "ne partagent pas le même référentiel de bits.\n",
                             qPrintable(language.code), games.visibleCount(), exists);
                return 1;
            }
        }
        std::printf("\n✓ « jouable » est un sous-ensemble d'« existe » pour chaque langue\n");
    }

    // §17 : « la liste nue est le point de référence de performance ; on mesure ensuite le
    // coût réel des badges par rapport à cette base. » Voici cette mesure.
    //
    // Elle balaie TOUT le catalogue, alors qu'un écran n'en montre qu'une quinzaine de
    // lignes à la fois : le chiffre par ligne est donc le seul qui compte pour le
    // défilement, et il est majoré par celui-ci.
    games.setLanguageFilter({});
    games.setLanguageOwnedOnly(false);

    QElapsedTimer timer;
    timer.start();
    int badges = 0;
    for (int row = 0; row < games.rowCount(); ++row)
        badges += games.index(row, 0).data(igiris::ui::GameListModel::LanguagesRole).toList().size();
    const qint64 elapsed = timer.elapsed();

    std::printf("\ncoût des badges : %d badges construits sur %d lignes en %lld ms "
                "(%.1f µs/ligne)\n",
                badges, games.rowCount(), static_cast<long long>(elapsed),
                games.rowCount() > 0 ? 1000.0 * elapsed / games.rowCount() : 0.0);

    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // Sortie LIGNE PAR LIGNE, y compris quand elle est redirigée.
    //
    // Sur un appareil, l'autostart redirige tout vers un fichier de log. La sortie standard
    // devient alors bufferisée par BLOC : rien n'atteint le fichier avant 4 Ko accumulés ou
    // la fin du processus. Or ce processus ne se termine jamais — il tient l'interface.
    // Le log restait donc vide, ce qui se lit « rien ne s'est lancé » alors que tout allait
    // bien, et masquait au passage les diagnostics de chargement du catalogue.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

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
        if (std::strcmp(argv[i], "--languages") == 0) {
            // « --languages [export.db] [dossier-roms] » — le dossier active la colonne
            // « jouable », qui n'existe pas sans scan.
            const QString exp = (i + 1 < argc) ? QString::fromLocal8Bit(argv[i + 1])
                                               : QStringLiteral("data/games.db");
            const QString roms = (i + 2 < argc) ? QString::fromLocal8Bit(argv[i + 2])
                                                : QString();
            return runLanguagesCommand(exp, roms);
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
    // Les actions qui s'adressent à l'HÔTE et non au catalogue : aujourd'hui, rendre
    // l'écran à son interface de réglages (§1, responsabilité 6).
    igiris::ui::HostActions         host;
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
    // Chemin de l'export : explicite, sinon recherché dans des emplacements connus.
    // Un binaire installé ne tourne pas depuis le dépôt : « data/games.db » relatif ne
    // vaut que pendant le développement.
    QString exportPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--export-db") == 0 && i + 1 < argc)
            exportPath = QString::fromLocal8Bit(argv[i + 1]);
    }
    // L'ADAPTATEUR D'ABORD. Sur un appareil, l'export est là où la distribution autorise
    // l'écriture, et ce chemin lui est propre. Chercher d'abord les emplacements Unix
    // génériques revenait à afficher « 0 jeux » alors que l'export était bel et bien là,
    // à l'emplacement que l'installateur de la distribution avait choisi.
    QStringList candidates;
    if (adapter)
        candidates = adapter->exportSearchPaths();

    // Puis les emplacements génériques, qui restent valables : un binaire lancé depuis le
    // dépôt doit continuer à trouver son data/games.db, sur une machine de développement où
    // aucune distribution n'est détectée.
    candidates << QStringLiteral("data/games.db")
               << QDir::homePath() + QStringLiteral("/.local/share/igiris/games.db")
               << QStringLiteral("/var/lib/igiris/games.db");

    if (exportPath.isEmpty()) {
        for (const QString &candidate : std::as_const(candidates)) {
            if (QFileInfo::exists(candidate)) {
                exportPath = candidate;
                break;
            }
        }
    }
    if (exportPath.isEmpty()) {
        // Aucun candidat n'existe. Citer le PREMIER — celui de la distribution quand elle
        // est reconnue — plutôt qu'un chemin de développement : c'est là que l'utilisateur
        // doit déposer le fichier, et le message doit le lui dire.
        exportPath = candidates.first();
    }

    if (catalogue.open(exportPath, &catalogueError)) {
        const qint64 opened = timer.elapsed();
        // Les index des filtres STATIQUES sont construits ici, une fois : le §6 exige
        // qu'une combinaison de filtres reste interactive à la manette.
        games.setCatalogue(catalogue.allGames(), catalogue.platformKeysByGame(),
                           catalogue.arcadePlatformKeys());

        // Lot 8 — les langues, si l'export les porte. Un export 1.3.0 reste parfaitement
        // utilisable : l'interface signale l'absence au lieu d'afficher des lignes muettes.
        if (catalogue.hasLanguages()) {
            games.setLanguages(catalogue.languages(), catalogue.langMaskByGame());
            detail.setLanguages(catalogue.languages());
            std::printf("langues : %lld codes · %d liens ROM↔langue\n",
                        static_cast<long long>(catalogue.languages().size()),
                        catalogue.meta().gameLanguages);
        } else {
            std::printf("langues : absentes de cet export (%s) — badges et filtres de "
                        "langue désactivés\n",
                        qPrintable(catalogue.meta().schemaVersion));
        }

        // Alias de noms — export 1.8.0. Sans eux, chercher « lttp » ou « ff7 » ne donne
        // rien : le catalogue ne connaît qu'un seul nom par jeu.
        if (catalogue.hasAliases()) {
            const auto aliases = catalogue.aliasesByGame();
            games.setAliases(aliases);
            long long total = 0;
            for (const auto &list : aliases)
                total += list.size();
            std::printf("alias : %lld sur %lld jeux\n", total,
                        static_cast<long long>(aliases.size()));
        } else {
            std::printf("alias : absents de cet export (%s) — recherche sur le titre seul\n",
                        qPrintable(catalogue.meta().schemaVersion));
        }

        // Modes de jeu — export 1.6.0. Même dégradation que les langues : un export
        // antérieur reste utilisable, simplement sans le filtre.
        if (catalogue.hasModes()) {
            const auto modes = catalogue.gameModes();
            games.setGameModes(modes);
            detail.setGameModes(modes);
            std::printf("modes de jeu : %lld au référentiel\n",
                        static_cast<long long>(modes.size()));
        } else {
            std::printf("modes de jeu : absents de cet export (%s) — filtre désactivé\n",
                        qPrintable(catalogue.meta().schemaVersion));
        }
        std::printf("catalogue : %d jeux · %lld plateformes · ouverture %lld ms · "
                    "chargement %lld ms\n",
                    games.totalCount(),
                    static_cast<long long>(games.availablePlatforms().size()),
                    static_cast<long long>(opened),
                    static_cast<long long>(timer.elapsed() - opened));

        // Filtre DYNAMIQUE, et surtout STATUT VERT : les deux dépendent du scan local.
        //
        // ⚠️ Le scan ne partait qu'avec « --roms », donc JAMAIS au démarrage de l'appareil,
        // où l'autostart lance le binaire sans argument. Aucune ROM ne pouvait passer au
        // vert, et le lancement était inaccessible — alors que l'adaptateur SAIT où sont
        // les ROMs. Sa méthode romDirectories() n'était appelée nulle part : une capacité
        // déclarée que personne ne consommait.
        QStringList romsDirs;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--roms") == 0 && i + 1 < argc)
                romsDirs << QString::fromLocal8Bit(argv[i + 1]);
        }

        // À défaut d'argument, c'est l'adaptateur qui tranche — comme pour l'export (§1).
        // La capacité est vérifiée : une distribution qui ne sait pas localiser ses ROMs
        // le DIT, et on ne lui invente pas un chemin.
        if (romsDirs.isEmpty() && adapter
            && adapter->supports(igiris::platform::Capability::RomDirectories)) {
            romsDirs = adapter->romDirectories();
            if (!romsDirs.isEmpty())
                std::printf("dossiers de ROMs (adaptateur) : %s\n",
                            qPrintable(romsDirs.join(u", ")));
        }

        for (const QString &romsDir : std::as_const(romsDirs)) {
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
            QSet<QString>           ownedRomKeys;
            for (const auto &rom : report.identified) {
                ownedRoms.insert(rom.gameKey + QLatin1Char('\x1f') + rom.platformKey,
                                 rom.path);
                // L'arcade n'a pas de CRC (§4) : elle ne peut pas porter de langue, et
                // c'est le catalogue qui le dit — pas une exception codée ici.
                if (!rom.crc32.isEmpty())
                    ownedRomKeys.insert(
                        igiris::catalog::romKey(rom.crc32, rom.platformKey));
            }
            detail.setOwnedRoms(ownedRoms);
            detail.setOwnedRomKeys(ownedRomKeys);

            // Le filtre DYNAMIQUE « jouable en <langue> » : export × ROMs possédées.
            if (catalogue.hasLanguages()) {
                const auto ownedLangs = catalogue.ownedLangMaskByGame(ownedRomKeys);
                games.setOwnedLanguageMasks(ownedLangs);
                std::printf("langues jouables : %lld jeux\n",
                            static_cast<long long>(ownedLangs.size()));
            }

            std::printf("scan local : %d fichiers · %lld jeux possédés\n", report.filesSeen,
                        static_cast<long long>(owned.size()));
        }
    } else {
        std::fprintf(stderr, "⚠ %s — interface chargée sans catalogue\n",
                     qPrintable(catalogueError));
        // Lister CE QUI A ÉTÉ CHERCHÉ. « 0 jeux » sans cette liste n'apprend rien : le
        // fichier peut être présent, simplement ailleurs que là où on l'attendait.
        std::fprintf(stderr, "  emplacements essayés :\n");
        for (const QString &candidate : std::as_const(candidates))
            std::fprintf(stderr, "    %s\n", qPrintable(candidate));
        std::fprintf(stderr, "  télécharger l'export : fetch-export.sh <dossier>\n"
                             "  ou forcer le chemin  : igiris-frontend --export-db <fichier>\n");
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
        // Plusieurs codes séparés par des virgules : « fr,de » exige les DEUX (§8).
        else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc)
            games.setLanguageFilter(QString::fromLocal8Bit(argv[i + 1])
                                        .split(QLatin1Char(','), Qt::SkipEmptyParts));
        else if (std::strcmp(argv[i], "--lang-owned") == 0)
            games.setLanguageOwnedOnly(true);
        // Les jaquettes sont la seule donnée distante (§11) : sur un appareil hors ligne,
        // les couper évite d'empiler des requêtes qui n'aboutiront jamais.
        else if (std::strcmp(argv[i], "--no-covers") == 0)
            games.setCoversEnabled(false);
    }
    std::printf("filtres : %d / %d jeux affichés\n", games.visibleCount(),
                games.totalCount());

    // Systèmes réellement présents : c'est le fichier de description qui tranche, et donc
    // lui qui décide du statut noir (§1, §7).
    detail.setCatalogue(&catalogue);
    detail.setAdapter(adapter.get());
    host.setAdapter(adapter.get());

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

            // Les langues de CETTE plateforme (§7). Majuscule = illuminée (une ROM
            // possédée la fournit), minuscule entre parenthèses = grisée.
            const QVariantList languages =
                index.data(igiris::ui::GameDetailModel::LanguagesRole).toList();
            if (!languages.isEmpty()) {
                QStringList badges;
                for (const QVariant &entry : languages) {
                    const QVariantMap badge = entry.toMap();
                    const QString     code  = badge.value(QStringLiteral("code")).toString();
                    badges.append(badge.value(QStringLiteral("owned")).toBool()
                                      ? code.toUpper()
                                      : QStringLiteral("(%1)").arg(code));
                }
                std::printf("      langues %s\n", qPrintable(badges.join(u' ')));
            }

            if (status == igiris::ui::GameDetailModel::Green)
                std::printf("      %s\n", qPrintable(detail.commandPreview(row)));
        }
        return 0;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("games"), &games);
    engine.rootContext()->setContextProperty(QStringLiteral("host"), &host);
    engine.rootContext()->setContextProperty(QStringLiteral("detail"), &detail);
    // La version, exposée à l'interface. Sur un appareil mis à jour par scp, savoir CE QUI
    // tourne sans passer par un terminal est la première question qu'on se pose.
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QStringLiteral(IGIRIS_FRONTEND_VERSION));

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
