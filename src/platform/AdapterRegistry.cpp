#include "platform/AdapterRegistry.h"

#include "platform/BatoceraAdapter.h"
#include "platform/RecalboxAdapter.h"

namespace igiris::platform {

std::unique_ptr<PlatformAdapter> detectAdapter(const QString &rootPrefix)
{
    // Ordre d'essai : du plus spécifique au plus général. Une seule entrée aujourd'hui ;
    // Recalbox s'ajoutera ici au lot 9, et nulle part ailleurs.
    if (BatoceraAdapter::detect(rootPrefix))
        return std::make_unique<BatoceraAdapter>(rootPrefix);
    if (RecalboxAdapter::detect(rootPrefix))
        return std::make_unique<RecalboxAdapter>(rootPrefix);

    return nullptr;
}

std::unique_ptr<PlatformAdapter> adapterById(const QString &id, const QString &rootPrefix)
{
    if (id == QLatin1String("batocera"))
        return std::make_unique<BatoceraAdapter>(rootPrefix);
    if (id == QLatin1String("recalbox"))
        return std::make_unique<RecalboxAdapter>(rootPrefix);
    return nullptr;
}

QStringList knownAdapterIds()
{
    return { QStringLiteral("batocera"), QStringLiteral("recalbox") };
}

} // namespace igiris::platform
