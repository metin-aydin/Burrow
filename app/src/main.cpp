#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "app/AppController.h"
#include "app/PreviewProvider.h"

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
        const QString grab = qEnvironmentVariable("BURROW_GRAB");
        if (!grab.isEmpty())
            QTimer::singleShot(qEnvironmentVariableIntValue("BURROW_GRAB_DELAY") ?: 2500, &app, [&engine, grab] {
                if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
                    w->grabWindow().save(grab);
                QCoreApplication::quit();
            });
    }

    const int rc = app.exec();
    controller.lock();
    return rc;
}
