// Tests du lot 9 — second adaptateur, et surtout TEST DE CONCEPTION (§13).
//
// Le systemlist.xml utilisé ici est SYNTHÉTIQUE, mais son schéma n'est pas inventé : il
// vient de SystemDeserializer.cpp du dépôt Recalbox, qui lit name/fullname en attributs
// de <system> et path/extensions/theme/command en attributs de <descriptor>.
//
// ⚠️ Contrairement à Batocera, aucun systemlist.xml RÉEL n'a pu être récupéré. Le parser
// est donc conforme au schéma publié, pas encore confronté à un fichier de production.

#include "platform/AdapterRegistry.h"
#include "platform/RecalboxAdapter.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace igiris::platform;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();
}

// Schéma Recalbox : tout en attributs. C'est la divergence qui a invalidé la première
// version de l'interface.
QByteArray systemList()
{
    return R"(<?xml version="1.0"?>
<systemList>
  <system uuid="1" name="snes" fullname="Super Nintendo">
    <descriptor path="/recalbox/share/roms/snes" extensions=".smc .SMC .sfc .zip"
                theme="snes"
                command="python /usr/bin/emulatorlauncher -system %SYSTEM% -rom %ROM% -emulator libretro -core snes9x"/>
    <properties type="console"/>
  </system>
  <system uuid="2" name="megadrive" fullname="Sega Mega Drive">
    <descriptor path="/recalbox/share/roms/megadrive" extensions=".md .bin"
                theme="megadrive"
                command="python /usr/bin/emulatorlauncher -system %SYSTEM% -rom %ROM% -emulator libretro -core genesisplusgx"/>
  </system>
  <system uuid="3" name="sansCommande" fullname="Système sans commande">
    <descriptor path="/recalbox/share/roms/x" extensions=".x"/>
  </system>
</systemList>)";
}

} // namespace

class TestRecalboxAdapter : public QObject
{
    Q_OBJECT

private slots:
    void detect_recognisesRecalboxTree();
    void detect_ignoresUnrelatedTree();

    void readSystems_parsesTheAttributeSchema();
    void readSystems_prefersUserCopyOverTemplate();
    void readSystems_skipsSystemsWithoutCommand();
    void readSystems_reportsMissingFile();

    void romDirectories_pointAtRecalboxShare();
    void resolvePlatformKey_handlesAliases();

    void buildLaunchCommand_keepsArgumentsSeparate();
    void registry_returnsRecalboxForItsTree();
};

void TestRecalboxAdapter::detect_recognisesRecalboxTree()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath("recalbox/share_init")));
    QVERIFY(RecalboxAdapter::detect(dir.path()));
}

void TestRecalboxAdapter::detect_ignoresUnrelatedTree()
{
    QTemporaryDir dir;
    QVERIFY(!RecalboxAdapter::detect(dir.path()));
}

void TestRecalboxAdapter::readSystems_parsesTheAttributeSchema()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("recalbox/share_init/system/.emulationstation/systemlist.xml"),
              systemList());

    QString    error;
    const auto systems = RecalboxAdapter(dir.path()).readSystems(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(systems.size(), 2); // le troisième n'a pas de commande

    QCOMPARE(systems.at(0).name, QStringLiteral("snes"));
    QCOMPARE(systems.at(0).fullName, QStringLiteral("Super Nintendo"));
    QCOMPARE(systems.at(0).romPath, QStringLiteral("/recalbox/share/roms/snes"));
    // Extensions dédoublonnées et en minuscules, comme pour l'autre format.
    QCOMPARE(systems.at(0).extensions, (QStringList{ ".smc", ".sfc", ".zip" }));
    QVERIFY(systems.at(0).defaultCommand().contains(QStringLiteral("-core snes9x")));
}

void TestRecalboxAdapter::readSystems_prefersUserCopyOverTemplate()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("recalbox/share_init/system/.emulationstation/systemlist.xml"),
              systemList());
    writeFile(dir.filePath("recalbox/share/system/.emulationstation/systemlist.xml"),
              R"(<systemList><system name="seul" fullname="Seul">
             <descriptor path="/x" command="/bin/run %ROM%"/></system></systemList>)");

    const auto systems = RecalboxAdapter(dir.path()).readSystems(nullptr);
    QCOMPARE(systems.size(), 1);
    QCOMPARE(systems.first().name, QStringLiteral("seul"));
}

void TestRecalboxAdapter::readSystems_skipsSystemsWithoutCommand()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("recalbox/share_init/system/.emulationstation/systemlist.xml"),
              systemList());

    const auto systems = RecalboxAdapter(dir.path()).readSystems(nullptr);
    for (const auto &system : systems)
        QVERIFY(!system.defaultCommand().isEmpty());
}

void TestRecalboxAdapter::readSystems_reportsMissingFile()
{
    QTemporaryDir dir;
    QString       error;
    QVERIFY(RecalboxAdapter(dir.path()).readSystems(&error).isEmpty());
    QVERIFY(!error.isEmpty()); // §15 : jamais un échec muet
}

void TestRecalboxAdapter::romDirectories_pointAtRecalboxShare()
{
    QTemporaryDir dir;
    QVERIFY(RecalboxAdapter(dir.path()).romDirectories().isEmpty());

    QVERIFY(QDir().mkpath(dir.filePath("recalbox/share/roms")));
    QCOMPARE(RecalboxAdapter(dir.path()).romDirectories(),
             QStringList{ QDir::cleanPath(dir.filePath("recalbox/share/roms")) });
}

void TestRecalboxAdapter::resolvePlatformKey_handlesAliases()
{
    const RecalboxAdapter adapter(QStringLiteral("/"));
    QCOMPARE(adapter.resolvePlatformKey(QStringLiteral("snes"), { "snes" }),
             QStringLiteral("snes"));
    QCOMPARE(adapter.resolvePlatformKey(QStringLiteral("megadrive"), { "genesis" }),
             QStringLiteral("genesis"));
    QVERIFY(adapter.resolvePlatformKey(QStringLiteral("dreamcast"), { "snes" }).isEmpty());
}

void TestRecalboxAdapter::buildLaunchCommand_keepsArgumentsSeparate()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("recalbox/share_init/system/.emulationstation/systemlist.xml"),
              systemList());

    const RecalboxAdapter adapter(dir.path());
    const auto            systems = adapter.readSystems(nullptr);
    QVERIFY(!systems.isEmpty());

    const QString rom = QStringLiteral("/roms/snes/Sonic & Knuckles (USA) [!].smc");
    const auto    command = adapter.buildLaunchCommand(systems.first(), rom, {});

    QVERIFY(command.isValid());
    QVERIFY2(command.unresolvedPlaceholders.isEmpty(),
             qPrintable(command.unresolvedPlaceholders.join(u' ')));
    QCOMPARE(command.program, QStringLiteral("python"));
    // Le nom tordu reste UN argument, sans échappement : même propriété que l'autre
    // adaptateur, obtenue sans dupliquer le code de substitution.
    QVERIFY(command.arguments.contains(rom));
}

void TestRecalboxAdapter::registry_returnsRecalboxForItsTree()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath("recalbox/share_init")));

    const auto adapter = detectAdapter(dir.path());
    QVERIFY(adapter != nullptr);
    QCOMPARE(adapter->id(), QStringLiteral("recalbox"));
    QVERIFY(knownAdapterIds().contains(QStringLiteral("recalbox")));
}

QTEST_GUILESS_MAIN(TestRecalboxAdapter)
#include "test_recalbox_adapter.moc"
