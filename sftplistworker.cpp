#include "sftplistworker.h"

#include <QFileInfo>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
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

SftpListWorker::SftpListWorker(QObject* parent) : QObject(parent) {}
SftpListWorker::~SftpListWorker() { disconnect(); }

void SftpListWorker::setConnectionParams(const QString& host,
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

bool SftpListWorker::connectAndAuth(QString& err) {
#ifdef Q_OS_WIN
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    if (libssh2_init(0) != 0) { err = "libssh2_init failed"; return false; }
    if (m_host.isEmpty() || m_user.isEmpty()) { err = "Host/user empty"; return false; }

    // Resolve host
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* res = nullptr;
    const QByteArray hostBA = m_host.toUtf8();
    const QByteArray portBA = QByteArray::number(m_port);

    if (getaddrinfo(hostBA.constData(), portBA.constData(), &hints, &res) != 0 || !res) {
        err = "Resolve failed";
        return false;
    }

    int sock = -1;
    for (auto* p = res; p; p = p->ai_next) {
        sock = (int)::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (::connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesock_compat(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) { err = "Connect failed"; return false; }
    m_sock = sock;

    m_session = (struct _LIBSSH2_SESSION*)libssh2_session_init();
    if (!m_session) { err = "session_init failed"; return false; }

    libssh2_session_set_blocking((LIBSSH2_SESSION*)m_session, 1);

    if (libssh2_session_handshake((LIBSSH2_SESSION*)m_session, m_sock)) {
        err = "Handshake failed";
        return false;
    }

    bool authed = false;

    // Public key auth first
    if (!m_keyPath.isEmpty()) {
        QString pubPath = m_keyPath + ".pub";
        if (!QFileInfo::exists(pubPath)) pubPath.clear();

        const QByteArray userBA = m_user.toUtf8();
        const QByteArray privBA = m_keyPath.toUtf8();
        const QByteArray pubBA  = pubPath.toUtf8();

        const int a = libssh2_userauth_publickey_fromfile_ex(
            (LIBSSH2_SESSION*)m_session,
            userBA.constData(), (unsigned int)userBA.size(),
            pubPath.isEmpty() ? nullptr : pubBA.constData(),
            privBA.constData(),
            nullptr
            );
        if (a == 0) authed = true;
    }

    // Password fallback
    if (!authed && !m_passwordFallback.isEmpty()) {
        const QByteArray userBA = m_user.toUtf8();
        const QByteArray passBA = m_passwordFallback.toUtf8();
        if (libssh2_userauth_password_ex(
                (LIBSSH2_SESSION*)m_session,
                userBA.constData(), (unsigned int)userBA.size(),
                passBA.constData(), (unsigned int)passBA.size(),
                nullptr
                ) == 0)
        {
            authed = true;
        }
    }

    if (!authed) { err = "Auth failed"; return false; }

    m_sftp = (struct _LIBSSH2_SFTP*)libssh2_sftp_init((LIBSSH2_SESSION*)m_session);
    if (!m_sftp) { err = "SFTP init failed"; return false; }

    return true;
}

void SftpListWorker::disconnect() {
    if (m_sftp) {
        libssh2_sftp_shutdown((LIBSSH2_SFTP*)m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect((LIBSSH2_SESSION*)m_session, "Normal Shutdown");
        libssh2_session_free((LIBSSH2_SESSION*)m_session);
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

void SftpListWorker::listDirectory(const QString& remoteDir) {
    QString err;
    disconnect();

    if (!connectAndAuth(err)) {
        emit error("SFTP browse: " + err);
        return;
    }

    QVector<RemoteEntry> out;

    const QByteArray dirBA = remoteDir.toUtf8();
    LIBSSH2_SFTP_HANDLE* dh = libssh2_sftp_opendir((LIBSSH2_SFTP*)m_sftp, dirBA.constData());
    if (!dh) {
        emit error("SFTP browse: cannot open dir: " + remoteDir);
        disconnect();
        return;
    }

    while (true) {
        char name[512];
        char longentry[1024];
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        const int rc = libssh2_sftp_readdir_ex(dh, name, sizeof(name), longentry, sizeof(longentry), &attrs);
        if (rc <= 0) break;

        const QString n = QString::fromUtf8(name, rc);
        if (n == "." || n == "..") continue;

        RemoteEntry e;
        e.name = n;

        if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
            const unsigned long t = (attrs.permissions & LIBSSH2_SFTP_S_IFMT);
            e.isDir = (t == LIBSSH2_SFTP_S_IFDIR);
        }

        if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) e.size = (quint64)attrs.filesize;

        out.push_back(e);
    }

    libssh2_sftp_closedir(dh);
    emit listed(remoteDir, out);

    disconnect();
}
