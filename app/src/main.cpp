#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "app/AppController.h"
#include "app/PreviewProvider.h"
#include "rdp/RdpView.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Burrow"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Burrow"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("metin-aydin.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BURROW_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/icons/256x256.png")));
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
    engine.addImageProvider(QStringLiteral("preview"), new PreviewProvider(controller.store()));
    qmlRegisterSingletonInstance("Burrow.Core", 1, 0, "App", &controller);
    qmlRegisterType<RdpView>("Burrow.Core", 1, 0, "RdpView");
    qmlRegisterUncreatableType<RdpSession>("Burrow.Core", 1, 0, "RdpSession", QStringLiteral("Created by App"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Burrow", "Main");

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
                controller.saveDevice(d);
                const QString id = controller.store()->doc().devices.constLast().id;
                QTimer::singleShot(500, &controller, [&controller, id] { controller.connectDevice(id); });
                // BURROW_DEMO_TRUST=1 answers the certificate prompt automatically (test server only).
                if (qEnvironmentVariableIntValue("BURROW_DEMO_TRUST"))
                    QObject::connect(&controller, &AppController::sessionOpened, &controller, [](RdpSession *s) {
                        QObject::connect(s, &RdpSession::certificatePrompt, s, [s] { s->answerCertificate(true); });
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
