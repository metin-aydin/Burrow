#include "app/Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
#include <QUrl>

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
    // BURROW_UPDATE_URL points the updater at another manifest, in builds made for the unit tests only
    // (BURROW_UPDATER_ALLOW_INSECURE, file:// fixtures). A release build always reads GitHub: where the
    // packages are checked by SHA-256 alone (Linux, unsigned Windows), whoever could set the variable
    // would otherwise choose what gets installed.
    m_manifestUrl = QUrl(QString::fromLatin1(kDefaultManifest));
#ifdef BURROW_UPDATER_ALLOW_INSECURE
    const QUrl env(qEnvironmentVariable("BURROW_UPDATE_URL"));
    if (!env.isEmpty() && env.isValid())
        m_manifestUrl = env;
#endif

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
#if defined(Q_OS_MACOS)
    return true;
#elif defined(Q_OS_WIN)
    // Installed copies only (the installer leaves its uninstaller next to Burrow.exe); the portable zip
    // doesn't update itself, as with JumpDesk.
    return QFile::exists(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("uninstall.exe")));
#else
    // An AppImage replaces itself (APPIMAGE is the running file); a .deb install is upgraded through apt.
    return appImagePath().size() > 0 || debInstalled();
#endif
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
QString Updater::appImagePath()
{
    const QString p = qEnvironmentVariable("APPIMAGE");
    return p.endsWith(QLatin1String(".AppImage")) && QFileInfo(p).isFile() ? p : QString();
}

bool Updater::debInstalled()
{
    // The package installs to /opt/burrow (tools/package-deb.sh) and dpkg knows it.
    return QCoreApplication::applicationDirPath() == QLatin1String("/opt/burrow/bin")
        && QFile::exists(QStringLiteral("/var/lib/dpkg/info/burrow.list"));
}
#endif

