#pragma once

#include <QString>

class SshKeyBootstrap {
public:
    // Ensures you have a local keypair. If keyPath is empty, uses default:
    // ~/.ssh/id_ed25519 (preferred)
    static bool ensureLocalKeypair(QString& keyPathOut,
                                   QString& pubKeyTextOut,
                                   QString* errorOut = nullptr);

    // Installs pubKeyText into remote authorized_keys using SSH password auth (libssh2).
    // remoteHost is the SSH host (same as your server host).
    static bool installPubkeyViaPassword(const QString& remoteHost,
                                         int sshPort,
                                         const QString& sshUser,
                                         const QString& sshPassword,
                                         const QString& pubKeyText,
                                         QString* errorOut = nullptr);
};
