#include "platform/BatoceraAdapter.h"

#include "platform/CommandLine.h"
#include "systems/EsSystemsParser.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

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
    Capabilities caps = Capability::SystemsFile | Capability::RomDirectories
                      | Capability::Launch | Capability::PerSystemEmulator;

    // Déclarée seulement si le binaire est RÉELLEMENT là. Une entrée de menu qui ne fait
    // rien est pire qu'une entrée grisée qui dit pourquoi (§1).
    if (hostSettingsCommand().isValid())
        caps |= Capability::HostSettings;

    return caps;
}

LaunchCommand BatoceraAdapter::hostSettingsCommand() const
{
    // Le frontend d'origine, qui est AUSSI la seule interface de configuration du système :
    // manettes, wifi, bluetooth, audio, résolution, mise à jour. On le relance tel quel
    // plutôt que de refaire ce qu'il fait déjà (§0, §12).
    //
    // ⚠️ « emulationstation-standalone » N'EST PAS le binaire : c'est un script bash qui
    // BOUCLE, lu dans l'image 43.1 :
    //
    //     touch "${REBOOT_FLAG}"
    //     while [ -e "${REBOOT_FLAG}" ]; do
    //         dbus-run-session -- emulationstation …
    //         [ -e /tmp/shutdown.please ] || [ -e /tmp/reboot.please ] && break
    //     done
    //
    // Il existe pour rendre EmulationStation INQUITTABLE : quoi qu'on choisisse dans son
    // menu, il le relance. Ses deux seules sorties sont l'extinction et le redémarrage.
    //
    // Le lancer tel quel enferme donc l'utilisateur : il retrouve ses réglages, mais plus
    // jamais notre interface. C'est ce qui est arrivé au premier essai sur appareil, et
    // c'est pourquoi le désarmement de sa boucle fait partie de l'ouverture (openHostSettings).
    //
    // On garde malgré tout le wrapper plutôt que le binaire nu : il pose HOME, la langue,
    // le répertoire courant, attend le compositeur et enveloppe dans dbus-run-session.
    // Refaire tout ça ici serait précisément la connaissance de distribution que le §1
    // veut voir rester chez l'hôte.
    static const char *const candidates[] = {
        "emulationstation-standalone",
        "emulationstation",
    };

    for (const char *name : candidates) {
        // D'abord sous la racine de l'adaptateur : c'est ce qui rend le cas testable sur
        // une image montée, sans rien exécuter de la machine hôte.
        const QString local = absolutePath(QStringLiteral("usr/bin/%1").arg(
            QLatin1String(name)));
        if (QFileInfo(local).isExecutable())
            return { local, {}, {} };
    }

    // Puis le PATH, pour le cas normal d'un appareil où l'adaptateur est enraciné sur « / ».
    for (const char *name : candidates) {
        const QString found = QStandardPaths::findExecutable(QLatin1String(name));
        if (!found.isEmpty())
            return { found, {}, {} };
    }

    return {};
}

QString BatoceraAdapter::hostSettingsLabel() const
{
    return QStringLiteral("Paramètres %1").arg(displayName());
}

bool BatoceraAdapter::hostFrontendIsRunning() const
{
    // Lu dans /proc, et non déduit d'un fichier drapeau.
    //
    // Le wrapper pose bien /var/run/emulationstation-standalone, mais ce drapeau SURVIT à
    // un plantage : on prendrait alors une trace périmée pour un frontend vivant, et le
    // bouton se mettrait à quitter l'application au lieu d'ouvrir les réglages. Un
    // processus, lui, est là ou n'est pas là.
    //
    // Le préfixe de racine rend la chose testable sans dépendre des processus de la
    // machine de développement — c'est aussi ce qui fait que ce test ne peut pas passer
    // par accident.
    QDir proc(absolutePath(QStringLiteral("proc")));
    if (!proc.exists())
        return false;

    const auto entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isPid = false;
        entry.toInt(&isPid);
        if (!isPid)
            continue;

        QFile comm(proc.filePath(entry + QStringLiteral("/comm")));
        if (!comm.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString name = QString::fromLatin1(comm.readLine()).trimmed();
        // « emulationstation » comme « emulationstation-standalone » : les deux signifient
        // que l'écran d'accueil d'origine occupe déjà la machine.
        if (name.startsWith(QLatin1String("emulationstation")))
            return true;
    }
    return false;
}

