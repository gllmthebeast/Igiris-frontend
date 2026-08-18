#include "ui/UpdateService.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace igiris::ui {

namespace {

// Plafond de débit passé à curl. Une borne qui télécharge 26 Mo ne doit pas prendre toute
// la connexion : sur une box partagée, une mise à jour qui coupe le reste de la maison est
// une mauvaise surprise, et le §0 veut une application qui se fasse oublier.
constexpr auto kRateLimit = "2M";

QString humanSize(qint64 bytes)
{
    return QLocale::system().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

UpdateService::UpdateService(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &UpdateService::readOutput);
    connect(&m_process, &QProcess::finished, this, &UpdateService::finished);

    // La progression se LIT sur le fichier qui grossit, elle ne se reçoit pas. curl n'émet
    // rien d'analysable en mode silencieux, et le rendre bavard produirait des retours
    // chariot à parser — plus fragile que de regarder une taille.
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, &UpdateService::pollProgress);
}

void UpdateService::setPaths(const QString &scriptPath, const QString &dataDir)
{
    m_scriptPath = scriptPath;
    m_dataDir    = dataDir;
    // À CÔTÉ des données, jamais dans /tmp : sur l'appareil, /tmp est en RAM et 26 Mo y
    // pèsent lourd. Et la bascule finale doit rester un `mv` sur le MÊME système de
    // fichiers, sinon elle cesse d'être atomique.
    m_stageDir = QDir(dataDir).filePath(QStringLiteral(".maj"));
    emit stateChanged();
}

void UpdateService::setCoversScript(const QString &scriptPath)
{
    m_coversScript = scriptPath;
    emit stateChanged();
}

void UpdateService::downloadCovers()
{
    if (busy() || m_coversScript.isEmpty() || !QFileInfo::exists(m_coversScript))
        return;

    m_coversJob = true;
    m_progress  = -1;
    m_total     = 0;
    m_coversDir = QDir(m_dataDir).filePath(QStringLiteral("covers"));
    m_status    = tr("téléchargement des vignettes…");

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("IGIRIS_MACHINE"), QStringLiteral("1"));
    env.insert(QStringLiteral("IGIRIS_LIMIT_RATE"), QLatin1String(kRateLimit));
    m_process.setProcessEnvironment(env);
    m_process.setProgram(QStringLiteral("bash"));
    m_process.setArguments({ m_coversScript, m_dataDir });
    m_process.start();

    m_poll.start();
    emit stateChanged();
    emit progressChanged();
}

void UpdateService::countCovers()
{
    // La progression se compte sur les FICHIERS PRÉSENTS et non sur un compteur de boucle :
    // le script travaille à quatre tâches en parallèle, et cette mesure reste juste. Elle
    // survit aussi à une interruption, puisqu'elle repart du disque.
    if (m_coversDir.isEmpty() || m_total <= 0)
        return;
    const int got = QDir(m_coversDir).entryList({ QStringLiteral("*.jpg") }, QDir::Files).size();
    m_progress     = qBound(0.0, static_cast<qreal>(got) / m_total, 1.0);
    m_progressText = QStringLiteral("%1 / %2").arg(got).arg(m_total);
    emit progressChanged();
}

void UpdateService::check()
{
    run(false);
}

void UpdateService::update()
{
    run(true);
}

void UpdateService::run(bool downloadIfNeeded)
{
    if (busy())
        return;
    if (m_scriptPath.isEmpty() || !QFileInfo::exists(m_scriptPath)) {
        m_status = tr("outil de mise à jour introuvable : %1").arg(m_scriptPath);
        emit stateChanged();
        return;
    }

    m_coversJob    = false;
    m_wantDownload = downloadIfNeeded;
    m_progress     = -1;
    m_total        = 0;
    m_progressText.clear();
    m_stageFile.clear();
    m_status = downloadIfNeeded ? tr("mise à jour…") : tr("vérification…");

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("IGIRIS_MACHINE"), QStringLiteral("1"));
    env.insert(QStringLiteral("IGIRIS_STAGE_DIR"), m_stageDir);
    env.insert(QStringLiteral("IGIRIS_LIMIT_RATE"), QLatin1String(kRateLimit));
    m_process.setProcessEnvironment(env);

    // Le script décide seul s'il y a quelque chose à prendre : il compare les empreintes.
    // Une VÉRIFICATION est donc une mise à jour qu'on interrompt avant le téléchargement —
    // c'est ce qui garantit que « vérifier » et « installer » ne peuvent pas diverger.
    m_process.setProgram(QStringLiteral("bash"));
    m_process.setArguments({ m_scriptPath, m_dataDir });
    m_process.start();

    emit stateChanged();
    emit progressChanged();
}

