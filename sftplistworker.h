#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct RemoteEntry {
    QString name;
    bool isDir = false;
    quint64 size = 0;
};

class SftpListWorker : public QObject {
    Q_OBJECT
public:
    explicit SftpListWorker(QObject* parent = nullptr);
    ~SftpListWorker() override;

    void setConnectionParams(const QString& host,
                             int port,
                             const QString& user,
                             const QString& keyPath,
                             const QString& passwordFallback);

public slots:
    void listDirectory(const QString& remoteDir);

signals:
    void listed(const QString& remoteDir, const QVector<RemoteEntry>& entries);
    void error(const QString& msg);

private:
    bool connectAndAuth(QString& err);
    void disconnect();

private:
    QString m_host;
    int     m_port = 22;
    QString m_user;
    QString m_keyPath;
    QString m_passwordFallback;

    int m_sock = -1;
    struct _LIBSSH2_SESSION* m_session = nullptr;
    struct _LIBSSH2_SFTP*    m_sftp    = nullptr;
};
