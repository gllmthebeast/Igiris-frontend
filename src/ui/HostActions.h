#pragma once

// Les actions qui s'adressent à l'HÔTE et non au catalogue — CLAUDE.md §1, responsabilité 6.
//
// Il n'y en a qu'une aujourd'hui, et elle existe pour une raison qui n'a rien d'accessoire :
// en remplaçant l'écran d'accueil de la distribution hôte, ce frontend remplace aussi sa
// SEULE interface de configuration. Sur les distributions visées, les manettes, le wifi, le
// bluetooth, l'audio, la résolution et la mise à jour du système vivent tous dans l'écran
// d'accueil d'origine. Le masquer sans rien proposer rendrait la machine inconfigurable.
//
// On ne réimplémente rien — le §0 interdit les addons, le §12 interdit de forker ES, et
// refaire le seul mappage de manettes serait des mois pour un résultat inférieur. On rend
// l'écran, et on le reprend.
//
// ⚠️ Aucune chaîne spécifique à une distribution ici : le nom du binaire, sa localisation
// et le libellé viennent tous de l'adaptateur (§1, règle vérifiée en CI).

#include <QObject>
#include <QString>

namespace igiris::platform {
class PlatformAdapter;
}

namespace igiris::ui {

class HostActions : public QObject
{
    Q_OBJECT

    // Faux quand l'adaptateur ne déclare pas la capacité — distribution inconnue, ou
    // binaire de réglages absent. L'interface montre alors une entrée GRISÉE qui dit
    // pourquoi, plutôt qu'un bouton qui ne ferait rien (§1).
    Q_PROPERTY(bool settingsAvailable READ settingsAvailable NOTIFY adapterChanged)
    // « Paramètres <hôte> » — le nom de la distribution vient de l'adaptateur, pas d'ici.
    Q_PROPERTY(QString settingsLabel READ settingsLabel NOTIFY adapterChanged)
    // Pourquoi c'est indisponible, en clair. Vide quand ça l'est.
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY adapterChanged)
    // Comment REVENIR ici depuis les réglages, dans les mots de l'hôte. Affiché au moment
    // où on lui rend l'écran — c'est le seul instant où ça sert.
    Q_PROPERTY(QString returnHint READ returnHint NOTIFY adapterChanged)

public:
    explicit HostActions(QObject *parent = nullptr);

    void setAdapter(const platform::PlatformAdapter *adapter);

    bool    settingsAvailable() const;
    QString settingsLabel() const;
    QString unavailableReason() const;
    QString returnHint() const;

    // Rend l'écran à l'interface de réglages de l'hôte. Retourne un message d'erreur
    // COMPLET, vide en cas de succès (§15) — même convention que le lancement d'un jeu.
    Q_INVOKABLE QString openSettings();

signals:
    void adapterChanged();

private:
    const platform::PlatformAdapter *m_adapter = nullptr;
};

} // namespace igiris::ui
