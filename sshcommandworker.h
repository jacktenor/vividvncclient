#pragma once
#include <QObject>
#include <QString>
#include <libssh2.h>

class SshCommandWorker : public QObject
{
    Q_OBJECT
public:
    explicit SshCommandWorker(QObject* parent=nullptr) : QObject(parent) {}

    void setConnectionParams(const QString& host, int port,
                             const QString& user,
                             const QString& keyPath,
                             const QString& passwordFallback)
    {
        m_host = host.trimmed();
        m_port = (port > 0 ? port : 22);
        m_user = user.trimmed();
        m_keyPath = keyPath.trimmed();
        m_pass = passwordFallback;
    }

    void setCommand(const QString& cmd) { m_command = cmd; }

public slots:
    void start();

signals:
    void finished(bool ok, const QString& output);

private:
    bool connectAndAuth(QString& err);
    void disconnectNow();

    QString m_host, m_user, m_keyPath, m_pass, m_command;
    int m_port = 22;

    LIBSSH2_SESSION* m_session = nullptr;
    int m_sock = -1;
};
