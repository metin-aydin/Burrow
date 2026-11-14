#include "app/Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>

namespace {
const char kAutoCheckKey[] = "updates/autoCheck";
const char kDefaultManifest[] = "https://github.com/metin-aydin/Burrow/releases/latest/download/latest.json";
constexpr int kFirstCheckMs = 15 * 1000;
constexpr int kIntervalMs = 6 * 60 * 60 * 1000;

QString currentVersion() { return QCoreApplication::applicationVersion(); }

// Runs a short command and returns its combined output, or a null string if it failed.
QString run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForFinished(120000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    const QString out = QString::fromUtf8(p.readAll());
    return out.isNull() ? QStringLiteral("") : out;
}

QString teamIdentifier(const QString &bundle)
{
    const QString out = run(QStringLiteral("/usr/bin/codesign"),
                            {QStringLiteral("-dv"), QStringLiteral("--verbose=2"), bundle});
    const auto m = QRegularExpression(QStringLiteral("TeamIdentifier=(\\S+)")).match(out);
    return m.hasMatch() && m.captured(1) != QLatin1String("not") ? m.captured(1) : QString();
}
} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    // BURROW_UPDATE_URL points the updater at a test manifest.
    const QString env = qEnvironmentVariable("BURROW_UPDATE_URL");
    m_manifestUrl = QUrl(env.isEmpty() ? QString::fromLatin1(kDefaultManifest) : env);

    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_timer.setInterval(kIntervalMs);
        if (autoCheck() && m_state != QLatin1String("downloading") && m_state != QLatin1String("ready")) {
            checkNow();
        }
    });
    // Development builds (0.0.0) never check on their own.
    if (supported() && currentVersion() != QLatin1String("0.0.0")) {
        m_timer.setInterval(kFirstCheckMs);
        m_timer.start();
    }
}

bool Updater::autoCheck() const
{
    return QSettings().value(kAutoCheckKey, true).toBool();
}

void Updater::setAutoCheck(bool on)
{
    QSettings().setValue(kAutoCheckKey, on);
    emit changed();
}

bool Updater::supported() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false; // Ubuntu (AppImage/deb) and Windows installers follow with their ports.
#endif
}

QString Updater::platformKey()
{
#if defined(Q_OS_MACOS)
    return QSysInfo::currentCpuArchitecture() == QLatin1String("arm64") ? QStringLiteral("mac-arm64")
                                                                        : QStringLiteral("mac-x64");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows-x64");
#else
    return QStringLiteral("linux-x64");
#endif
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = QString(a).remove(QLatin1Char('v')).split(QLatin1Char('.'));
    const QStringList pb = QString(b).remove(QLatin1Char('v')).split(QLatin1Char('.'));
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0;
        const int y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

void Updater::setState(const QString &s, const QString &error)
{
    m_state = s;
    m_error = error;
    emit changed();
}

void Updater::checkNow()
{
    if (m_state == QLatin1String("checking") || m_state == QLatin1String("downloading"))
        return;
    setState(QStringLiteral("checking"));
    QNetworkRequest req(m_manifestUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Burrow/%1").arg(currentVersion()));
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onManifest(reply); });
}

void Updater::onManifest(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        setState(QStringLiteral("error"), tr("Couldn't check for updates: %1").arg(reply->errorString()));
        return;
    }
    const QJsonObject m = QJsonDocument::fromJson(reply->readAll()).object();
    const QString version = m.value("version").toString();
    const QJsonObject asset = m.value("assets").toObject().value(platformKey()).toObject();
    if (version.isEmpty()) {
        setState(QStringLiteral("error"), tr("The update information is invalid."));
        return;
    }
    if (compareVersions(version, currentVersion()) <= 0 || asset.isEmpty()) {
        setState(QStringLiteral("upToDate"));
        return;
    }
    m_latest = version;
    m_notes = m.value("notes").toString();
    m_assetName = asset.value("name").toString();
    m_assetSha256 = asset.value("sha256").toString().toLower();
    setState(QStringLiteral("available"));
    emit updateAvailable(version);
}

