#include "sftptransferworker.h"
#include "qtimer.h"

#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

static void closesock_compat(int s) {
#ifdef Q_OS_WIN
    ::closesocket((SOCKET)s);
#else
    ::close(s);
#endif
}

SftpTransferWorker::SftpTransferWorker(QObject* parent) : QObject(parent) {}
SftpTransferWorker::~SftpTransferWorker() { disconnect(); }

void SftpTransferWorker::setConnectionParams(const QString& host,
                                             int port,
                                             const QString& user,
                                             const QString& keyPath,
                                             const QString& passwordFallback)
{
    m_host = host.trimmed();
    m_port = (port > 0 ? port : 22);
    m_user = user.trimmed();
    m_keyPath = keyPath.trimmed();
    m_passwordFallback = passwordFallback;
}



void SftpTransferWorker::setTransfer(Direction dir,
                                     const QString& localPath,
                                     const QString& remotePath)
{
    m_dir = dir;
    m_localPath = localPath;
    m_remotePath = remotePath;
}

void SftpTransferWorker::cancel() {
    m_cancelled.storeRelease(1);
}

bool SftpTransferWorker::connectAndAuth(QString& err) {
#ifdef Q_OS_WIN
    // Ensure WSA started (your app already does this in main for Q_OS_WIN,
    // but doing it here doesn't hurt if this worker is reused elsewhere).
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    if (m_host.isEmpty()) { err = "SFTP: host is empty."; return false; }
    if (m_user.isEmpty()) { err = "SFTP: user is empty."; return false; }

    if (libssh2_init(0) != 0) {
        err = "SFTP: libssh2_init failed.";
        return false;
    }

    // --- Resolve host ---
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo* res = nullptr;
    const QByteArray hostBA = m_host.toUtf8();
    const QByteArray portBA = QByteArray::number(m_port);

    int rc = getaddrinfo(hostBA.constData(), portBA.constData(), &hints, &res);
    if (rc != 0 || !res) {
        err = QString("SFTP: DNS/resolve failed for %1:%2").arg(m_host).arg(m_port);
        return false;
    }

    // --- Connect socket ---
    int sock = -1;
    for (auto* p = res; p; p = p->ai_next) {
        sock = (int)::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (::connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesock_compat(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        err = QString("SFTP: could not connect to %1:%2").arg(m_host).arg(m_port);
        return false;
    }

    m_sock = sock;

    // --- Start SSH session ---
    m_session = libssh2_session_init();
    if (!m_session) {
        err = "SFTP: libssh2_session_init failed.";
        return false;
    }

    libssh2_session_set_blocking(m_session, 1);

    if (libssh2_session_handshake(m_session, m_sock)) {
        err = "SFTP: SSH handshake failed.";
        return false;
    }

    // --- Auth: try public key first (same key you already use for tunneling) ---
    bool authed = false;

    if (!m_keyPath.isEmpty()) {
        // Most users keep private key at keyPath, and public key at keyPath + ".pub"
        QString pubPath = m_keyPath + ".pub";
        if (!QFileInfo::exists(pubPath)) pubPath.clear(); // libssh2 can still work with empty pub sometimes

        const QByteArray userBA = m_user.toUtf8();
        const QByteArray privBA = m_keyPath.toUtf8();
        const QByteArray pubBA  = pubPath.toUtf8();

        int a = libssh2_userauth_publickey_fromfile_ex(
            m_session,
            userBA.constData(),
            (unsigned int)userBA.size(),
            pubPath.isEmpty() ? nullptr : pubBA.constData(),
            privBA.constData(),
            nullptr // passphrase (your keys appear unencrypted; if you later add one, wire it here)
            );

        if (a == 0) authed = true;
    }

    // --- Fallback: password (optional) ---
    if (!authed && !m_passwordFallback.isEmpty()) {
        const QByteArray userBA = m_user.toUtf8();
        const QByteArray passBA = m_passwordFallback.toUtf8();
        if (libssh2_userauth_password_ex(
                m_session,
                userBA.constData(),
                (unsigned int)userBA.size(),
                passBA.constData(),
                (unsigned int)passBA.size(),
                nullptr
                ) == 0)
        {
            authed = true;
        }
    }

    if (!authed) {
        err = "SFTP: authentication failed (publickey + password fallback both failed).";
        return false;
    }

    // --- Start SFTP subsystem ---
    m_sftp = libssh2_sftp_init(m_session);
    if (!m_sftp) {
        err = "SFTP: libssh2_sftp_init failed (server may not have SFTP enabled).";
        return false;
    }

    emit status("SFTP connected.");
    return true;
}

void SftpTransferWorker::disconnect() {
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "Normal Shutdown");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_sock >= 0) {
        closesock_compat(m_sock);
        m_sock = -1;
    }
    libssh2_exit();

#ifdef Q_OS_WIN
    WSACleanup();
#endif
}

