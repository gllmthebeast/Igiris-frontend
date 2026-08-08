#pragma once

// Implémentation Batocera de l'adaptateur — cible de référence (§1).
//
// C'est ici, et nulle part ailleurs, que vivent les chemins « /userdata/… ». Le test
// no-distro-literals refuse ces chaînes partout hors de src/platform/.

#include "platform/PlatformAdapter.h"

namespace igiris::platform {

class BatoceraAdapter final : public PlatformAdapter
{
public:
    // `rootPrefix` préfixe tous les chemins absolus. « / » sur un vrai appareil ; un
    // répertoire temporaire dans les tests, ou le point de montage d'une image.
    // Sans ce paramètre, l'adaptateur ne serait testable que sur la machine cible.
    explicit BatoceraAdapter(QString rootPrefix = QStringLiteral("/"));

    // Reconnaît-on une installation Batocera sous cette racine ?
    static bool detect(const QString &rootPrefix);

    QString      id() const override;
    QString      displayName() const override;
    Capabilities capabilities() const override;

    QString systemsFilePath() const override;

    QString resolvePlatformKey(const QString &platformKey,
                               const QStringList &localSystems) const override;

    QStringList romDirectories() const override;

    LaunchCommand buildLaunchCommand(const SystemEntry &system,
                                     const QString &romPath) const override;

    bool launch(const SystemEntry &system, const QString &romPath,
                QString *error) const override;

private:
    QString absolutePath(const QString &relative) const;

    QString m_root;
};

} // namespace igiris::platform
