#pragma once

// Fabrique d'adaptateurs.
//
// Elle existe pour une raison précise : le reste du code n'a pas le droit de NOMMER une
// distribution, pas même en écrivant le nom d'une classe. Le test no-distro-literals
// refuserait un « BatoceraAdapter » dans main.cpp — et il a raison, c'est exactement la
// dépendance que le §1 interdit.
//
// Ajouter une distribution (lot 9) revient à ajouter une ligne ICI, sans toucher au reste.
// C'est le point de contrôle du §13.

#include "platform/PlatformAdapter.h"

#include <QString>
#include <QStringList>
#include <memory>

namespace igiris::platform {

// Adaptateur correspondant à la distribution détectée sous `rootPrefix`.
// nullptr si aucune ne correspond — cas normal sur une machine de développement.
std::unique_ptr<PlatformAdapter> detectAdapter(const QString &rootPrefix = QStringLiteral("/"));

// Identifiants des distributions connues, pour les diagnostics.
QStringList knownAdapterIds();

} // namespace igiris::platform
