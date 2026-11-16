#include "app/Tray.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QIcon>
#include <QTimer>
#include <QWindow>

#include "app/AppController.h"

namespace {
// Renders a line icon (drawn in black) in another colour, at the sizes panels ask for.
[[maybe_unused]] QIcon monochromeIcon(const QString &path, const QColor &color)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray svg = f.readAll();
    svg.replace("#000", color.name().toLatin1());
    QSvgRenderer renderer(svg);
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
} // namespace

Tray::Tray(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    // The tunnel mark alone, in one colour: a template icon on macOS (adapts to light/dark menu bars), white on Linux
    // (GNOME's and Ubuntu's top bar is dark in both themes); the colour logo on Windows.
#if defined(Q_OS_MACOS)
    QIcon icon(QStringLiteral(":/qt/qml/Burrow/qml/icons/tunnel.svg"));
    icon.setIsMask(true);
#elif defined(Q_OS_WIN)
    QIcon icon(QStringLiteral(":/qt/qml/Burrow/resources/icons/256x256.png"));
#else
    QIcon icon = monochromeIcon(QStringLiteral(":/qt/qml/Burrow/qml/icons/tunnel.svg"), QColor(Qt::white));
#endif
    m_icon.setIcon(icon);
    m_icon.setToolTip(QStringLiteral("Burrow"));
    m_icon.setContextMenu(&m_menu);
    connect(&m_menu, &QMenu::aboutToShow, this, &Tray::rebuild);
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
#ifndef Q_OS_MACOS
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            showWindow();
#else
        Q_UNUSED(r)
#endif
    });
    // Rebuilding destroys the menu's actions, so never do it from inside one of them: queue it.
    connect(app, &AppController::stateChanged, this, &Tray::rebuild, Qt::QueuedConnection);
    connect(app->devices(), &QAbstractItemModel::modelReset, this, &Tray::rebuild, Qt::QueuedConnection);
    connect(app, &AppController::languageChanged, this, &Tray::rebuild, Qt::QueuedConnection);
    rebuild();
    m_icon.show();
}

void Tray::showWindow()
{
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void Tray::rebuild()
{
    m_menu.clear();
    if (m_app->state() == QLatin1String("unlocked")) {
        auto *header = m_menu.addAction(tr("Computers"));
        header->setEnabled(false);
        const auto &devices = m_app->store()->doc().devices;
        if (devices.isEmpty()) {
            m_menu.addAction(tr("No Computers Yet"))->setEnabled(false);
        }
        for (const Model::Device &d : devices) {
            const QString tunnel = m_app->tunnelName(d.tunnelId);
            QAction *a = m_menu.addAction(tunnel.isEmpty() ? d.name : QStringLiteral("%1  ·  %2").arg(d.name, tunnel));
            const QString id = d.id;
            connect(a, &QAction::triggered, this, [this, id] {
                QTimer::singleShot(0, this, [this, id] {
                    showWindow();
                    emit connectRequested(id);
                });
            });
        }
        m_menu.addSeparator();
        m_menu.addAction(tr("Open Burrow"), this, &Tray::showWindow);
        m_menu.addAction(tr("Lock"), this, [this] { QMetaObject::invokeMethod(m_app, &AppController::lock, Qt::QueuedConnection); });
    } else {
        m_menu.addAction(tr("Unlock Burrow…"), this, &Tray::showWindow);
    }
    m_menu.addSeparator();
    m_menu.addAction(tr("Quit Burrow"), this, [] { QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection); });
}
