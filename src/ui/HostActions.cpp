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
    // Quand l'hôte tourne déjà, le bouton reste utile — il nous ferme — même si le binaire
    // de réglages était introuvable. C'est le cas d'un lancement en « port ».
    if (returnsToHost())
        return true;
    return m_adapter && m_adapter->supports(platform::Capability::HostSettings);
}

bool HostActions::returnsToHost() const
{
    return m_adapter && m_adapter->hostFrontendIsRunning();
}

QString HostActions::settingsLabel() const
{
    // Lancé DEPUIS l'écran d'accueil de l'hôte : le bouton nous ferme, il n'ouvre rien.
    // Le libellé le dit, sinon l'utilisateur appuierait en croyant ouvrir les réglages.
    if (returnsToHost() && m_adapter && !m_adapter->hostReturnLabel().isEmpty())
        return m_adapter->hostReturnLabel();

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
