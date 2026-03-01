#pragma once

#include <QObject>
#include <QString>
#include <QAtomicInt>

class SftpTransferWorker : public QObject {
    Q_OBJECT
public:
    enum class Direction { Upload, Download };

    explicit SftpTransferWorker(QObject* parent = nullptr);
    ~SftpTransferWorker() override;

    void setConnectionParams(const QString& host,
                             int port,
                             const QString& user,
                             const QString& keyPath,
                             const QString& passwordFallback);

    void setTransfer(Direction dir,
                     const QString& localPath,
                     const QString& remotePath);

public slots:
    void start();
    void cancel();

signals:
    void progress(qint64 done, qint64 total);
    void status(const QString& msg);
    void finished(bool ok, const QString& errorOrEmpty);

private:
    bool runUpload(QString& err);
    bool runDownload(QString& err);

    bool connectAndAuth(QString& err);
    void disconnect();

private:
    QString m_host;
    int     m_port = 22;
    QString m_user;
    QString m_keyPath;
    QString m_passwordFallback;

    Direction m_dir = Direction::Upload;
    QString   m_localPath;
    QString   m_remotePath;

    QAtomicInt m_cancelled {0};

    // IMPORTANT:
    // libssh2 uses typedefs to these struct tags:
    //   typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
    //   typedef struct _LIBSSH2_SFTP    LIBSSH2_SFTP;
    // So we forward-declare the *struct tags* here and store pointers to them.
    struct _LIBSSH2_SESSION* m_session = nullptr;
    struct _LIBSSH2_SFTP*    m_sftp    = nullptr;

    int m_sock = -1;
};
