#include "sshcommandworker.h"

#include <QByteArray>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include <libssh2_sftp.h>

bool SshCommandWorker::connectAndAuth(QString& err)
{
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)m_port);

    // NOTE: This supports IPv4 numeric addresses. If you use hostnames, replace with getaddrinfo().
    if (inet_pton(AF_INET, m_host.toUtf8().constData(), &sin.sin_addr) != 1) {
        err = "SSH: invalid IPv4 address (hostnames not supported in this helper yet)";
        return false;
    }

    m_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sock < 0) {
        err = "SSH: socket() failed";
        return false;
    }

    if (::connect(m_sock, (sockaddr*)(&sin), sizeof(sockaddr_in)) != 0) {
        err = "SSH: connect() failed";
        return false;
    }

    m_session = libssh2_session_init();
    if (!m_session) {
        err = "SSH: session_init failed";
        return false;
    }

    if (libssh2_session_handshake(m_session, m_sock)) {
        err = "SSH: handshake failed";
        return false;
    }

    const QByteArray userBA = m_user.toUtf8();
    const QByteArray keyBA  = m_keyPath.toUtf8();
    const QByteArray passBA = m_pass.toUtf8();

    const int rc = libssh2_userauth_publickey_fromfile(
        m_session,
        userBA.constData(),
        nullptr,
        keyBA.constData(),
        passBA.isEmpty() ? nullptr : passBA.constData()
    );

    if (rc != 0) {
        err = "SSH: authentication failed";
        return false;
    }

    return true;
}

void SshCommandWorker::disconnectNow()
{
    if (m_session) {
        libssh2_session_disconnect(m_session, "Bye");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_sock >= 0) {
        ::close(m_sock);
        m_sock = -1;
    }
}

void SshCommandWorker::start()
{
    QString err;
    if (!connectAndAuth(err)) {
        disconnectNow();
        emit finished(false, err);
        return;
    }

    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(m_session);
    if (!ch) {
        disconnectNow();
        emit finished(false, "SSH: cannot open channel");
        return;
    }

    const QByteArray cmdBA = m_command.toUtf8();
    if (libssh2_channel_exec(ch, cmdBA.constData()) != 0) {
        libssh2_channel_free(ch);
        disconnectNow();
        emit finished(false, "SSH: exec failed");
        return;
    }

    QString out;
    char buf[4096];
    for (;;) {
        const int rc = libssh2_channel_read(ch, buf, sizeof(buf));
        if (rc > 0) out += QString::fromUtf8(buf, rc);
        else break;
    }
    for (;;) {
        const int rc = libssh2_channel_read_stderr(ch, buf, sizeof(buf));
        if (rc > 0) out += QString::fromUtf8(buf, rc);
        else break;
    }

    libssh2_channel_close(ch);
    libssh2_channel_free(ch);
    disconnectNow();

    emit finished(true, out.trimmed());
}
