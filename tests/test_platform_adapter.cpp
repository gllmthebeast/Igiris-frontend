// Tests du lot 1 — adaptateur de plateforme.
//
// Tout se joue sur une arborescence FACTICE montée dans un répertoire temporaire : c'est
// ce que permet le rootPrefix de l'adaptateur, et c'est ce qui rend ces tests exécutables
// sur une machine de build qui n'est pas l'appareil cible (§15).
//
// Ce fichier vit hors de src/ : il a légitimement besoin de nommer la distribution qu'il
// teste, ce que le test no-distro-literals interdit dans le code.

#include "platform/BatoceraAdapter.h"
#include "platform/CommandLine.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace igiris::platform;

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();
}

// La ligne de commande réelle de Batocera : c'est elle qu'on doit savoir traiter.
QString realisticCommand()
{
    return QStringLiteral(
        "/usr/bin/emulatorlauncher %CONTROLLERSCONFIG% -system %SYSTEM% -rom %ROM% "
        "-emulator libretro -core snes9x");
}

} // namespace

class TestPlatformAdapter : public QObject
{
    Q_OBJECT

private slots:
    // ---------------------------------------------------------------- découpage
    void tokenize_separatesOnWhitespace();
    void tokenize_respectsQuotes();
    void tokenize_keepsEmptyQuotedArgument();

    // ------------------------------------------------------------ substitution
    void substitute_replacesKnownPlaceholders();
    void substitute_romRawBeforeRom();
    void substitute_bothRomRawSpellings();
    void substitute_flagsUnknownPlaceholderAndKeepsItVerbatim();
    void substitute_awkwardFilenameStaysOneArgument();

    // ------------------------------------------------- (1) fichier des systèmes
    void systemsFile_prefersUserOverride();
    void systemsFile_fallsBackToSystemCopy();
    void systemsFile_emptyWhenAbsent();

    // ------------------------------------------------------------- détection
    void detect_trueOnMarker();
    void detect_falseOnEmptyTree();

    // ------------------------------------------- (2) résolution de platform_key
    void resolve_exactMatch();
    void resolve_viaAlias();
    void resolve_emptyWhenSystemAbsent();

    // ------------------------------------------------- (3) dossiers de ROMs
    void romDirectories_foundAndEmptyWhenAbsent();

    // ------------------------------------------------------- (4) lancement
    void launch_reallyRunsTheCommand();
    void launch_refusesUnresolvedPlaceholder();
    void launch_refusesMissingRom();
    void launch_errorMessageIsNeverEmpty();

    // ------------------------------------------------------------ capacités
    void capabilities_areDeclared();
};

// ---------------------------------------------------------------------- découpage

void TestPlatformAdapter::tokenize_separatesOnWhitespace()
{
    QCOMPARE(tokenizeCommand(QStringLiteral("/usr/bin/foo -a  -b")),
             (QStringList{ "/usr/bin/foo", "-a", "-b" }));
}

void TestPlatformAdapter::tokenize_respectsQuotes()
{
    // Un chemin contenant une espace doit rester UN jeton.
    QCOMPARE(tokenizeCommand(QStringLiteral("/bin/x \"/mnt/mes jeux/rom.zip\" -q")),
             (QStringList{ "/bin/x", "/mnt/mes jeux/rom.zip", "-q" }));
}

void TestPlatformAdapter::tokenize_keepsEmptyQuotedArgument()
{
    QCOMPARE(tokenizeCommand(QStringLiteral("/bin/x \"\" -v")),
             (QStringList{ "/bin/x", "", "-v" }));
}

// ------------------------------------------------------------------ substitution

void TestPlatformAdapter::substitute_replacesKnownPlaceholders()
{
    const auto tokens = tokenizeCommand(
        QStringLiteral("/bin/run -s %SYSTEM% -r %ROM% -b %BASENAME% -d %GAMEDIR%"));
    const auto out = substitutePlaceholders(
        tokens, LaunchContext{ QStringLiteral("/userdata/roms/snes/Super Mario World.sfc"),
                               QStringLiteral("snes") });

    QVERIFY(out.unresolved.isEmpty());
    QCOMPARE(out.tokens.at(2), QStringLiteral("snes"));
    QCOMPARE(out.tokens.at(4), QStringLiteral("/userdata/roms/snes/Super Mario World.sfc"));
    QCOMPARE(out.tokens.at(6), QStringLiteral("Super Mario World"));
    QCOMPARE(out.tokens.at(8), QStringLiteral("/userdata/roms/snes"));
}

