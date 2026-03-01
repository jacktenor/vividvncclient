#pragma once
#include "qprogressbar.h"
#include "sftptransferworker.h"
#include "sshtunnel.h"
#include <QMainWindow>
#include <QCheckBox>
#include <QComboBox>

class QLineEdit;
class QPushButton;
class QLabel;
class QComboBox;
class VncView;
class VncClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();

    void onProfileChanged(int idx);
    void onSaveProfileClicked();
    void onDeleteProfileClicked();

private:
    // Keychain password helpers (QtKeychain)
    void loadPasswordForProfile(const QString& profileName);
    void savePasswordForProfile(const QString& profileName, const QString& password);
    void deletePasswordForProfile(const QString& profileName);

    void loadProfilesIntoCombo();
    void loadProfileToFields(const QString& profileName);
    void saveFieldsToProfile(const QString& profileName);
    QString currentProfileName() const;

private:
    void loadSshPasswordForProfile(const QString& profileName);
    void saveSshPasswordForProfile(const QString& profileName, const QString& password);
    void deleteSshPasswordForProfile(const QString& profileName);

    // Profiles UI
    QComboBox* m_profiles{};
    QPushButton* m_saveProfile{};
    QPushButton* m_deleteProfile{};

    // Connection UI
    QLineEdit* m_host{};
    QLineEdit* m_port{};
    QLineEdit* m_pass{};
    QPushButton* m_connect{};
    QPushButton* m_disconnect{};
    QLabel* m_status{};

    QComboBox* m_quality;

    QCheckBox* m_savePass{};

    QLineEdit* m_sshPass{};

    VncView* m_view{};
    VncClient* m_client{};

    // SSH tunnel widgets + tunnel
    QCheckBox* m_useSsh { nullptr };
    QLineEdit* m_sshUser { nullptr };
    QLineEdit* m_sshPort { nullptr };
    QLineEdit* m_sshKeyPath { nullptr };
    SshTunnel* m_tunnel { nullptr };

    // --- File transfer UI ---
    QLineEdit*   m_remotePath = nullptr;
    QProgressBar* m_xferBar = nullptr;
    QPushButton* m_uploadBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_cancelXferBtn = nullptr;

    // --- Transfer worker thread ---
    QThread* m_xferThread = nullptr;
    SftpTransferWorker* m_xferWorker = nullptr;

};
