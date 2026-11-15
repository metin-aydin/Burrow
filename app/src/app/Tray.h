#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWindow>

class AppController;

// Menu bar / system tray icon on every platform: lists the saved computers (click to connect)
// plus Open, Lock and Quit.
class Tray : public QObject
{
    Q_OBJECT
public:
    Tray(AppController *app, QObject *parent = nullptr);
    void setWindow(QWindow *window) { m_window = window; }
    void showWindow();

signals:
    void connectRequested(const QString &deviceId);

private:
    void rebuild();

    AppController *m_app;
    QSystemTrayIcon m_icon;
    QMenu m_menu;
    QPointer<QWindow> m_window;
};
