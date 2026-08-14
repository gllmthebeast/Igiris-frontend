#include "platform/RecalboxAdapter.h"

#include "platform/CommandLine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace igiris::platform {

namespace {

// systemlist.xml, du plus spécifique au plus général.
// La copie utilisateur (share) l'emporte sur le modèle livré (share_init) : c'est l'ordre
// qu'applique SystemDeserializer côté Recalbox.
const char *const kSystemsFileCandidates[] = {
    "recalbox/share/system/.emulationstation/systemlist.xml",
    "recalbox/share_init/system/.emulationstation/systemlist.xml",
};

const char *const kDetectionMarkers[] = {
    "recalbox/share_init",
    "recalbox/share/system",
    "usr/share/recalbox",
};

// Mêmes divergences de nommage que chez la cible de référence : la famille
// EmulationStation partage l'essentiel des noms de dossiers, seules les exceptions
// comptent.
struct Alias {
    const char *platformKey;
    const char *candidates;
};

const Alias kAliases[] = {
    { "megadrive", "megadrive,genesis" },
    { "pcengine", "pcengine,tg16,turbografx16" },
    { "mastersystem", "mastersystem,sms" },
    { "gameandwatch", "gameandwatch,gw" },
    { "arcade", "arcade,mame,fbneo" },
};

QStringList parseExtensions(const QString &raw)
{
    QStringList out;
    const auto  parts = raw.split(QRegularExpression(QStringLiteral("\\s+")),
                                  Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString lower = part.toLower();
        if (!out.contains(lower))
            out.append(lower);
    }
    return out;
}

} // namespace

RecalboxAdapter::RecalboxAdapter(QString rootPrefix)
    : m_root(std::move(rootPrefix))
{
    if (m_root.isEmpty())
        m_root = QStringLiteral("/");
}

QString RecalboxAdapter::absolutePath(const QString &relative) const
{
    return QDir::cleanPath(m_root + QLatin1Char('/') + relative);
}

bool RecalboxAdapter::detect(const QString &rootPrefix)
{
    const QString root = rootPrefix.isEmpty() ? QStringLiteral("/") : rootPrefix;
    for (const char *marker : kDetectionMarkers) {
        if (QFileInfo::exists(
                QDir::cleanPath(root + QLatin1Char('/') + QLatin1String(marker))))
            return true;
    }
    return false;
}

QString RecalboxAdapter::id() const
{
    return QStringLiteral("recalbox");
}

QString RecalboxAdapter::displayName() const
{
    return QStringLiteral("Recalbox");
}

Capabilities RecalboxAdapter::capabilities() const
{
    // Comme la cible de référence : pas de ControllerMapping tant que les manettes ne sont
    // pas énumérées. Déclaré, jamais supposé (§1).
    return Capability::SystemsFile | Capability::RomDirectories | Capability::Launch
         | Capability::PerSystemEmulator;
}

QString RecalboxAdapter::systemsFilePath() const
{
    for (const char *candidate : kSystemsFileCandidates) {
        const QString path = absolutePath(QLatin1String(candidate));
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QList<SystemEntry> RecalboxAdapter::readSystems(QString *error) const
{
    const QString path = systemsFilePath();
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("aucun systemlist.xml sous %1").arg(m_root);
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("impossible d'ouvrir %1 : %2").arg(path, file.errorString());
        return {};
    }

    // Le format de Recalbox n'est PAS celui d'EmulationStation. Tout est en ATTRIBUTS :
    //
    //   <system name="snes" fullname="Super Nintendo">
    //     <descriptor path="…" extensions="…" theme="…" command="…"/>
    //
    // là où la convention ES écrit <name>snes</name>. C'est cette divergence qui a
    // invalidé la première version de l'interface (§13).
    QXmlStreamReader   xml(&file);
    QList<SystemEntry> systems;

    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() != QLatin1String("system"))
            continue;

        SystemEntry entry;
        entry.name     = xml.attributes().value(QLatin1String("name")).toString().trimmed();
        entry.fullName = xml.attributes().value(QLatin1String("fullname")).toString().trimmed();

        // Descendre dans ce <system> pour y lire <descriptor>.
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("descriptor")) {
                const auto attributes = xml.attributes();
                entry.romPath = attributes.value(QLatin1String("path")).toString().trimmed();
                entry.theme   = attributes.value(QLatin1String("theme")).toString().trimmed();
                entry.extensions =
                    parseExtensions(attributes.value(QLatin1String("extensions")).toString());

                const QString command =
                    attributes.value(QLatin1String("command")).toString().trimmed();
                if (!command.isEmpty())
                    entry.launchOptions.append(LaunchOption{ QString(), command });
            }
            xml.skipCurrentElement();
        }

        if (!entry.name.isEmpty() && !entry.launchOptions.isEmpty())
            systems.append(entry);
    }

    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("%1 (ligne %2) : %3")
                         .arg(path)
                         .arg(xml.lineNumber())
                         .arg(xml.errorString());
        return {};
    }

    if (error)
        error->clear();
    return systems;
}

