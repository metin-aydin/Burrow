#include "app/LogFile.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>

#include <windows.h>

namespace {
QFile *g_log = nullptr;
QMutex g_lock;

void writeLine(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
    QMutexLocker lock(&g_lock);
    if (g_log) {
        g_log->write(QStringLiteral("%1 %2: %3\n")
                         .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                              QLatin1String(kinds[qBound(0, int(type), 4)]), msg)
                         .toUtf8());
        g_log->flush();
    }
}
} // namespace

void LogFile::installIfNoConsole()
{
    // Started from a console, or with stderr redirected (CI): leave the messages there.
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (GetConsoleWindow() || (err && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN))
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Burrow");
    QDir().mkpath(dir);
    g_log = new QFile(dir + QStringLiteral("/burrow.log"));
    if (!g_log->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        delete g_log;
        g_log = nullptr;
        return;
    }
    qInstallMessageHandler(writeLine);
}
#else
void LogFile::installIfNoConsole() {}
#endif