QString Updater::assetName(const QString &version)
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Burrow-%1-%2-setup.exe").arg(version, platformKey());
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Burrow-%1-%2.zip").arg(version, platformKey());
#else
    if (platformKey() == QLatin1String("linux-x64-deb"))
        return QStringLiteral("burrow_%1_amd64.deb").arg(version);
    return QStringLiteral("Burrow-%1-%2.AppImage").arg(version, platformKey());
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
    // The manifest lists the AppImage and the .deb separately: each copy updates in its own format.
    return appImagePath().isEmpty() && debInstalled() ? QStringLiteral("linux-x64-deb") : QStringLiteral("linux-x64");
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
    static const QRegularExpression versionRe(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
    if (!versionRe.match(version).hasMatch()) {
        setState(QStringLiteral("error"), tr("The update information is invalid."));
        return;
    }
    if (compareVersions(version, currentVersion()) <= 0 || asset.isEmpty()) {
        setState(QStringLiteral("upToDate"));
        return;
    }
    m_latest = version;
    m_notes = m.value("notes").toString();
    // The file name is never taken from the manifest as a path: it must be exactly the name the release
    // workflow produces for this version and platform.
    const QString expected = assetName(version);
    if (asset.value("name").toString() != expected) {
        setState(QStringLiteral("error"), tr("The update information is invalid."));
        return;
    }
    m_assetName = expected;
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
#elif defined(Q_OS_WIN)
    const QString err = installWindows();
    if (!err.isEmpty()) {
        setState(QStringLiteral("error"), err);
        return;
    }
    QCoreApplication::quit();
#else
    const QString err = installLinux();
    if (!err.isEmpty()) {
        setState(QStringLiteral("error"), err);
        return;
    }
    QCoreApplication::quit();
#endif
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
// Linux packages aren't code-signed: the SHA-256 from the release manifest (fetched over HTTPS from GitHub)
// is the check, made in onDownloaded before this runs.
QString Updater::installLinux()
{
    // The verified bytes again, from the download: whatever is installed is checked right before use.
    QFile in(m_downloadPath);
    if (!in.open(QIODevice::ReadOnly))
        return tr("Couldn't prepare the update.");
    const QByteArray data = in.readAll();
    in.close();
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != m_assetSha256.toLatin1())
        return tr("The downloaded update failed verification and was discarded.");

    const QString appImage = appImagePath();
    if (!appImage.isEmpty()) {
        // QSaveFile writes a randomly named file next to the running AppImage (created exclusively, never
        // through an existing file or link) and renames it over the AppImage only once complete; the
        // running copy keeps working from its open file. A folder other users can write to is refused.
        const QFileInfo dir(QFileInfo(appImage).absolutePath());
        if (!dir.isWritable())
            return tr("Burrow can't write to %1. Download the new version from GitHub.").arg(dir.absoluteFilePath());
        if (dir.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther))
            return tr("Other users can change files in %1, so Burrow doesn't update itself there. Download the new version from GitHub.")
                .arg(dir.absoluteFilePath());
        QSaveFile out(appImage);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
            return tr("Couldn't save the update.");
        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                           | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
        if (!out.commit())
            return tr("Couldn't replace Burrow with the new version.");
        QFile::remove(m_downloadPath);
        if (!QProcess::startDetached(appImage, {}))
            return tr("The update is installed; start Burrow again.");
        return {};
    }
    if (debInstalled()) {
        // Staged in a private (0700, random name) folder. The helper waits for Burrow to quit, then asks for
        // the administrator password (pkexec); as root it copies the package where only root can write,
        // checks its SHA-256 there and only then installs it, so nothing can swap the file in between.
        QTemporaryDir staging(QDir::tempPath() + QStringLiteral("/burrow-update-XXXXXX"));
        if (!staging.isValid())
            return tr("Couldn't prepare the update.");
        staging.setAutoRemove(false);
        QFile::setPermissions(staging.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString deb = QDir(staging.path()).filePath(QStringLiteral("burrow.deb"));
        QFile d(deb);
        if (!d.open(QIODevice::WriteOnly | QIODevice::NewOnly) || d.write(data) != data.size()) {
            QDir(staging.path()).removeRecursively();
            return tr("Couldn't save the update.");
        }
        d.close();
        QFile::remove(m_downloadPath);
        const QString script = QDir(staging.path()).filePath(QStringLiteral("install.sh"));
        QFile s(script);
        if (!s.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            QDir(staging.path()).removeRecursively();
            return tr("Couldn't prepare the update.");
        }
        s.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        s.write(QStringLiteral(
            "#!/bin/sh\n"
            "# $1 = staged .deb, $2 = its SHA-256, $3 = staging folder\n"
            "while kill -0 %1 2>/dev/null; do sleep 0.3; done\n"
            "pkexec /bin/sh -c 'set -e; d=$(mktemp -d); trap \"rm -rf \\\"${d:?}\\\"\" EXIT; "
            "cp -- \"$1\" \"$d/burrow.deb\"; echo \"$2  $d/burrow.deb\" | sha256sum -c --quiet -; "
            "apt-get install -y \"$d/burrow.deb\"' sh \"$1\" \"$2\"\n"
            "rm -rf \"${3:?}\"\n"
            "exec /opt/burrow/bin/burrow\n").arg(QCoreApplication::applicationPid()).toUtf8());
        s.close();
        if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {script, deb, m_assetSha256, staging.path()}))
            return tr("Couldn't start the update.");
        return {};
    }
    return tr("This copy of Burrow can't update itself. Download the new version from GitHub.");
}
#endif

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

    // Private (0700, random name) staging directory; the helper removes it after the swap.
    QTemporaryDir stagingDir(QDir::tempPath() + QStringLiteral("/burrow-update-XXXXXX"));
    if (!stagingDir.isValid())
        return tr("Couldn't prepare the update.");
    stagingDir.setAutoRemove(false);
    const QString staging = stagingDir.path();
    QFile::setPermissions(staging, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (run(QStringLiteral("/usr/bin/ditto"), {QStringLiteral("-x"), QStringLiteral("-k"), m_downloadPath, staging}).isNull()) {
        QDir(staging).removeRecursively();
        return tr("Couldn't unpack the update.");
    }
    const QString fresh = QDir(staging).filePath(QStringLiteral("Burrow.app"));
    if (run(QStringLiteral("/usr/bin/codesign"),
            {QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), fresh}).isNull()
        || teamIdentifier(fresh) != currentTeam) {
        QDir(staging).removeRecursively();
        return tr("The update's signature doesn't match this app; it was not installed.");
    }

    // A detached helper waits for Burrow to exit, verifies the signature and team again right before
    // swapping (nothing can change the bundle unnoticed in between), swaps and relaunches.
    const QString script = QDir(staging).filePath(QStringLiteral("install.sh"));
    QFile s(script);
    if (!s.open(QIODevice::WriteOnly))
        return tr("Couldn't prepare the update.");
    s.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    s.write(QStringLiteral(
        "#!/bin/sh\n"
        "# $1 = new bundle, $2 = installed bundle, $3 = expected Developer ID team, $4 = staging dir\n"
        "while kill -0 %1 2>/dev/null; do sleep 0.3; done\n"
        "team=$(/usr/bin/codesign -dv --verbose=2 \"$1\" 2>&1 | sed -n 's/^TeamIdentifier=//p')\n"
        "if /usr/bin/codesign --verify --deep --strict \"$1\" && [ \"$team\" = \"$3\" ]; then\n"
        "  old=\"$2.old\"\n"
        "  rm -rf \"$old\"\n"
        "  mv \"$2\" \"$old\" && mv \"$1\" \"$2\" && rm -rf \"$old\" || { [ -d \"$old\" ] && mv \"$old\" \"$2\"; }\n"
        "fi\n"
        "rm -rf \"$4\"\n"
        "open \"$2\"\n")
                .arg(QCoreApplication::applicationPid()).toUtf8());
    s.close();
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {script, fresh, current, currentTeam, staging}))
        return tr("Couldn't start the update.");
    return {};
}
