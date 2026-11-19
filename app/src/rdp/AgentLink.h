#pragma once

#include <QByteArray>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Talks to burrow-agent on an Ubuntu computer (ADR 0003): "start" unlocks its screen before the RDP
// connection, beats every 5 s keep it unlocked, "end" locks it again. Each command goes over its own
// connection through the device's tunnel: the agent sends a one-time challenge, the command is signed
// with the pairing key together with it (HMAC-SHA256), and the agent's reply is signed the same way, so a
// recorded command can't be replayed and an impostor agent is noticed. Must match agent/protocol.go.
class AgentLink
{
public:
    AgentLink(int tunnelHandle, const QString &host, quint16 port, const QByteArray &key);
    ~AgentLink();

    // Sends one command and waits for the signed reply: empty on success, otherwise what went wrong.
    QString send(const char *cmd);
    // Beats every 5 s on a thread of its own until stop(); stop() then sends "end".
    void startBeats();
    void stop();

    // Pairing code (base32 in groups, as burrow-agent prints it) → 32-byte key; empty if not one.
    static QByteArray keyFromCode(const QString &code);
    static QString requestMac(const QByteArray &key, const QString &cmd, const QString &challenge);
    static QString replyMac(const QByteArray &key, const QString &cmd, const QString &challenge, bool ok);

private:
    int m_tunnel;
    QString m_host;
    quint16 m_port;
    QByteArray m_key;
    std::thread m_beats;
    std::mutex m_mu;
    std::condition_variable m_cv;
    bool m_stopping = false;
    std::atomic_bool m_started{false};
};