QString BatoceraAdapter::hostReturnLabel() const
{
    return QStringLiteral("Retour à %1").arg(displayName());
}

QString BatoceraAdapter::hostSettingsReturnHint() const
{
    // Le libellé exact du menu de l'hôte, qu'on ne peut pas renommer et que personne ne
    // devinerait : « Redémarrer EmulationStation » est ce qui RAMÈNE ICI, une fois sa
    // boucle désarmée. « Redémarrer le système » marche aussi, mais redémarre la machine.
    return QStringLiteral("Pour revenir ici : menu de %1 → Quitter → "
                          "« Redémarrer EmulationStation ».")
        .arg(displayName());
}

bool BatoceraAdapter::openHostSettings(QString *error) const
{
    const auto setError = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const LaunchCommand command = hostSettingsCommand();
    if (!command.isValid())
        return setError(QStringLiteral("interface de réglages introuvable sur cette "
                                       "installation : ni emulationstation-standalone ni "
                                       "emulationstation dans /usr/bin ni dans le PATH"));

    // Détaché, et SANS quitter : sur les deux chaînes de démarrage de cet hôte — labwc en
    // Wayland, openbox en X11 — les deux applications sont des clientes du même
    // compositeur. La seconde s'affiche par-dessus, et sa fermeture nous redonne l'écran.
    //
    // Se terminer soi-même serait plus simple mais irréversible : si le binaire de réglages
    // ne démarre pas, l'utilisateur se retrouverait devant un écran vide, sans interface
    // pour rien réparer.
    QProcess process;
    process.setProgram(command.program);
    process.setArguments(command.arguments);

    if (!process.startDetached())
        return setError(QStringLiteral("échec de l'ouverture de « %1 » : %2")
                            .arg(command.program, process.errorString()));

    disarmSettingsLoop();

    if (error)
        error->clear();
    return true;
}

void BatoceraAdapter::disarmSettingsLoop() const
{
    // DÉSARMER la boucle du wrapper, sans quoi l'utilisateur ne revient jamais ici.
    //
    // Le wrapper expose lui-même de quoi le faire — c'est son propre mécanisme, pas une
    // ruse de notre part :
    //
    //     if [ "$1" = "--stop-rebooting" ]; then rm -f "${REBOOT_FLAG}"; exit 0; fi
    //
    // Pas de course à craindre : le wrapper pose son drapeau dans ses premières
    // millisecondes, et ne le RELIT qu'après la fermeture d'EmulationStation. On a donc
    // toute la session pour le retirer. Le délai laisse simplement passer le `touch`.
    //
    // Ensuite, quitter EmulationStation par « Redémarrer EmulationStation » le fait sortir,
    // la boucle constate l'absence du drapeau, le wrapper se termine — et notre fenêtre est
    // toujours là, dessous.
    const LaunchCommand command = hostSettingsCommand();
    if (!command.isValid() || !command.program.endsWith(QLatin1String("-standalone")))
        return; // binaire nu : pas de boucle à désarmer

    QTimer::singleShot(3000, [program = command.program]() {
        QProcess::startDetached(program, { QStringLiteral("--stop-rebooting") });
    });
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

QStringList BatoceraAdapter::exportSearchPaths() const
{
    // /userdata est la SEULE partition inscriptible : le reste du système est en lecture
    // seule et serait écrasé à la mise à jour. C'est donc là que l'installateur dépose
    // l'export, et là qu'il faut le chercher.
    //
    // Les chemins sont renvoyés qu'ils existent ou non : c'est l'appelant qui teste. Une
    // liste vide se lirait « cette distribution ne sait pas où est l'export », ce qui est
    // faux — elle le sait, le fichier n'a simplement pas encore été téléchargé.
    return {
        absolutePath(QStringLiteral("userdata/system/igiris/games.db")),
        absolutePath(QStringLiteral("userdata/igiris/games.db")),
    };
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