QString RecalboxAdapter::resolvePlatformKey(const QString &platformKey,
                                            const QStringList &localSystems) const
{
    if (platformKey.isEmpty())
        return {};
    if (localSystems.contains(platformKey))
        return platformKey;

    for (const Alias &alias : kAliases) {
        if (platformKey != QLatin1String(alias.platformKey))
            continue;
        const auto candidates = QString::fromLatin1(alias.candidates)
                                    .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &candidate : candidates) {
            if (localSystems.contains(candidate))
                return candidate;
        }
    }
    return {};
}

QStringList RecalboxAdapter::romDirectories() const
{
    const QString roms = absolutePath(QStringLiteral("recalbox/share/roms"));
    if (!QFileInfo::exists(roms))
        return {};
    return { roms };
}

QStringList RecalboxAdapter::exportSearchPaths() const
{
    // Même raisonnement que chez Batocera, autre partition : c'est /recalbox/share qui est
    // inscriptible ici. La différence de chemin est précisément ce que cette méthode existe
    // pour absorber.
    return {
        absolutePath(QStringLiteral("recalbox/share/system/igiris/games.db")),
        absolutePath(QStringLiteral("recalbox/share/igiris/games.db")),
    };
}

LaunchCommand RecalboxAdapter::buildLaunchCommand(const SystemEntry   &system,
                                                  const QString       &romPath,
                                                  const LaunchDetails &details) const
{
    LaunchCommand result;
    if (!system.isValid() || romPath.isEmpty())
        return result;

    const int index = (details.optionIndex >= 0
                       && details.optionIndex < system.launchOptions.size())
                          ? details.optionIndex
                          : 0;
    const QStringList tokens = tokenizeCommand(system.launchOptions.at(index).command);
    if (tokens.isEmpty())
        return result;

    LaunchContext context;
    context.romPath        = romPath;
    context.systemName     = system.name;
    context.systemFullName = system.fullName;
    context.gameName       = details.gameName;
    context.homePath       = QDir::homePath();

    const SubstitutionResult substituted = substitutePlaceholders(tokens, context);

    result.program                = substituted.tokens.first();
    result.arguments              = substituted.tokens.mid(1);
    result.unresolvedPlaceholders = substituted.unresolved;
    return result;
}

bool RecalboxAdapter::launch(const SystemEntry &system, const QString &romPath,
                             const LaunchDetails &details, QString *error) const
{
    const auto setError = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const LaunchCommand command = buildLaunchCommand(system, romPath, details);
    if (!command.isValid())
        return setError(QStringLiteral("commande de lancement vide pour le système « %1 »")
                            .arg(system.name));

    if (!command.unresolvedPlaceholders.isEmpty())
        return setError(QStringLiteral("placeholders non reconnus dans la commande de "
                                       "« %1 » : %2 — lancement refusé")
                            .arg(system.name,
                                 command.unresolvedPlaceholders.join(QStringLiteral(", "))));

    if (!QFileInfo::exists(romPath))
        return setError(QStringLiteral("ROM introuvable : %1").arg(romPath));

    QProcess process;
    process.setProgram(command.program);
    process.setArguments(command.arguments);

    if (!process.startDetached())
        return setError(QStringLiteral("échec du lancement de « %1 » : %2")
                            .arg(command.program, process.errorString()));

    if (error)
        error->clear();
    return true;
}

} // namespace igiris::platform
