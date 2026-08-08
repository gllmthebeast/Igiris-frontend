#pragma once

// Découpage et substitution de la ligne de commande lue dans le fichier de description
// des systèmes.
//
// Ces fonctions ne connaissent aucune distribution : elles traitent la convention
// EmulationStation, commune à toute la famille (§1).
//
// ⚠️ Différence ASSUMÉE avec EmulationStation amont : celui-ci construit UNE chaîne qu'il
// confie ensuite à un shell, et il échappe donc ses valeurs (guillemets autour des
// chemins). Ici, programme et arguments restent séparés jusqu'à QProcess : reproduire cet
// échappement ferait entrer les guillemets DANS l'argument, et l'émulateur recevrait un
// chemin littéralement entouré de guillemets. On substitue donc les valeurs brutes.

#include <QString>
#include <QStringList>

namespace igiris::platform {

// Découpe une ligne de commande en jetons, en respectant les guillemets simples et
// doubles. Aucun shell n'est impliqué : c'est précisément le but.
QStringList tokenizeCommand(const QString &command);

// Contexte de substitution des placeholders de la convention EmulationStation.
//
// Les valeurs vides sont légitimes : elles correspondent à ce que fait l'amont quand
// l'information n'existe pas (aucune manette branchée, pas de fiche XML produite).
struct LaunchContext {
    QString romPath;          // %ROM%, %ROM_RAW%, %ROMRAW%
    QString systemName;       // %SYSTEM%   — clé locale, « snes »
    QString systemFullName;   // %SYSTEMNAME% — libellé, « Super Nintendo… »
    QString gameName;         // %GAMENAME%
    QString homePath;         // %HOME%
    QString gameInfoXmlPath;  // %GAMEINFOXML% — vide = non produite, comme l'amont
    QString controllersConfig; // %CONTROLLERSCONFIG% — vide = aucune manette transmise
};

struct SubstitutionResult {
    QStringList tokens;
    QStringList unresolved; // placeholders inconnus, laissés verbatim dans les jetons

    // Jetons supprimés parce qu'un placeholder connu s'est résolu en RIEN.
    // Sans cette suppression, « %CONTROLLERSCONFIG% » deviendrait un argument VIDE passé
    // au lanceur — ce que l'amont n'a jamais : il assemble une chaîne, où le vide
    // disparaît de lui-même.
    QStringList droppedEmpty;
};

SubstitutionResult substitutePlaceholders(const QStringList &tokens,
                                          const LaunchContext &context);

} // namespace igiris::platform