void TestPlatformAdapter::substitute_romRawBeforeRom()
{
    // Si %ROM% était traité en premier, il resterait un « _RAW% » orphelin.
    const auto out = substitutePlaceholders(
        QStringList{ QStringLiteral("%ROM_RAW%") },
        LaunchContext{ QStringLiteral("/roms/a.zip"), QStringLiteral("snes") });

    QCOMPARE(out.tokens.first(), QStringLiteral("/roms/a.zip"));
    QVERIFY(out.unresolved.isEmpty());
}

void TestPlatformAdapter::substitute_bothRomRawSpellings()
{
    // Les deux orthographes circulent dans la famille EmulationStation. Un fichier de
    // référence de 195 systèmes emploie %ROMRAW% ; d'autres emploient %ROM_RAW%.
    // N'en gérer qu'une casserait les lancements de l'autre sans message.
    for (const QString &spelling : { QStringLiteral("%ROMRAW%"), QStringLiteral("%ROM_RAW%") }) {
        const auto out = substitutePlaceholders(
            QStringList{ spelling },
            LaunchContext{ QStringLiteral("/roms/a.zip"), QStringLiteral("snes") });
        QVERIFY2(out.unresolved.isEmpty(), qPrintable(spelling));
        QCOMPARE(out.tokens.first(), QStringLiteral("/roms/a.zip"));
    }
}

void TestPlatformAdapter::substitute_flagsUnknownPlaceholderAndKeepsItVerbatim()
{
    const auto tokens = tokenizeCommand(realisticCommand());
    const auto out    = substitutePlaceholders(
        tokens, LaunchContext{ QStringLiteral("/roms/snes/a.sfc"), QStringLiteral("snes") });

    // %CONTROLLERSCONFIG% n'est pas géré au lot 1 : il doit être SIGNALÉ, pas avalé.
    QCOMPARE(out.unresolved, QStringList{ QStringLiteral("%CONTROLLERSCONFIG%") });
    QVERIFY(out.tokens.contains(QStringLiteral("%CONTROLLERSCONFIG%")));
}

void TestPlatformAdapter::substitute_awkwardFilenameStaysOneArgument()
{
    // Le vrai piège : ces noms existent en masse dans les collections.
    const QString rom = QStringLiteral("/roms/md/Sonic & Knuckles (USA) [!].md");
    const auto    out = substitutePlaceholders(
        QStringList{ QStringLiteral("-rom"), QStringLiteral("%ROM%") },
        LaunchContext{ rom, QStringLiteral("megadrive") });

    QCOMPARE(out.tokens.size(), 2);
    QCOMPARE(out.tokens.at(1), rom); // un seul argument, aucun échappement nécessaire
}

// --------------------------------------------------- (1) fichier des systèmes

void TestPlatformAdapter::systemsFile_prefersUserOverride()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    touch(dir.filePath("usr/share/emulationstation/es_systems.cfg"));
    touch(dir.filePath("userdata/system/configs/emulationstation/es_systems.cfg"));

    const BatoceraAdapter adapter(dir.path());
    QCOMPARE(adapter.systemsFilePath(),
             QDir::cleanPath(dir.filePath("userdata/system/configs/emulationstation/es_systems.cfg")));
}

void TestPlatformAdapter::systemsFile_fallsBackToSystemCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    touch(dir.filePath("usr/share/emulationstation/es_systems.cfg"));

    const BatoceraAdapter adapter(dir.path());
    QCOMPARE(adapter.systemsFilePath(),
             QDir::cleanPath(dir.filePath("usr/share/emulationstation/es_systems.cfg")));
}

void TestPlatformAdapter::systemsFile_emptyWhenAbsent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const BatoceraAdapter adapter(dir.path());
    QVERIFY(adapter.systemsFilePath().isEmpty());
}

// -------------------------------------------------------------------- détection

void TestPlatformAdapter::detect_trueOnMarker()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath("userdata/system")));
    QVERIFY(BatoceraAdapter::detect(dir.path()));
}

void TestPlatformAdapter::detect_falseOnEmptyTree()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!BatoceraAdapter::detect(dir.path()));
}

// ------------------------------------------------ (2) résolution de platform_key

void TestPlatformAdapter::resolve_exactMatch()
{
    const BatoceraAdapter adapter(QStringLiteral("/"));
    QCOMPARE(adapter.resolvePlatformKey(QStringLiteral("snes"),
                                        { "snes", "megadrive", "psx" }),
             QStringLiteral("snes"));
}

