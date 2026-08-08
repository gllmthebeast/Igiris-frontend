// Tests du lot 2 — parser du fichier de description des systèmes.
//
// Les cas tordus testés ici ne sont pas imaginés : ils viennent d'un vrai fichier de
// référence (195 systèmes), récupéré hors du dépôt conformément au §15.

#include "systems/EsSystemsParser.h"

#include <QBuffer>
#include <QTest>

using namespace igiris::systems;

namespace {

ParseResult parse(const QByteArray &xml)
{
    QBuffer buffer;
    buffer.setData(xml);
    buffer.open(QIODevice::ReadOnly);
    return parseEsSystems(&buffer);
}

} // namespace

class TestEsSystemsParser : public QObject
{
    Q_OBJECT

private slots:
    void parses_minimalSystem();
    void parses_multipleLabelledCommands();
    void keeps_commandOrderAsPreference();
    void keeps_pathVerbatimWithPlaceholders();
    void extensions_areLoweredAndDeduplicated();
    void warns_onSystemWithoutName();
    void warns_onSystemWithoutCommand();
    void fails_onMalformedXmlWithPosition();
    void fails_onWrongRootElement();
    void fails_onMissingFile();
    void collects_placeholdersAcrossSystems();
};

void TestEsSystemsParser::parses_minimalSystem()
{
    const auto r = parse(R"(<systemList>
        <system>
            <name>snes</name>
            <fullname>Super Nintendo</fullname>
            <path>/roms/snes</path>
            <extension>.sfc .smc</extension>
            <command>/bin/run %ROM%</command>
            <platform>snes</platform>
            <theme>snes</theme>
        </system>
    </systemList>)");

    QVERIFY2(r.ok(), qPrintable(r.error.message));
    QCOMPARE(r.systems.size(), 1);
    QCOMPARE(r.systems.first().name, QStringLiteral("snes"));
    QCOMPARE(r.systems.first().fullName, QStringLiteral("Super Nintendo"));
    QCOMPARE(r.systems.first().platform, QStringLiteral("snes"));
    QCOMPARE(r.systems.first().defaultCommand(), QStringLiteral("/bin/run %ROM%"));
}

void TestEsSystemsParser::parses_multipleLabelledCommands()
{
    // Cas réel : un système peut proposer plusieurs émulateurs, chacun étiqueté.
    const auto r = parse(R"(<systemList>
        <system>
            <name>adam</name>
            <command label="MAME [Diskette]">/bin/mame -flop1 %ROM%</command>
            <command label="MAME [Cartridge]">/bin/mame -cart1 %ROM%</command>
            <command label="ColEm">/bin/colem %ROM%</command>
        </system>
    </systemList>)");

    QVERIFY(r.ok());
    QCOMPARE(r.systems.first().launchOptions.size(), 3);
    QCOMPARE(r.systems.first().launchOptions.at(1).label,
             QStringLiteral("MAME [Cartridge]"));
}

void TestEsSystemsParser::keeps_commandOrderAsPreference()
{
    const auto r = parse(R"(<systemList><system><name>x</name>
        <command label="premier">/bin/a %ROM%</command>
        <command label="second">/bin/b %ROM%</command>
    </system></systemList>)");

    QVERIFY(r.ok());
    QCOMPARE(r.systems.first().defaultCommand(), QStringLiteral("/bin/a %ROM%"));
}

void TestEsSystemsParser::keeps_pathVerbatimWithPlaceholders()
{
    // Le chemin n'est PAS toujours littéral. Le résoudre ici serait empiéter sur
    // l'adaptateur, seul à savoir ce que vaut %ROMPATH% sur cette installation.
    const auto r = parse(R"(<systemList><system><name>3do</name>
        <path>%ROMPATH%/3do</path>
        <command>/bin/run %ROM%</command>
    </system></systemList>)");

    QVERIFY(r.ok());
    QCOMPARE(r.systems.first().romPath, QStringLiteral("%ROMPATH%/3do"));
}

void TestEsSystemsParser::extensions_areLoweredAndDeduplicated()
{
    // Les vrais fichiers listent chaque extension dans les deux casses.
    const auto r = parse(R"(<systemList><system><name>x</name>
        <extension>.bin .BIN .chd .CHD .zip</extension>
        <command>/bin/run %ROM%</command>
    </system></systemList>)");

    QVERIFY(r.ok());
    QCOMPARE(r.systems.first().extensions,
             (QStringList{ ".bin", ".chd", ".zip" }));
}

void TestEsSystemsParser::warns_onSystemWithoutName()
{
    const auto r = parse(R"(<systemList>
        <system><command>/bin/run %ROM%</command></system>
        <system><name>ok</name><command>/bin/run %ROM%</command></system>
    </systemList>)");

    QVERIFY(r.ok());
    QCOMPARE(r.systems.size(), 1);
    QCOMPARE(r.warnings.size(), 1); // ignoré, mais SIGNALÉ
}

void TestEsSystemsParser::warns_onSystemWithoutCommand()
{
    // Présent mais injouable : le faire disparaître sans rien dire produirait un statut
    // noir inexplicable.
    const auto r = parse(R"(<systemList>
        <system><name>muet</name><path>/roms/muet</path></system>
    </systemList>)");

    QVERIFY(r.ok());
    QVERIFY(r.systems.isEmpty());
    QCOMPARE(r.warnings.size(), 1);
    QVERIFY(r.warnings.first().contains(QStringLiteral("muet")));
}

void TestEsSystemsParser::fails_onMalformedXmlWithPosition()
{
    const auto r = parse(R"(<systemList>
        <system><name>snes</name><command>/bin/run</command>
    </systemList>)");

    QVERIFY(!r.ok());
    QVERIFY(!r.error.message.isEmpty()); // message de Qt, verbatim
    QVERIFY(r.error.line > 0);           // avec sa position
    QVERIFY(r.systems.isEmpty());        // rien de partiel n'est retourné
}

void TestEsSystemsParser::fails_onWrongRootElement()
{
    const auto r = parse(R"(<gameList><game><name>x</name></game></gameList>)");

    QVERIFY(!r.ok());
    QVERIFY(r.error.message.contains(QStringLiteral("systemList")));
}

void TestEsSystemsParser::fails_onMissingFile()
{
    const auto r = parseEsSystemsFile(QStringLiteral("/n/existe/pas.cfg"));
    QVERIFY(!r.ok());
    QVERIFY(r.error.message.contains(QStringLiteral("/n/existe/pas.cfg")));
}

void TestEsSystemsParser::collects_placeholdersAcrossSystems()
{
    const auto r = parse(R"(<systemList>
        <system><name>a</name><command>/bin/a %ROM% %CORE_RETROARCH%</command></system>
        <system><name>b</name><command>/bin/b %ROM% %GAMEDIR%</command></system>
    </systemList>)");

    QVERIFY(r.ok());
    QCOMPARE(collectCommandPlaceholders(r.systems),
             (QStringList{ "%CORE_RETROARCH%", "%GAMEDIR%", "%ROM%" }));
}

QTEST_GUILESS_MAIN(TestEsSystemsParser)
#include "test_es_systems_parser.moc"
