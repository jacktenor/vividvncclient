#pragma once

#include "qlineedit.h"
#include <QObject>
#include <QProcess>

class SshTunnel : public QObject {
    Q_OBJECT
public:
    explicit SshTunnel(QObject* parent = nullptr);
    ~SshTunnel() override;

    // Starts ssh local port forward:
    // localPort -> remoteVncHost:remoteVncPort via user@sshHost:sshPort
    // Returns true on success. localPortOut is set to the chosen local port.
    bool startTunnel(const QString& sshHost,
                     int sshPort,
                     const QString& sshUser,
                     const QString& keyPath,          // optional (recommended)
                     const QString& remoteVncHost,    // usually "127.0.0.1"
                     int remoteVncPort,               // usually 5900
                     int& localPortOut,
                     QString* errorOut = nullptr);

    void stopTunnel();
    bool isRunning() const;

    QString lastErrorOutput() const { return m_lastStderr; }

signals:
    void tunnelDied(const QString& reason);

private:
    int allocateFreeLocalPort(QString* errorOut);
    QString buildSshTarget(const QString& user, const QString& host) const;

private:
    QLineEdit* m_sshPass { nullptr };
    QProcess* m_proc { nullptr };
    int m_localPort { 0 };
    QString m_lastStderr;
};
