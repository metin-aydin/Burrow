#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// In-app updates from GitHub Releases (ADR 0002), same behaviour as the author's JumpDesk/UPaste:
// checks 15 s after launch and every 6 hours (packaged builds only), never downloads without asking,
// verifies the SHA-256 from the release manifest and, on macOS, that the new app carries the same
// Developer ID team signature before replacing the running bundle. On Windows (ADR 0010) the update is
// the installer, run silently; when the running Burrow is Authenticode-signed the installer must be
// validly signed by the same publisher (electron-updater's rule, as in JumpDesk).
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)          // idle | checking | upToDate | available | downloading | ready | installing | error
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)
    Q_PROPERTY(bool supported READ supported CONSTANT)
public:
    explicit Updater(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    bool autoCheck() const;
    void setAutoCheck(bool on);
    bool supported() const;

    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void download();
    Q_INVOKABLE void install();

    // Compares dotted versions ("1.10.0" > "1.9.3"). Public for tests.
    static int compareVersions(const QString &a, const QString &b);
    static QString platformKey();
    // The update package's file name for a version: never taken from the manifest as a path.
    static QString assetName(const QString &version);
#ifdef Q_OS_WIN
    // Subject of the valid Authenticode signer of a file, or empty when it isn't validly signed.
    static QString signerSubject(const QString &path);
#endif

signals:
    void changed();
    void updateAvailable(const QString &version);

private:
    void setState(const QString &s, const QString &error = {});
    void onManifest(QNetworkReply *reply);
    void onDownloaded(QNetworkReply *reply);
    QString installMac();
#ifdef Q_OS_WIN
    QString installWindows();
#endif

    QNetworkAccessManager *m_net;
    QTimer m_timer;
    QUrl m_manifestUrl;
    QString m_state = QStringLiteral("idle");
    QString m_latest, m_notes, m_error;
    QString m_assetName, m_assetSha256;
    QString m_downloadPath;
    double m_progress = 0;
};