void TestPlatformAdapter::resolve_viaAlias()
{
    const BatoceraAdapter adapter(QStringLiteral("/"));
    // Installation qui nomme le système « genesis » : le catalogue dit « megadrive ».
    QCOMPARE(adapter.resolvePlatformKey(QStringLiteral("megadrive"), { "genesis", "snes" }),
             QStringLiteral("genesis"));
}

void TestPlatformAdapter::resolve_emptyWhenSystemAbsent()
{
    const BatoceraAdapter adapter(QStringLiteral("/"));
    // Absent = statut NOIR (§7), pas une erreur.
    QVERIFY(adapter.resolvePlatformKey(QStringLiteral("dreamcast"), { "snes" }).isEmpty());
    QVERIFY(adapter.resolvePlatformKey(QString(), { "snes" }).isEmpty());
}

// ------------------------------------------------------- (3) dossiers de ROMs

void TestPlatformAdapter::romDirectories_foundAndEmptyWhenAbsent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(BatoceraAdapter(dir.path()).romDirectories().isEmpty());

    QVERIFY(QDir().mkpath(dir.filePath("userdata/roms")));
    QCOMPARE(BatoceraAdapter(dir.path()).romDirectories(),
             QStringList{ QDir::cleanPath(dir.filePath("userdata/roms")) });
}

// ---------------------------------------------------------------- (4) lancement

void TestPlatformAdapter::launch_reallyRunsTheCommand()
{
    // Le chemin nominal, exécuté pour de vrai — et avec le pire nom de fichier plausible.
    // C'est ce test qui prouve que l'absence de shell tient jusqu'au bout : une
    // esperluette, des parenthèses et des crochets non échappés traverseraient un shell
    // en se transformant en opérateurs.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString rom = dir.filePath(QStringLiteral("Sonic & Knuckles (USA) [!].md"));
    touch(rom);

    SystemEntry system;
    system.name    = QStringLiteral("megadrive");
    system.launchOptions = { { QStringLiteral("test"), QStringLiteral("/usr/bin/touch \"%ROM%.lance\"") } };

    QString error;
    QVERIFY2(BatoceraAdapter(dir.path()).launch(system, rom, &error),
             qPrintable(error));
    QVERIFY(error.isEmpty());

    // startDetached() rend la main aussitôt : on attend l'effet de bord.
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(rom + QStringLiteral(".lance")), 5000);
}

void TestPlatformAdapter::launch_refusesUnresolvedPlaceholder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString rom = dir.filePath("a.sfc");
    touch(rom);

    SystemEntry system;
    system.name    = QStringLiteral("snes");
    system.launchOptions = { { QStringLiteral("test"), realisticCommand() } }; // contient %CONTROLLERSCONFIG%

    QString error;
    QVERIFY(!BatoceraAdapter(dir.path()).launch(system, rom, &error));
    QVERIFY(error.contains(QStringLiteral("%CONTROLLERSCONFIG%")));
}

void TestPlatformAdapter::launch_refusesMissingRom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SystemEntry system;
    system.name    = QStringLiteral("snes");
    system.launchOptions = { { QStringLiteral("test"), QStringLiteral("/bin/true %ROM%") } };

    QString error;
    QVERIFY(!BatoceraAdapter(dir.path()).launch(system, dir.filePath("absente.sfc"), &error));
    QVERIFY(error.contains(QStringLiteral("introuvable")));
}

void TestPlatformAdapter::launch_errorMessageIsNeverEmpty()
{
    // §15 : erreurs verbatim. Un échec muet est un bug.
    SystemEntry invalid;
    QString     error;
    QVERIFY(!BatoceraAdapter(QStringLiteral("/")).launch(invalid, QStringLiteral("/x"), &error));
    QVERIFY(!error.isEmpty());
}

// ---------------------------------------------------------------------- capacités

void TestPlatformAdapter::capabilities_areDeclared()
{
    const BatoceraAdapter adapter(QStringLiteral("/"));
    QVERIFY(adapter.supports(Capability::SystemsFile));
    QVERIFY(adapter.supports(Capability::RomDirectories));
    QVERIFY(adapter.supports(Capability::Launch));
    QCOMPARE(adapter.id(), QStringLiteral("batocera"));
}

QTEST_GUILESS_MAIN(TestPlatformAdapter)
#include "test_platform_adapter.moc"
