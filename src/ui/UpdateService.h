#pragma once

// Mise à jour des données, depuis l'interface — CLAUDE.md §14.
//
// Pourquoi ça existe : jusqu'ici tout passait par `tools/fetch-export.sh`, lancé depuis un
// TERMINAL. Sur une borne branchée à une télévision, sans clavier, ça n'existe pas —
// autrement dit personne n'aurait jamais rien mis à jour, et la cadence de versions
// n'aurait servi à rien.
//
// ⚠️ Ce service NE RÉIMPLÉMENTE PAS le téléchargement. Il PILOTE le script, qui porte déjà
// la seule chose qui compte : vérifier l'empreinte AVANT de remplacer quoi que ce soit, et
// basculer d'un seul `mv`. Dupliquer cette logique en C++ aurait créé deux vérités, dont
// une non éprouvée.
//
// C'est aussi ce qui évite d'ajouter Qt6Network au binaire : le §12 demande de peser chaque
// module Qt, puisqu'il doit être cross-compilé dans les images Buildroot des distributions
// cibles. `curl` et `python3` sont déjà là — l'outil de lancement de l'hôte est en Python.

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace igiris::ui {

class UpdateService : public QObject
{
    Q_OBJECT

    // La PASTILLE : vrai dès qu'une mise à jour est disponible. Une seule, globale — le §0
    // veut une interface minimale, pas un centre de notifications.
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    // Vrai pendant le travail. L'interface reste utilisable : le téléchargement est en
    // ARRIÈRE-PLAN, on continue de parcourir sa liste pendant ce temps.
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    // 0..1, ou -1 tant que la taille totale est inconnue. Le compteur est INDICATIF : il
    // suit la taille du fichier en cours d'écriture, pas un protocole de progression.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    // « 12,4 / 26,1 Mo » — lisible à trois mètres, ce qu'un pourcentage seul n'est pas.
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    // État en clair, verbatim en cas d'échec (§15). C'est le seul endroit où l'utilisateur
    // peut comprendre ce qui s'est passé.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    // Faux sans réseau : l'interface le DIT au lieu de tenter et d'échouer. L'appareil
    // fonctionne parfaitement hors ligne — le catalogue est entièrement local (§11).
    Q_PROPERTY(bool online READ online NOTIFY stateChanged)

public:
    explicit UpdateService(QObject *parent = nullptr);

    // Où trouver le script et où déposer l'export. Les deux viennent du point d'entrée :
    // ce service ne devine aucun chemin, et surtout aucun chemin de distribution (§1).
    void setPaths(const QString &scriptPath, const QString &dataDir);
    void setCoversScript(const QString &scriptPath);

    bool    updateAvailable() const { return m_available; }
    bool    busy() const { return m_process.state() != QProcess::NotRunning; }
    qreal   progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString status() const { return m_status; }
    bool    online() const { return m_online; }

    // Regarde s'il y a du nouveau, sans rien télécharger d'autre que le manifeste.
    Q_INVOKABLE void check();

    // Télécharge et bascule. Rien n'est remplacé si l'empreinte ne concorde pas.
    Q_INVOKABLE void update();

    // Constitue le cache LOCAL de vignettes, depuis IGDB, en arrière-plan.
    //
    // ⚠️ Rien n'est redistribué : l'appareil télécharge depuis la source que le frontend
    // interroge déjà à chaque affichage. Il le fait une fois et garde le résultat, au lieu
    // de refaire la requête à chaque défilement. C'est ce qui lève le point bloquant du
    // §11 — héberger un pack nous-mêmes ferait de nous un distributeur.
    Q_INVOKABLE void downloadCovers();

signals:
    void stateChanged();
    void progressChanged();
    // Émis après une bascule réussie : le catalogue sur le disque a changé, tout ce qui en
    // dépend doit être rechargé.
    void catalogueReplaced();
    // Le cache de vignettes a changé : la liste doit relire ce qui est disponible.
    void coversReady();

private:
    void run(bool downloadIfNeeded);
    void countCovers();
    void readOutput();
    void pollProgress();
    void finished(int code, QProcess::ExitStatus status);

    QProcess m_process;
    QTimer   m_poll;

    QString m_scriptPath;
    QString m_dataDir;
    QString m_stageDir;
    QString m_stageFile;

    bool    m_available = false;
    bool    m_online    = true;
    bool    m_wantDownload = false;
    qreal   m_progress = -1;
    qint64  m_total    = 0;
    QString m_progressText;
    QString m_status;
    QString m_remoteVersion;
    QString m_coversScript;
    QString m_coversDir;
    bool    m_coversJob = false;
};

} // namespace igiris::ui
