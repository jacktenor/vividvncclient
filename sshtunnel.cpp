#include "sshtunnel.h"

#include <QTcpServer>
#include <QHostAddress>
#include <QEventLoop>
#include <QTimer>

SshTunnel::SshTunnel(QObject* parent) : QObject(parent) {
    m_proc = new QProcess(this);

    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        m_lastStderr += QString::fromUtf8(m_proc->readAllStandardError());
    });

    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        Q_UNUSED(e);
        const QString reason = QString("SSH process error: %1").arg(m_proc->errorString());
        emit tunnelDied(reason);
    });

    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st) {
                Q_UNUSED(st);
                const QString reason =
                    QString("SSH tunnel exited (code %1). %2").arg(code).arg(m_lastStderr.trimmed());
                emit tunnelDied(reason);
            });
}

SshTunnel::~SshTunnel() {
    stopTunnel();
}

bool SshTunnel::isRunning() const {
    return m_proc && (m_proc->state() == QProcess::Running);
}

QString SshTunnel::buildSshTarget(const QString& user, const QString& host) const {
    if (user.trimmed().isEmpty()) return host.trimmed();
    return user.trimmed() + "@" + host.trimmed();
}

int SshTunnel::allocateFreeLocalPort(QString* errorOut) {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        if (errorOut) *errorOut = "Could not allocate local port: " + server.errorString();
        return 0;
    }
    const int port = server.serverPort();
    server.close();
    return port;
}

bool SshTunnel::startTunnel(const QString& sshHost,
                            int sshPort,
                            const QString& sshUser,
                            const QString& keyPath,
                            const QString& remoteVncHost,
                            int remoteVncPort,
                            int& localPortOut,
                            QString* errorOut)
{
    stopTunnel();
    m_lastStderr.clear();

    if (sshHost.trimmed().isEmpty()) {
        if (errorOut) *errorOut = "SSH host is empty.";
        return false;
    }
    if (sshPort <= 0) sshPort = 22;
    if (remoteVncPort <= 0) remoteVncPort = 5900;

    QString allocErr;
    m_localPort = allocateFreeLocalPort(&allocErr);
    if (m_localPort == 0) {
        if (errorOut) *errorOut = allocErr;
        return false;
    }

    // ssh -N -L <localPort>:<remoteVncHost>:<remoteVncPort> user@host -p <sshPort>
    // Important flags:
    // - ExitOnForwardFailure=yes : ssh will exit if port forward can't be established
    // - ServerAlive...           : detect dead connections
    QStringList args;
    args << "-N"
         << "-o" << "ExitOnForwardFailure=yes"
         << "-o" << "ServerAliveInterval=15"
         << "-o" << "ServerAliveCountMax=3"
         << "-o" << "BatchMode=yes"; // avoids password prompt freezing the GUI

    if (!keyPath.trimmed().isEmpty()) {
        args << "-i" << keyPath.trimmed();
    }

    args << "-p" << QString::number(sshPort);

    const QString forward =
        QString("%1:%2:%3").arg(m_localPort).arg(remoteVncHost.trimmed()).arg(remoteVncPort);
    args << "-L" << forward;

    args << buildSshTarget(sshUser, sshHost);

    m_proc->start("ssh", args);

    if (!m_proc->waitForStarted(3000)) {
        if (errorOut) *errorOut = "Failed to start ssh: " + m_proc->errorString();
        stopTunnel();
        return false;
    }

    // Give it a moment: if forwarding fails, ssh will usually exit immediately (ExitOnForwardFailure)
    QEventLoop loop;
    QTimer t;
    t.setSingleShot(true);
    connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(800);
    loop.exec();

    if (m_proc->state() != QProcess::Running) {
        const QString msg = "SSH tunnel failed: " + m_lastStderr.trimmed();
        if (errorOut) *errorOut = msg;
        stopTunnel();
        return false;
    }

    localPortOut = m_localPort;
    return true;
}

void SshTunnel::stopTunnel() {
    if (!m_proc) return;

    if (m_proc->state() == QProcess::Running) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(1000)) {
            m_proc->kill();
            m_proc->waitForFinished(1000);
        }
    }

    m_localPort = 0;
}