bool SftpTransferWorker::runUpload(QString& err) {
    QFile local(m_localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        err = "Upload: cannot open local file: " + local.errorString();
        return false;
    }

    const qint64 total = local.size();
    emit progress(0, total);
    emit status("Uploading...");

    const QByteArray remoteBA = m_remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE* rfile = libssh2_sftp_open(
        m_sftp,
        remoteBA.constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        0644
        );

    if (!rfile) {
        err = "Upload: cannot open remote file for write: " + m_remotePath;
        return false;
    }

    qint64 done = 0;
    QByteArray buf;
    buf.resize(64 * 1024);

    while (!local.atEnd()) {
        if (m_cancelled.loadAcquire()) {
            libssh2_sftp_close(rfile);
            err = "Upload cancelled.";
            return false;
        }

        const qint64 n = local.read(buf.data(), buf.size());
        if (n <= 0) break;

        const char* p = buf.constData();
        qint64 left = n;

        while (left > 0) {
            if (m_cancelled.loadAcquire()) {
                libssh2_sftp_close(rfile);
                err = "Upload cancelled.";
                return false;
            }

            const ssize_t w = libssh2_sftp_write(rfile, p, (size_t)left);
            if (w < 0) {
                libssh2_sftp_close(rfile);
                err = "Upload: remote write failed.";
                return false;
            }

            p += w;
            left -= w;
            done += w;
            emit progress(done, total);
        }
    }

    libssh2_sftp_close(rfile);
    emit status("Upload complete.");
    return true;
}

static bool sftpRemoteExists(LIBSSH2_SFTP* sftp,
                             const QString& remotePath,
                             quint64* outSize)
{
    if (!sftp) return false;

    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    const QByteArray p = remotePath.toUtf8();

    const int rc = libssh2_sftp_stat_ex(
        sftp,
        p.constData(),
        (unsigned int)p.size(),
        LIBSSH2_SFTP_STAT,
        &attrs
        );

    if (rc == 0) {
        if (outSize) {
            if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
                *outSize = (quint64)attrs.filesize;
            else
                *outSize = 0;
        }
        return true;
    }

    return false;
}

void SftpTransferWorker::start()
{
    m_cancelled.storeRelease(0);

    QString err;
    emit status("Connecting for SFTP...");

    if (!connectAndAuth(err)) {
        emit finished(false, err);
        return;
    }

    // Overwrite prompt for uploads
    if (m_dir == Direction::Upload) {
        quint64 existingSize = 0;

        if (sftpRemoteExists((LIBSSH2_SFTP*)m_sftp, m_remotePath, &existingSize)) {
            m_overwriteDecisionReady = false;
            m_overwriteApproved = false;

            emit overwritePrompt(m_remotePath, existingSize);

            QEventLoop loop;
            QTimer poll;
            poll.setInterval(25);

            connect(&poll, &QTimer::timeout, &loop, [&]() {
                if (m_overwriteDecisionReady)
                    loop.quit();
            });

            poll.start();
            loop.exec();

            if (!m_overwriteApproved) {
                disconnect();
                emit finished(false, "Upload cancelled (remote file exists).");
                return;
            }
        }
    }

    bool ok = false;
    if (m_dir == Direction::Upload)
        ok = runUpload(err);
    else
        ok = runDownload(err);

    disconnect();
    emit finished(ok, ok ? QString() : err);
}

void SftpTransferWorker::setOverwriteDecision(bool overwrite)
{
    m_overwriteApproved = overwrite;
    m_overwriteDecisionReady = true;
}

bool SftpTransferWorker::runDownload(QString& err) {
    QFile local(m_localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err = "Download: cannot open local output file: " + local.errorString();
        return false;
    }

    const QByteArray remoteBA = m_remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE* rfile = libssh2_sftp_open(
        m_sftp,
        remoteBA.constData(),
        LIBSSH2_FXF_READ,
        0
        );

    if (!rfile) {
        err = "Download: cannot open remote file for read: " + m_remotePath;
        return false;
    }

    // Try to get remote size for progress
    qint64 total = -1;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (libssh2_sftp_stat(m_sftp, remoteBA.constData(), &attrs) == 0) {
        if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) total = (qint64)attrs.filesize;
    }

    emit progress(0, total);
    emit status("Downloading...");

    qint64 done = 0;
    QByteArray buf;
    buf.resize(64 * 1024);

    while (true) {
        if (m_cancelled.loadAcquire()) {
            libssh2_sftp_close(rfile);
            err = "Download cancelled.";
            return false;
        }

        const ssize_t r = libssh2_sftp_read(rfile, buf.data(), buf.size());
        if (r == 0) break;       // EOF
        if (r < 0) {
            libssh2_sftp_close(rfile);
            err = "Download: remote read failed.";
            return false;
        }

        const qint64 w = local.write(buf.constData(), (qint64)r);
        if (w != (qint64)r) {
            libssh2_sftp_close(rfile);
            err = "Download: local write failed.";
            return false;
        }

        done += r;
        emit progress(done, total);
    }

    libssh2_sftp_close(rfile);
    emit status("Download complete.");
    return true;
}
