#include "app/AgentSetup.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>

namespace {
const char kDismissed[] = "agent/suggestionDismissed";
}

AgentSetup::AgentSetup(QObject *parent)
    : QObject(parent)
{
    m_demo = QCoreApplication::arguments().contains(QStringLiteral("--seed-demo"))
             && qEnvironmentVariableIntValue("BURROW_DEMO_AGENT");
#if defined(Q_OS_LINUX)
    // Next to Burrow in its package: AppImage usr/bin, .deb /opt/burrow/bin.
    m_binary = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("burrow-agent"));
    // GNOME's Desktop Sharing is what the agent prepares; other desktops have nothing for it to unlock.
    m_available = QFile::exists(m_binary)
                  && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("GNOME"), Qt::CaseInsensitive);
#endif
    if (m_demo)
        m_available = true;
    refresh();
}

bool AgentSetup::suggest() const
{
    return m_available && !m_installed && !m_busy && !QSettings().value(kDismissed, false).toBool();
}

void AgentSetup::refresh()
{
    if (m_available)
        run(QStringLiteral("status"));
}

void AgentSetup::install()
{
    run(QStringLiteral("install"));
}

void AgentSetup::newCode()
{
    run(QStringLiteral("pair"));
}

void AgentSetup::uninstall()
{
    run(QStringLiteral("uninstall"));
}

void AgentSetup::dismiss()
{
    QSettings().setValue(kDismissed, true);
    emit changed();
}

void AgentSetup::copyCode() const
{
    if (!m_code.isEmpty())
        QGuiApplication::clipboard()->setText(m_code);
}

void AgentSetup::run(const QString &command)
{
    if (!m_available || m_busy)
        return;
    m_error.clear();
    if (m_demo) {
        // Pretend, so the screens can be seen on a Mac: a fixed, harmless code (the shared test vector).
        m_installed = command != QLatin1String("uninstall");
        m_running = m_installed;
        m_code = m_installed ? QStringLiteral("AAAQ-EAYE-AUDA-OCAJ-BIFQ-YDIO-B4IB-CEQT-CQKR-MFYY-DENB-WHA5-DYPQ") : QString();
        if (command == QLatin1String("status")) {
            m_installed = m_running = false;
            m_code.clear();
        }
        emit changed();
        return;
    }
    m_busy = true;
    emit changed();
    auto *p = new QProcess(this);
    p->setProgram(m_binary);
    p->setArguments({command, QStringLiteral("--json")});
    connect(p, &QProcess::finished, this, [this, p, command](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        m_busy = false;
        const QByteArray out = p->readAllStandardOutput();
        if (status != QProcess::NormalExit || out.trimmed().isEmpty()) {
            m_error = tr("The helper couldn't be set up: %1").arg(QString::fromLocal8Bit(p->readAllStandardError()).trimmed());
            emit changed();
            return;
        }
        read(out);
        if (code != 0 && m_error.isEmpty())
            m_error = tr("The helper couldn't be set up.");
        emit changed();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        m_busy = false;
        m_error = tr("The helper couldn't be started: %1").arg(p->errorString());
        emit changed();
    });
    p->start();
}

void AgentSetup::read(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.contains(QLatin1String("error"))) {
        m_error = o.value("error").toString();
        return;
    }
    m_installed = o.value("installed").toBool();
    m_running = o.value("running").toBool();
    m_code = m_installed ? o.value("code").toString() : QString();
    m_port = o.value("port").toInt(m_port);
}
