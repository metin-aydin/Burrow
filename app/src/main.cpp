#include <QApplication>
#include <QDir>
#include <QEvent>

#include "platform/WindowChrome.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

#include "app/AppController.h"
#include "app/PreviewProvider.h"
#include "app/Tray.h"
#include "rdp/RdpView.h"

namespace {
// Points OpenSSL at the "legacy" provider shipped with the app (MD4/RC4, needed by FreeRDP's NTLM sign-in).
// Without this libcrypto looks under the build machine's install prefix. Must run before FreeRDP starts;
// an OPENSSL_MODULES the user set themselves wins.
void useBundledOpenSslModules()
{
    if (qEnvironmentVariableIsSet("OPENSSL_MODULES"))
        return;
#if defined(Q_OS_MACOS)
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/../PlugIns/ossl-modules");
#else
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/ossl-modules");
#endif
    if (QDir(dir).exists())
        qputenv("OPENSSL_MODULES", QDir::toNativeSeparators(QDir(dir).canonicalPath()).toLocal8Bit());
}

// Feeds user input to the auto-lock timer.
class ActivityFilter : public QObject
{
public:
    explicit ActivityFilter(AppController *app) : m_app(app) {}
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::KeyPress: case QEvent::MouseButtonPress: case QEvent::Wheel: case QEvent::TouchBegin:
            m_app->noteActivity();
            break;
        default:
            break;
        }
        return QObject::eventFilter(o, e);
    }
