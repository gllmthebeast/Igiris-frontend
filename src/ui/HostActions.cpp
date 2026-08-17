#include "ui/HostActions.h"

#include "platform/PlatformAdapter.h"

namespace igiris::ui {

HostActions::HostActions(QObject *parent)
    : QObject(parent)
{
}

void HostActions::setAdapter(const platform::PlatformAdapter *adapter)
{
    m_adapter = adapter;
    emit adapterChanged();
}

bool HostActions::settingsAvailable() const
{
    return m_adapter && m_adapter->supports(platform::Capability::HostSettings);
}

QString HostActions::settingsLabel() const
{
    // Un libellé de repli, pour que l'entrée reste NOMMÉE même grisée : une case vide et
    // inerte ne dit rien à qui la regarde.
    if (!m_adapter || m_adapter->hostSettingsLabel().isEmpty())
        return tr("Paramètres du système");
    return m_adapter->hostSettingsLabel();
}

QString HostActions::unavailableReason() const
{
    if (settingsAvailable())
        return {};
    if (!m_adapter)
        return tr("aucune distribution reconnue");
    return tr("interface de réglages introuvable");
}

QString HostActions::returnHint() const
{
    return m_adapter ? m_adapter->hostSettingsReturnHint() : QString();
}

QString HostActions::openSettings()
{
    if (!m_adapter)
        return tr("aucune distribution reconnue : réglages inaccessibles");

    QString error;
    if (m_adapter->openHostSettings(&error))
        return {};

    // Verbatim (§15) : c'est le seul message que l'utilisateur verra, et il est déjà
    // devant une machine dont il ne peut plus rien configurer.
    return error;
}

} // namespace igiris::ui
