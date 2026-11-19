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
#include <QTimer>

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

void AgentSetup::showCode()
{
    run(QStringLiteral("code"));
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
    if (m_code.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_code);
    // A pairing code shouldn't stay on the clipboard: cleared after a minute, unless something else was
    // copied meanwhile.
    const QString copied = m_code;
    QTimer::singleShot(60 * 1000, QGuiApplication::clipboard(), [copied] {
        if (QGuiApplication::clipboard()->text() == copied)
            QGuiApplication::clipboard()->clear();
    });
}

void AgentSetup::run(const QString &command)
{
    if (!m_available || m_busy)
        return;
    m_error.clear();
    if (m_demo) {
        // Pretend, so the screens can be seen on a Mac: a fixed, harmless code (the shared test vector).
        if (command == QLatin1String("status")) {
            m_installed = m_running = false;
            m_code.clear();
        } else {
            m_installed = m_running = command != QLatin1String("uninstall");
            m_code = m_installed ? QStringLiteral("AAAQ-EAYE-AUDA-OCAJ-BIFQ-YDIO-B4IB-CEQT-CQKR-MFYY-DENB-WHA5-DYPQ") : QString();
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
            m_running = false;
            m_code.clear();
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
    QJsonParseError parse{};
    const QJsonObject o = QJsonDocument::fromJson(json, &parse).object();
    // Anything but a clear answer forgets what was shown (code, running): never a stale "running" or code.
    if (parse.error != QJsonParseError::NoError || o.contains(QLatin1String("error"))
        || !o.value("installed").isBool()) {
        m_error = o.value("error").toString(tr("The helper gave an answer Burrow doesn't understand."));
        m_running = false;
        m_code.clear();
        return;
    }
    m_installed = o.value("installed").toBool();
    m_running = o.value("running").toBool();
    // status never carries the code; install, pair and code do. Keep a code already shown otherwise.
    if (!m_installed)
        m_code.clear();
    else if (o.contains(QLatin1String("code")))
        m_code = o.value("code").toString();
    m_port = o.value("port").toInt(m_port);
}
