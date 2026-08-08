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

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            printVersion();
            return 0;
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