void UpdateService::readOutput()
{
    while (m_process.canReadLine()) {
        const QString line = QString::fromUtf8(m_process.readLine()).trimmed();
        if (!line.startsWith(QLatin1String("igiris:")))
            continue;

        const QString payload = line.mid(7);
        const int     eq      = payload.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key   = payload.left(eq);
        const QString value = payload.mid(eq + 1);

        // Les deux travaux partagent le même canal mais PAS la même logique : celle du
        // catalogue interrompt le processus dès qu'elle sait qu'il y a du neuf, ce qui
        // arrêterait net le téléchargement des vignettes. Elles sont donc séparées ici,
        // avant toute interprétation.
        if (m_coversJob) {
            if (key == QLatin1String("total"))
                m_total = value.toLongLong();
            else if (key == QLatin1String("status") && value == QLatin1String("nodb"))
                m_status = tr("catalogue absent : rien à illustrer");
            continue;
        }

        if (key == QLatin1String("version")) {
            m_remoteVersion = value;
        } else if (key == QLatin1String("total")) {
            m_total = value.toLongLong();
        } else if (key == QLatin1String("file")) {
            m_stageFile = value;
        } else if (key == QLatin1String("status")) {
            if (value == QLatin1String("uptodate")) {
                m_available = false;
                m_status    = tr("catalogue à jour (%1)").arg(m_remoteVersion);
            } else if (value == QLatin1String("downloading")) {
                // Il y a du nouveau. En simple vérification, on s'arrête ICI : le manifeste
                // a suffi à répondre, et rien ne justifie de tirer 26 Mo sans qu'on le
                // demande.
                m_available = true;
                if (!m_wantDownload) {
                    m_status = tr("catalogue %1 disponible").arg(m_remoteVersion);
                    m_process.terminate();
                } else {
                    m_status = tr("téléchargement du catalogue %1…").arg(m_remoteVersion);
                    m_poll.start();
                }
            } else if (value == QLatin1String("badhash")) {
                // L'ancien export est intact — le script ne bascule qu'après vérification.
                m_status = tr("empreinte incorrecte : l'ancien catalogue est conservé");
            } else if (value == QLatin1String("ok")) {
                m_available = false;
                m_status    = tr("catalogue mis à jour (%1)").arg(m_remoteVersion);
            }
            emit stateChanged();
        }
    }
}

void UpdateService::pollProgress()
{
    if (m_coversJob) {
        countCovers();
        return;
    }
    if (m_stageFile.isEmpty() || m_total <= 0)
        return;

    const qint64 got = QFileInfo(m_stageFile).size();
    m_progress       = qBound(0.0, static_cast<qreal>(got) / m_total, 1.0);
    m_progressText   = QStringLiteral("%1 / %2").arg(humanSize(got), humanSize(m_total));
    emit progressChanged();
}

void UpdateService::finished(int code, QProcess::ExitStatus status)
{
    m_poll.stop();
    QDir(m_stageDir).removeRecursively();

    const bool interrompu = !m_wantDownload && m_available;

    if (status == QProcess::CrashExit && !interrompu) {
        m_status = tr("mise à jour interrompue");
    } else if (code != 0 && !interrompu && m_status.isEmpty()) {
        // Sans réseau, curl échoue et le script s'arrête. On le DIT plutôt que de laisser
        // un bouton sans effet : l'appareil marche très bien hors ligne, ce n'est pas une
        // panne (§11).
        m_online = false;
        m_status = tr("hors ligne — le catalogue local reste utilisable");
    }

    if (m_coversJob) {
        // Le décompte final vient du disque, pas du script : c'est la même mesure que
        // pendant le travail, donc elle ne peut pas la contredire.
        countCovers();
        const int got = QDir(m_coversDir).entryList({ QStringLiteral("*.jpg") },
                                                    QDir::Files).size();
        m_status = code == 0 ? tr("%1 vignettes disponibles hors ligne").arg(got)
                             : tr("vignettes : %1 récupérées, reprise possible").arg(got);
        m_coversJob = false;
        emit coversReady();
    } else if (code == 0 && m_wantDownload && !m_available) {
        emit catalogueReplaced();
    }

    m_progress = -1;
    m_progressText.clear();
    emit progressChanged();
    emit stateChanged();
}

} // namespace igiris::ui
