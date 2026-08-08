#include "platform/BatoceraAdapter.h"

#include "platform/CommandLine.h"
#include "systems/EsSystemsParser.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace igiris::platform {

namespace {

// Emplacements du fichier de description des systèmes, du plus spécifique au plus général.
// La copie utilisateur l'emporte sur celle du système : c'est elle que Batocera lit quand
// elle existe, et c'est là que se retrouvent les systèmes ajoutés à la main.
const char *const kSystemsFileCandidates[] = {
    "userdata/system/configs/emulationstation/es_systems.cfg",
    "usr/share/emulationstation/es_systems.cfg",
};

// Marqueurs d'une installation Batocera. On en exige UN seul : une image montée en lecture
// seule n'expose pas forcément la partition userdata.
const char *const kDetectionMarkers[] = {
    "usr/share/batocera",
    "userdata/system",
};

// Noms de dossier divergents entre distributions de la famille EmulationStation, pour une
// même plateforme du catalogue. La liste est volontairement courte : l'essentiel des noms
// est partagé (megadrive, snes, psx…), ce sont les exceptions qui comptent.
//
// Elle vit dans l'adaptateur parce que c'est une donnée de distribution — le reste du code
// n'a jamais à connaître ces variantes.
struct Alias {
    const char *platformKey;
    const char *candidates; // séparés par des virgules, par ordre de préférence
};

const Alias kAliases[] = {
    { "megadrive",   "megadrive,genesis" },
    { "pcengine",    "pcengine,tg16,turbografx16" },
    { "mastersystem","mastersystem,sms" },
    { "gameandwatch","gameandwatch,gw" },
    { "arcade",      "arcade,mame,fbneo" },
};

} // namespace

BatoceraAdapter::BatoceraAdapter(QString rootPrefix)
    : m_root(std::move(rootPrefix))
{
    if (m_root.isEmpty())
        m_root = QStringLiteral("/");
}

QString BatoceraAdapter::absolutePath(const QString &relative) const
{
    return QDir::cleanPath(m_root + QLatin1Char('/') + relative);
}

bool BatoceraAdapter::detect(const QString &rootPrefix)
{
    const QString root = rootPrefix.isEmpty() ? QStringLiteral("/") : rootPrefix;
    for (const char *marker : kDetectionMarkers) {
        if (QFileInfo::exists(QDir::cleanPath(root + QLatin1Char('/')
                                              + QLatin1String(marker))))
            return true;
    }
    return false;
}

QString BatoceraAdapter::id() const
{
    return QStringLiteral("batocera");
}

QString BatoceraAdapter::displayName() const
{
    return QStringLiteral("Batocera");
}

Capabilities BatoceraAdapter::capabilities() const
{
    return Capability::SystemsFile | Capability::RomDirectories | Capability::Launch
         | Capability::PerSystemEmulator;
}

QString BatoceraAdapter::systemsFilePath() const
{
    for (const char *candidate : kSystemsFileCandidates) {
        const QString path = absolutePath(QLatin1String(candidate));
        if (QFileInfo::exists(path))
            return path;
    }
    return {}; // introuvable : à traiter par l'appelant, pas à masquer
}

QList<SystemEntry> BatoceraAdapter::readSystems(QString *error) const
{
    const QString path = systemsFilePath();
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("aucun es_systems.cfg sous %1").arg(m_root);
        return {};
    }

    // Batocera suit la convention EmulationStation : le parser du lot 2 sait la lire.
    const auto result = systems::parseEsSystemsFile(path);
    if (!result.ok()) {
        if (error)
            *error = result.error.message; // verbatim (§15)
        return {};
    }
    if (error)
        error->clear();
    return result.systems;
}

QString BatoceraAdapter::resolvePlatformKey(const QString &platformKey,
                                            const QStringList &localSystems) const
{
    if (platformKey.isEmpty())
        return {};

    // Le nom du catalogue est presque toujours le nom local : on l'essaie d'abord.
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

    // Absent de cette installation : c'est le statut noir du §7, pas une erreur.
    return {};
}

QStringList BatoceraAdapter::romDirectories() const
{
    const QString roms = absolutePath(QStringLiteral("userdata/roms"));
    if (!QFileInfo::exists(roms))
        return {};
    return { roms };
}

LaunchCommand BatoceraAdapter::buildLaunchCommand(const SystemEntry   &system,
                                                  const QString       &romPath,
                                                  const LaunchDetails &details) const
{
    LaunchCommand result;
    if (!system.isValid() || romPath.isEmpty())
        return result;

    // La commande est celle du fichier de description, jamais un chemin reconstitué (§1).
    // L'option retenue est celle demandée, la première par défaut : le fichier les liste
    // par ordre de préférence (§7).
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
    context.systemFullName = system.fullName; // %SYSTEMNAME% = le LIBELLÉ, pas la clé
    context.gameName       = details.gameName;
    context.homePath       = QDir::homePath();
    // Vides, comme l'amont lorsqu'il n'a rien à fournir : pas de fiche XML produite, et
    // aucune manette transmise tant que la capacité ControllerMapping n'est pas déclarée.
    context.gameInfoXmlPath   = QString();
    context.controllersConfig = QString();

    const SubstitutionResult substituted = substitutePlaceholders(tokens, context);

    result.program                = substituted.tokens.first();
    result.arguments              = substituted.tokens.mid(1);
    result.unresolvedPlaceholders = substituted.unresolved;
    return result;
}

bool BatoceraAdapter::launch(const SystemEntry &system, const QString &romPath,
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

    // Pas de shell : programme et arguments restent séparés jusqu'au bout.
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