void Updater::download()
{
    if (m_state != QLatin1String("available") && m_state != QLatin1String("error"))
        return;
    if (m_assetName.isEmpty() || m_assetSha256.size() != 64) {
        setState(QStringLiteral("error"), tr("The update information is invalid."));
        return;
    }
    // Assets sit next to the manifest: .../releases/download/vX.Y.Z/<name>.
    QUrl url = m_manifestUrl;
    const QString path = url.path();
    url.setPath(path.contains(QLatin1String("/latest/download/"))
                    ? QString(path).replace(QLatin1String("/latest/download/latest.json"),
                                            QStringLiteral("/download/v%1/%2").arg(m_latest, m_assetName))
                    : path.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/') + m_assetName);
    m_progress = 0;
    setState(QStringLiteral("downloading"));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? double(got) / double(total) : 0;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onDownloaded(reply); });
}

void Updater::onDownloaded(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        setState(QStringLiteral("error"), tr("Download failed: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray data = reply->readAll();
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != m_assetSha256.toLatin1()) {
        setState(QStringLiteral("error"), tr("The downloaded update failed verification and was discarded."));
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    m_downloadPath = QDir(dir).filePath(m_assetName);
    QFile f(m_downloadPath);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        setState(QStringLiteral("error"), tr("Couldn't save the update."));
        return;
    }
    f.close();
    m_progress = 1;
    setState(QStringLiteral("ready"));
}

void Updater::install()
{
    if (m_state != QLatin1String("ready"))
        return;
    setState(QStringLiteral("installing"));
#ifdef Q_OS_MACOS
    const QString err = installMac();
    if (!err.isEmpty()) {
        setState(QStringLiteral("error"), err);
        return;
    }
    QCoreApplication::quit();
#endif
}

QString Updater::installMac()
{
    // .../Burrow.app/Contents/MacOS/Burrow -> .../Burrow.app
    QDir bundleDir(QCoreApplication::applicationDirPath());
    bundleDir.cdUp();
    bundleDir.cdUp();
    const QString current = bundleDir.absolutePath();
    if (!current.endsWith(QLatin1String(".app")))
        return tr("Burrow isn't running from an app bundle, so it can't update itself.");

    const QString currentTeam = teamIdentifier(current);
    if (currentTeam.isEmpty())
        return tr("This copy of Burrow isn't signed, so updates can't be verified. Download the new version from GitHub.");

    const QString staging = QDir(QFileInfo(m_downloadPath).absolutePath()).filePath(QStringLiteral("staging"));
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    if (run(QStringLiteral("/usr/bin/ditto"), {QStringLiteral("-x"), QStringLiteral("-k"), m_downloadPath, staging}).isNull())
        return tr("Couldn't unpack the update.");
    const QString fresh = QDir(staging).filePath(QStringLiteral("Burrow.app"));
    if (run(QStringLiteral("/usr/bin/codesign"),
            {QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), fresh}).isNull()
        || teamIdentifier(fresh) != currentTeam)
        return tr("The update's signature doesn't match this app; it was not installed.");

    // A detached helper waits for Burrow to exit, swaps the bundles and relaunches.
    const QString script = QDir(staging).filePath(QStringLiteral("install.sh"));
    QFile s(script);
    if (!s.open(QIODevice::WriteOnly))
        return tr("Couldn't prepare the update.");
    s.write(QStringLiteral(
        "#!/bin/sh\n"
        "while kill -0 %1 2>/dev/null; do sleep 0.3; done\n"
        "old=\"$2.old\"\n"
        "rm -rf \"$old\"\n"
        "mv \"$2\" \"$old\" && mv \"$1\" \"$2\" && rm -rf \"$old\" || { [ -d \"$old\" ] && mv \"$old\" \"$2\"; }\n"
        "open \"$2\"\n")
                .arg(QCoreApplication::applicationPid()).toUtf8());
    s.close();
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {script, fresh, current}))
        return tr("Couldn't start the update.");
    return {};
}
