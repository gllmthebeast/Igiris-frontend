#include "platform/CommandLine.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace igiris::platform {

QStringList tokenizeCommand(const QString &command)
{
    QStringList tokens;
    QString     current;
    bool        inToken = false;
    QChar       quote;  // caractère de guillemet ouvert, nul si hors guillemets

    for (int i = 0; i < command.size(); ++i) {
        const QChar c = command.at(i);

        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else
                current.append(c);
            continue;
        }

        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            // Un guillemet ouvre un jeton même s'il est vide : "" est un argument valide.
            quote   = c;
            inToken = true;
            continue;
        }

        if (c.isSpace()) {
            if (inToken) {
                tokens.append(current);
                current.clear();
                inToken = false;
            }
            continue;
        }

        current.append(c);
        inToken = true;
    }

    if (inToken)
        tokens.append(current);

    return tokens;
}

SubstitutionResult substitutePlaceholders(const QStringList &tokens,
                                          const LaunchContext &context)
{
    const QFileInfo romInfo(context.romPath);

    // Ordre important : les variantes longues passent AVANT %ROM%, sinon « %ROM » serait
    // remplacé en premier et laisserait un « _RAW% » ou « RAW% » orphelin.
    //
    // Les deux orthographes de ROM_RAW existent réellement dans la famille
    // EmulationStation — %ROMRAW% dans un fichier de référence de 195 systèmes, %ROM_RAW%
    // ailleurs. Choisir un camp ferait échouer les lancements de l'autre, en silence.
    const QList<QPair<QString, QString>> known = {
        { QStringLiteral("%CONTROLLERSCONFIG%"), context.controllersConfig },
        { QStringLiteral("%GAMEINFOXML%"), context.gameInfoXmlPath },
        { QStringLiteral("%SYSTEMNAME%"), context.systemFullName },
        { QStringLiteral("%GAMENAME%"), context.gameName },
        { QStringLiteral("%BASENAME%"), romInfo.completeBaseName() },
        { QStringLiteral("%GAMEDIR%"), romInfo.absolutePath() },
        { QStringLiteral("%ROM_RAW%"), context.romPath },
        { QStringLiteral("%ROMRAW%"), context.romPath },
        { QStringLiteral("%ROM%"), context.romPath },
        { QStringLiteral("%SYSTEM%"), context.systemName },
        { QStringLiteral("%HOME%"), context.homePath },
    };

    static const QRegularExpression placeholderRe(QStringLiteral("%[A-Z0-9_]+%"));

    SubstitutionResult result;
    result.tokens.reserve(tokens.size());

    for (const QString &token : tokens) {
        QString    substituted = token;
        const bool hadPlaceholder = placeholderRe.match(token).hasMatch();

        for (const auto &[needle, value] : known)
            substituted.replace(needle, value);

        // Ce qui ressemble encore à un placeholder après coup ne nous est pas connu.
        auto it = placeholderRe.globalMatch(substituted);
        while (it.hasNext()) {
            const QString name = it.next().captured(0);
            if (!result.unresolved.contains(name))
                result.unresolved.append(name);
        }

        // Un jeton qui n'était QUE des placeholders et qui se résout en rien disparaît :
        // le garder produirait un argument vide, que l'amont n'a jamais puisqu'il
        // assemble une chaîne.
        if (hadPlaceholder && substituted.isEmpty()) {
            result.droppedEmpty.append(token);
            continue;
        }

        result.tokens.append(substituted);
    }

    return result;
}

} // namespace igiris::platform
