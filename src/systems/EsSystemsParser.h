#pragma once

// Lecture du fichier de description des systèmes.
//
// Le format <systemList><system> est une convention EmulationStation, pas une invention
// d'une distribution (§1) : ce parser ne connaît donc AUCUNE distribution, et il est
// soumis au test no-distro-literals comme le reste du code.
//
// Il est délibérément FIDÈLE : il ne résout ni les placeholders (« %ROMPATH%/snes »), ni
// les « ~ », ni les chemins relatifs. Cette résolution dépend de la distribution, donc
// elle appartient à l'adaptateur.

#include "platform/PlatformAdapter.h"

#include <QList>
#include <QString>
#include <QStringList>

class QIODevice;

namespace igiris::systems {

struct ParseError {
    QString message; // message COMPLET, jamais reformulé (§15)
    qint64  line   = 0;
    qint64  column = 0;

    bool isError() const { return !message.isEmpty(); }
};

struct ParseResult {
    QList<platform::SystemEntry> systems;
    ParseError                   error;

    // Systèmes ignorés et pourquoi. Un système écarté en silence deviendrait un statut
    // noir inexplicable pour l'utilisateur : on le signale toujours.
    QStringList warnings;

    bool ok() const { return !error.isError(); }
};

ParseResult parseEsSystems(QIODevice *device);
ParseResult parseEsSystemsFile(const QString &path);

// Placeholders présents dans les commandes, tous systèmes confondus. Sert de diagnostic :
// c'est la liste de ce qu'un adaptateur devra savoir substituer pour ce fichier.
QStringList collectCommandPlaceholders(const QList<platform::SystemEntry> &systems);

} // namespace igiris::systems
