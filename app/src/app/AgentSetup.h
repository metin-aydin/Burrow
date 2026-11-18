#pragma once

#include <QObject>
#include <QString>

class QProcess;

// Burrow on Ubuntu installs burrow-agent (ADR 0003) on this computer, from the copy in its own package,
// so the computer's desktop can be reached from Burrow on another computer: the gallery suggests it, a
// click installs it (a systemd user service, no administrator password) and the pairing code is shown
// for the other computer. Everything runs `burrow-agent <command> --json`.
class AgentSetup : public QObject
{
    Q_OBJECT
    // This computer can run the agent: Linux with a GNOME session and the agent in Burrow's package.
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // The gallery shows its suggestion: available, not installed, not dismissed.
    Q_PROPERTY(bool suggest READ suggest NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)   // pairing code, once installed
    Q_PROPERTY(int port READ port NOTIFY changed)
public:
    explicit AgentSetup(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool installed() const { return m_installed; }
    bool running() const { return m_running; }
    bool suggest() const;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString code() const { return m_code; }
    int port() const { return m_port; }

    Q_INVOKABLE void install();
    Q_INVOKABLE void newCode();     // a new pairing key; the old code stops working
    Q_INVOKABLE void uninstall();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void dismiss();     // "Not Now": the gallery stops suggesting it (Settings still has it)
    Q_INVOKABLE void copyCode() const;

signals:
    void changed();

private:
    void run(const QString &command);
    void read(const QByteArray &json);

    QString m_binary;   // the agent in Burrow's package
    bool m_available = false;
    bool m_installed = false;
    bool m_running = false;
    bool m_busy = false;
    bool m_demo = false;   // development: BURROW_DEMO_AGENT=1 pretends, for screenshots on a Mac
    QString m_error;
    QString m_code;
    int m_port = 47391;
};