private:
    AppController *m_app;
};
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);   // QApplication: the tray menu uses widgets on every platform
    useBundledOpenSslModules();
    QGuiApplication::setApplicationName(QStringLiteral("Burrow"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Burrow"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("metin-aydin.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BURROW_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Burrow/resources/icons/256x256.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller;
    // --seed-demo: fill an empty vault with sample devices and previews (development only).
    if (app.arguments().contains(QStringLiteral("--seed-demo"))) {
        controller.setDemoOpen(qEnvironmentVariable("BURROW_DEMO_OPEN"));
        QObject::connect(&controller, &AppController::stateChanged, &controller, [&controller] {
            if (controller.state() == QLatin1String("unlocked"))
                controller.seedDemo();
        });
    }

    QQmlApplicationEngine engine;
    controller.applyLanguage(&engine);
    controller.applyTheme();
    engine.addImageProvider(QStringLiteral("preview"), new PreviewProvider(controller.store()));
    qmlRegisterSingletonInstance("Burrow.Core", 1, 0, "App", &controller);
    qmlRegisterType<RdpView>("Burrow.Core", 1, 0, "RdpView");
    qmlRegisterUncreatableType<RemoteSession>("Burrow.Core", 1, 0, "RemoteSession", QStringLiteral("Created by App"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Burrow", "Main");

    // Closing the window keeps Burrow in the menu bar / tray; Quit is in the tray menu (and Cmd+Q).
    // The vault auto-locks after inactivity (Settings → Auto-Lock), so a hidden window isn't left unlocked.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    ActivityFilter activity(&controller);
    app.installEventFilter(&activity);
    Tray tray(&controller);
    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().value(0))) {
        WindowChrome::useContentTitleBar(window);   // our header sits in the title bar (Theme.titleBarHeight)
        tray.setWindow(window);
        QObject::connect(&tray, &Tray::connectRequested, window, [window](const QString &id) {
            QMetaObject::invokeMethod(window, "startSession", Q_ARG(QVariant, id), Q_ARG(QVariant, QString()),
                                      Q_ARG(QVariant, QString()));
        });
    }

    // Development helpers (only with --seed-demo): BURROW_DEMO_PASSWORD unlocks or creates
    // the vault automatically, BURROW_GRAB=<file.png> saves a window capture after 2.5 s.
    if (app.arguments().contains(QStringLiteral("--seed-demo"))) {
        const QString pw = qEnvironmentVariable("BURROW_DEMO_PASSWORD");
        if (!pw.isEmpty())
            QTimer::singleShot(300, &controller, [&controller, pw] {
                controller.state() == QLatin1String("setup") ? controller.createVault(pw, pw)
                                                             : controller.unlock(pw);
            });
        // BURROW_DEMO_TUNNEL=<profile name> starts that tunnel after unlocking.
        const QString demoTunnel = qEnvironmentVariable("BURROW_DEMO_TUNNEL");
        if (!demoTunnel.isEmpty())
            QObject::connect(&controller, &AppController::stateChanged, &controller, [&controller, demoTunnel] {
                if (controller.state() != QLatin1String("unlocked"))
                    return;
                QTimer::singleShot(800, &controller, [&controller, demoTunnel] {
                    for (const Model::Tunnel &t : controller.store()->doc().tunnels)
                        if (t.name == demoTunnel)
                            controller.connectTunnel(t.id);
                });
            });
        // BURROW_DEMO_CONNECT=host:port:user:password adds a direct test device and connects to it.
        const QStringList target = qEnvironmentVariable("BURROW_DEMO_CONNECT").split(QLatin1Char(':'));
        if (target.size() == 4)
            QObject::connect(&controller, &AppController::stateChanged, &controller, [&controller, target] {
                if (controller.state() != QLatin1String("unlocked"))
                    return;
                QVariantMap d = controller.newDevice();
                d["name"] = QStringLiteral("Local Test");
                d["host"] = target.at(0);
                d["port"] = target.at(1).toInt();
                d["username"] = target.at(2);
                d["password"] = target.at(3);
                d["savePassword"] = true;
                // BURROW_DEMO_PROTOCOL=vnc makes it a Mac reached over VNC (ADR 0008).
                if (qEnvironmentVariable("BURROW_DEMO_PROTOCOL") == QLatin1String("vnc")) {
                    d["protocol"] = QStringLiteral("vnc");
                    d["os"] = QStringLiteral("macos");
                    d["clipboard"] = QStringLiteral("off");   // never send this Mac's clipboard to a test server
                }
                // BURROW_DEMO_DISPLAYS=all spans every display (multi-monitor test).
                d["displays"] = qEnvironmentVariable("BURROW_DEMO_DISPLAYS", QStringLiteral("window"));
                // BURROW_DEMO_LAYOUT=<json> pre-sets the saved monitor layout (tabs/windows restore test).
                const QByteArray layout = qgetenv("BURROW_DEMO_LAYOUT");
                if (!layout.isEmpty())
                    d["layout"] = QJsonDocument::fromJson(layout).object().toVariantMap();
                controller.saveDevice(d);
                const QString id = controller.store()->doc().devices.constLast().id;
                QTimer::singleShot(500, &controller, [&controller, id] { controller.connectDevice(id); });
                // BURROW_DEMO_INPUT=1 sends a fixed mouse/key sequence once connected (input test, no
                // synthetic events on the desktop).
                if (qEnvironmentVariableIntValue("BURROW_DEMO_INPUT"))
                    QObject::connect(&controller, &AppController::sessionOpened, &controller, [](RemoteSession *s) {
                        QObject::connect(s, &RemoteSession::stateChanged, s, [s] {
                            if (s->state() != QLatin1String("connected"))
                                return;
                            qInfo("demo: sending test input");
                            QTimer::singleShot(800, s, [s] {
                                s->sendMouseMove(10, 10);
                                s->sendMouseButton(Qt::LeftButton, true, 100, 120);
                                s->sendMouseButton(Qt::LeftButton, false, 100, 120);
                                s->sendWheel(-120, false, 100, 120);
                                const quint32 keys[] = {'H', 'i', 0xff0d};
                                for (quint32 k : keys) { s->sendKeysym(k, true); s->sendKeysym(k, false); }
                                s->sendKeysym(0xffeb, true); s->sendKeysym('c', true);
                                s->sendKeysym('c', false); s->sendKeysym(0xffeb, false);
                                s->sendKey(0x1e, true); s->sendKey(0x1e, false);   // scancode path: 'a'
                                // BURROW_DEMO_CLOSE=1 closes the session afterwards (lock-on-disconnect test).
                                if (qEnvironmentVariableIntValue("BURROW_DEMO_CLOSE"))
                                    QTimer::singleShot(700, s, [s] { s->close(); });
                            });
                        });
                    });
                // BURROW_DEMO_TRUST=1 answers the certificate prompt automatically (test server only).
                if (qEnvironmentVariableIntValue("BURROW_DEMO_TRUST"))
                    QObject::connect(&controller, &AppController::sessionOpened, &controller, [](RemoteSession *s) {
                        QObject::connect(s, &RemoteSession::certificatePrompt, s, [s] { s->answerCertificate(true); });
                    });
            });
        // BURROW_GRAB=<file.png> saves every open window after the delay (main window first, then
        // sessions as file-1.png, file-2.png ...).
        const QString grab = qEnvironmentVariable("BURROW_GRAB");
        if (!grab.isEmpty())
            QTimer::singleShot(qEnvironmentVariableIntValue("BURROW_GRAB_DELAY") ?: 2500, &app, [grab] {
                int n = 0;
                for (QWindow *w : QGuiApplication::topLevelWindows()) {
                    auto *qw = qobject_cast<QQuickWindow *>(w);
                    if (!qw || !qw->isVisible())
                        continue;
                    QString file = grab;
                    if (n > 0)
                        file.replace(QStringLiteral(".png"), QStringLiteral("-%1.png").arg(n));
                    qw->grabWindow().save(file);
                    ++n;
                }
                QCoreApplication::quit();
            });
    }

    const int rc = app.exec();
    controller.shutdown();
    return rc;
}
