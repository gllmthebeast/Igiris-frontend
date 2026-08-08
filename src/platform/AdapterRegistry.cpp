#include "platform/AdapterRegistry.h"

#include "platform/BatoceraAdapter.h"

namespace igiris::platform {

std::unique_ptr<PlatformAdapter> detectAdapter(const QString &rootPrefix)
{
    // Ordre d'essai : du plus spécifique au plus général. Une seule entrée aujourd'hui ;
    // Recalbox s'ajoutera ici au lot 9, et nulle part ailleurs.
    if (BatoceraAdapter::detect(rootPrefix))
        return std::make_unique<BatoceraAdapter>(rootPrefix);

    return nullptr;
}

QStringList knownAdapterIds()
{
    return { QStringLiteral("batocera") };
}

} // namespace igiris::platform
