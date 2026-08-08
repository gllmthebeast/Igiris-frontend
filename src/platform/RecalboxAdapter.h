#pragma once

// Implémentation Recalbox — cible de validation (§1), et surtout TEST DE CONCEPTION (§13).
//
// C'est ici, et nulle part ailleurs, que vivent les chemins « /recalbox/… » et la lecture
// du format systemlist.xml.

#include "platform/PlatformAdapter.h"

namespace igiris::platform {

class RecalboxAdapter final : public PlatformAdapter
{
public:
    explicit RecalboxAdapter(QString rootPrefix = QStringLiteral("/"));

    static bool detect(const QString &rootPrefix);

    QString      id() const override;
    QString      displayName() const override;
    Capabilities capabilities() const override;

    QList<SystemEntry> readSystems(QString *error) const override;
    QString            systemsFilePath() const override;

    QString resolvePlatformKey(const QString &platformKey,
                               const QStringList &localSystems) const override;

    QStringList romDirectories() const override;

    LaunchCommand buildLaunchCommand(const SystemEntry &system, const QString &romPath,
                                     const LaunchDetails &details) const override;

    bool launch(const SystemEntry &system, const QString &romPath,
                const LaunchDetails &details, QString *error) const override;

private:
    QString absolutePath(const QString &relative) const;

    QString m_root;
};

} // namespace igiris::platform
