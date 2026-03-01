#include "sshkeybootstrap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// Must be included BEFORE windows.h if you use it
// #include <windows.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

// libssh2
#include <libssh2.h>

static QString homePath() {
    return QDir::homePath();
}

static bool readTextFile(const QString& path, QString& out, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = "Failed to open " + path + ": " + f.errorString();
        return false;
    }
    out = QString::fromUtf8(f.readAll());
    return true;
}

bool SshKeyBootstrap::ensureLocalKeypair(QString& keyPathOut,
                                         QString& pubKeyTextOut,
                                         QString* errorOut)
{
    // Default key path if none provided
    QString keyPath = keyPathOut.trimmed();
    if (keyPath.isEmpty()) {
        keyPath = homePath() + "/.ssh/id_ed25519";
    }

    const QString pubPath = keyPath + ".pub";
    QDir().mkpath(QFileInfo(keyPath).absolutePath());

    // If it already exists, just read pubkey
    if (QFileInfo::exists(keyPath) && QFileInfo::exists(pubPath)) {
        QString pub;
        if (!readTextFile(pubPath, pub, errorOut)) return false;
        keyPathOut = keyPath;
        pubKeyTextOut = pub.trimmed() + "\n";
        return true;
    }

    // Generate with ssh-keygen (no passphrase to keep tunneling automatic)
    // ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -N "" -C "QtVncTunnel"
    QProcess p;
    QStringList args;
    args << "-t" << "ed25519"
         << "-f" << keyPath
         << "-N" << ""
         << "-C" << "QtVncTunnel";

    p.start("ssh-keygen", args);
    if (!p.waitForFinished(15000)) {
        p.kill();
        if (errorOut) *errorOut = "ssh-keygen timed out.";
        return false;
    }

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (errorOut) {
            *errorOut =
                "ssh-keygen failed:\n" +
                QString::fromUtf8(p.readAllStandardError()).trimmed();
        }
        return false;
    }

    // Read public key
    QString pub;
    if (!readTextFile(pubPath, pub, errorOut)) return false;

    keyPathOut = keyPath;
    pubKeyTextOut = pub.trimmed() + "\n";
    return true;
}

static int openTcpSocket(const QString& host, int port, QString* errorOut) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const QByteArray hostUtf8 = host.toUtf8();
    const QByteArray portUtf8 = QByteArray::number(port);

    int rc = getaddrinfo(hostUtf8.constData(), portUtf8.constData(), &hints, &res);
    if (rc != 0 || !res) {
        if (errorOut) *errorOut = "getaddrinfo failed for " + host + ":" + QString::number(port);
        return -1;
    }

    int sock = -1;
    for (auto p = res; p; p = p->ai_next) {
        sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (::connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock < 0 && errorOut) *errorOut = "Could not connect TCP to " + host + ":" + QString::number(port);
    return sock;
}

static bool execRemote(LIBSSH2_SESSION* session, const QString& cmd, QString* errorOut) {
    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session);
    if (!channel) {
        if (errorOut) *errorOut = "Failed to open SSH channel.";
        return false;
    }

    QByteArray c = cmd.toUtf8();
    int rc = libssh2_channel_exec(channel, c.constData());
    if (rc != 0) {
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        if (errorOut) *errorOut = "Remote exec failed for: " + cmd;
        return false;
    }

    // Drain output (optional)
    char buffer[4096];
    for (;;) {
        ssize_t n = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (n <= 0) break;
    }
    for (;;) {
        ssize_t n = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
        if (n <= 0) break;
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    return true;
}

bool SshKeyBootstrap::installPubkeyViaPassword(const QString& remoteHost,
                                               int sshPort,
                                               const QString& sshUser,
                                               const QString& sshPassword,
                                               const QString& pubKeyText,
                                               QString* errorOut)
{
    if (remoteHost.trimmed().isEmpty() || sshUser.trimmed().isEmpty()) {
        if (errorOut) *errorOut = "Remote host or SSH user is empty.";
        return false;
    }
    if (sshPort <= 0) sshPort = 22;
    if (sshPassword.isEmpty()) {
        if (errorOut) *errorOut = "SSH password is empty.";
        return false;
    }
    if (pubKeyText.trimmed().isEmpty()) {
        if (errorOut) *errorOut = "Public key text is empty.";
        return false;
    }

    if (libssh2_init(0) != 0) {
        if (errorOut) *errorOut = "libssh2_init failed.";
        return false;
    }

    QString sockErr;
    int sock = openTcpSocket(remoteHost.trimmed(), sshPort, &sockErr);
    if (sock < 0) {
        if (errorOut) *errorOut = sockErr;
        libssh2_exit();
        return false;
    }

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        ::close(sock);
        libssh2_exit();
        if (errorOut) *errorOut = "libssh2_session_init failed.";
        return false;
    }

    libssh2_session_set_blocking(session, 1);

    int rc = libssh2_session_handshake(session, sock);
    if (rc != 0) {
        libssh2_session_free(session);
        ::close(sock);
        libssh2_exit();
        if (errorOut) *errorOut = "SSH handshake failed.";
        return false;
    }

    rc = libssh2_userauth_password(session,
                                   sshUser.toUtf8().constData(),
                                   sshPassword.toUtf8().constData());
    if (rc != 0) {
        libssh2_session_disconnect(session, "bye");
        libssh2_session_free(session);
        ::close(sock);
        libssh2_exit();
        if (errorOut) *errorOut = "SSH password auth failed.";
        return false;
    }

    // Create ~/.ssh, set perms
    if (!execRemote(session, "mkdir -p ~/.ssh && chmod 700 ~/.ssh", errorOut)) {
        libssh2_session_disconnect(session, "bye");
        libssh2_session_free(session);
        ::close(sock);
        libssh2_exit();
        return false;
    }

    // Append key if not already present.
    // This avoids duplicates across repeated connects.
    const QString escapedKey = pubKeyText.trimmed().replace("'", "'\"'\"'");
    const QString cmd =
        "grep -qxF '" + escapedKey + "' ~/.ssh/authorized_keys 2>/dev/null || "
                                     "echo '" + escapedKey + "' >> ~/.ssh/authorized_keys && "
                       "chmod 600 ~/.ssh/authorized_keys";

    if (!execRemote(session, cmd, errorOut)) {
        libssh2_session_disconnect(session, "bye");
        libssh2_session_free(session);
        ::close(sock);
        libssh2_exit();
        return false;
    }

    libssh2_session_disconnect(session, "done");
    libssh2_session_free(session);
    ::close(sock);
    libssh2_exit();
    return true;
}
