#include "systems/EsSystemsParser.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace igiris::systems {

using platform::LaunchOption;
using platform::SystemEntry;

namespace {

// « .smc .SMC .sfc .zip » → { ".smc", ".sfc", ".zip" }.
//
// Les fichiers réels listent systématiquement chaque extension dans les deux casses. Les
// garder telles quelles doublerait la liste sans rien apporter : la comparaison de suffixe
// se fera de toute façon sans tenir compte de la casse.
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

void readSystem(QXmlStreamReader &xml, ParseResult &result)
{
    SystemEntry entry;

    while (xml.readNextStartElement()) {
        const QStringView tag = xml.name();

        if (tag == QLatin1String("name")) {
            entry.name = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("fullname")) {
            entry.fullName = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("path")) {
            entry.romPath = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("extension")) {
            entry.extensions = parseExtensions(xml.readElementText());
        } else if (tag == QLatin1String("platform")) {
            entry.platform = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("theme")) {
            entry.theme = xml.readElementText().trimmed();
        } else if (tag == QLatin1String("command")) {
            // Plusieurs <command> par système sont légitimes, chacune étiquetée. L'ordre
            // du fichier est un ordre de préférence : on le conserve.
            const QString label = xml.attributes()
                                      .value(QLatin1String("label"))
                                      .toString()
                                      .trimmed();
            const QString command = xml.readElementText().trimmed();
            if (!command.isEmpty())
                entry.launchOptions.append(LaunchOption{ label, command });
        } else {
            xml.skipCurrentElement();
        }
    }

    if (entry.name.isEmpty()) {
        result.warnings.append(
            QStringLiteral("système sans <name> ignoré (ligne %1)").arg(xml.lineNumber()));
        return;
    }
    if (entry.launchOptions.isEmpty()) {
        // Sans commande, le système est présent mais injouable : le dire explicitement
        // vaut mieux que de le faire disparaître de la liste.
        result.warnings.append(QStringLiteral("système « %1 » sans <command> : ignoré")
                                   .arg(entry.name));
        return;
    }

    result.systems.append(entry);
}

} // namespace

ParseResult parseEsSystems(QIODevice *device)
{
    ParseResult result;

    if (!device) {
        result.error = { QStringLiteral("aucun flux à lire"), 0, 0 };
        return result;
    }

    QXmlStreamReader xml(device);

    if (!xml.readNextStartElement()) {
        result.error = { QStringLiteral("document vide ou illisible : %1")
                             .arg(xml.errorString()),
                         xml.lineNumber(), xml.columnNumber() };
        return result;
    }

    if (xml.name() != QLatin1String("systemList")) {
        result.error = { QStringLiteral("racine attendue <systemList>, trouvé <%1>")
                             .arg(xml.name().toString()),
                         xml.lineNumber(), xml.columnNumber() };
        return result;
    }

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("system"))
            readSystem(xml, result);
        else
            xml.skipCurrentElement();
    }

    if (xml.hasError()) {
        // XML malformé : on remonte le message de Qt tel quel, avec sa position (§15).
        result.error = { xml.errorString(), xml.lineNumber(), xml.columnNumber() };
        result.systems.clear();
    }

    return result;
}

ParseResult parseEsSystemsFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ParseResult result;
        result.error = { QStringLiteral("impossible d'ouvrir %1 : %2")
                             .arg(path, file.errorString()),
                         0, 0 };
        return result;
    }
    return parseEsSystems(&file);
}

QStringList collectCommandPlaceholders(const QList<SystemEntry> &systems)
{
    static const QRegularExpression re(QStringLiteral("%[A-Z0-9_]+%"));

    QSet<QString> found;
    for (const SystemEntry &system : systems) {
        for (const LaunchOption &option : system.launchOptions) {
            auto it = re.globalMatch(option.command);
            while (it.hasNext())
                found.insert(it.next().captured(0));
        }
    }

    QStringList out(found.cbegin(), found.cend());
    out.sort();
    return out;
}

} // namespace igiris::systems
