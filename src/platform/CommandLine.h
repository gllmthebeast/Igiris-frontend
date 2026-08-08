#pragma once

// Découpage et substitution de la ligne de commande lue dans le fichier de description
// des systèmes.
//
// Ces fonctions ne connaissent aucune distribution : elles traitent la convention
// EmulationStation, qui est commune à toute la famille (§1). Elles vivent malgré tout
// sous platform/ parce qu'elles n'ont de sens que pour les adaptateurs.

#include <QString>
#include <QStringList>

namespace igiris::platform {

// Découpe une ligne de commande en jetons, en respectant les guillemets simples et
// doubles. Aucun shell n'est impliqué : c'est précisément le but.
QStringList tokenizeCommand(const QString &command);

// Contexte de substitution des placeholders de la convention EmulationStation.
struct LaunchContext {
    QString romPath;    // chemin complet de la ROM
    QString systemName; // nom de système local
};

struct SubstitutionResult {
    QStringList tokens;
    QStringList unresolved; // placeholders inconnus, laissés verbatim dans les jetons
};

// Remplace %ROM%, %ROM_RAW%, %BASENAME%, %GAMEDIR% et %SYSTEM% dans chaque jeton.
//
// Un placeholder inconnu n'est PAS supprimé : il reste tel quel et son nom est remonté
// dans `unresolved`. Le supprimer silencieusement fabriquerait une commande plausible mais
// fausse, et l'échec serait attribué à l'émulateur.
SubstitutionResult substitutePlaceholders(const QStringList &tokens,
                                          const LaunchContext &context);

} // namespace igiris::platform
