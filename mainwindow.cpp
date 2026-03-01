#include "mainwindow.h"
#include "vncview.h"
#include "vncclient.h"
#include "sshkeybootstrap.h"
#include "sshtunnel.h"
#include "applog.h"
#include <qt6keychain/keychain.h>
#include "sftptransferworker.h"
#include "remotebrowserdialog.h"

#include <QSignalBlocker>
#include <QCheckBox>
#include <QDesktopServices>
#include <QUrl>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QComboBox>
#include <QSettings>
#include <QInputDialog>
#include <QMessageBox>
#include <QSizePolicy>
#include <QClipboard>
#include <QApplication>
#include <QStringList>
#include <QStyleFactory>
#include <QProgressBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>

static QSettings makeSettings() {
    return QSettings("QtVnc", "QtVncClient");
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    central->setStyleSheet(R"(
    QCheckBox::indicator {
        width: 14px;
        height: 14px;
        border: 1px solid palette(mid);
        border-radius: 3px;
        background: transparent;
    }
    QCheckBox::indicator:checked {
        border: 1px solid palette(mid);
        background: transparent;
        image: url(:/icons/checkmark.png);
    }
)");

    // ---- TOP BAR (two rows) ----
    auto* topBar = new QVBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);
    topBar->setSpacing(6);

    auto* row1 = new QHBoxLayout();
    auto* row2 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row2->setContentsMargins(0, 0, 0, 0);

    QFontMetrics fm(QApplication::font());

    // ── Row 1 widgets ──────────────────────────────────────────────────────────

    m_profiles = new QComboBox();
    m_profiles->setFixedWidth(150);

    m_saveProfile   = new QPushButton("Save...");
    m_deleteProfile = new QPushButton("Delete");

    m_host = new QLineEdit("127.0.0.1");
    m_port = new QLineEdit("5900");
    m_host->setFixedWidth(fm.horizontalAdvance("255.255.255.255") + 20);
    m_port->setFixedWidth(fm.horizontalAdvance("65535") + 18);

    m_pass = new QLineEdit();
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setFixedWidth(150);

    m_savePass = new QCheckBox("Save password?");
    m_savePass->setLayoutDirection(Qt::RightToLeft);

    m_connect    = new QPushButton("Connect");
    m_disconnect = new QPushButton("Disconnect");
    m_disconnect->setEnabled(false);

    m_quality = new QComboBox();
    m_quality->addItem("Sharp (Text)");
    m_quality->addItem("Balanced");
    m_quality->addItem("Smooth Motion");

    // Fixed-width labels for Row 1 — sized to the widest label in that row
    const QStringList row1LabelTexts = { "Profile:", "Quality:", "Host:", "Port:", "Password:" };
    int row1LabelW = 0;
    for (const QString& t : row1LabelTexts)
        row1LabelW = qMax(row1LabelW, fm.horizontalAdvance(t));

    auto makeRow1Label = [&](const QString& t) -> QLabel* {
        auto* l = new QLabel(t);
        //l->setFixedWidth(row1LabelW);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    auto makeGroup = [&](int spacing = 6) -> QHBoxLayout* {
        auto* g = new QHBoxLayout();
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(spacing);
        return g;
    };

    // ── Row 1 assembly ────────────────────────────────────────────────────────
    row1->addSpacing(20);
    row1->addWidget(makeRow1Label("Profile:"));
    row1->addSpacing(0);
    row1->addWidget(m_profiles);
    row1->addSpacing(0);
    row1->addWidget(m_saveProfile);
    row1->addSpacing(0);
    row1->addWidget(m_deleteProfile);
    row1->addSpacing(8);

    row1->addWidget(makeRow1Label("Quality:"));
    row1->addSpacing(4);
    row1->addWidget(m_quality);
    row1->addSpacing(8);

    row1->addWidget(makeRow1Label("Host:"));
    row1->addSpacing(0);
    row1->addWidget(m_host);
    row1->addSpacing(4);
    row1->addWidget(makeRow1Label("Port:"));
    row1->addSpacing(0);
    row1->addWidget(m_port);
    row1->addSpacing(8);

    row1->addWidget(makeRow1Label("Password:"));
    row1->addSpacing(4);
    row1->addWidget(m_pass);
    row1->addSpacing(4);
    row1->addWidget(m_savePass);

    row1->addStretch(1);   // ← this is the elastic gap that absorbs window resizing

    row1->addWidget(m_connect);
    row1->addSpacing(0);
    row1->addWidget(m_disconnect);
    row1->addSpacing(25);

    // ── Row 2 widgets ──────────────────────────────────────────────────────────

    m_status = new QLabel("Idle");

    m_useSsh = new QCheckBox("SSH tunnel?");
    m_useSsh->setLayoutDirection(Qt::RightToLeft);

    m_sshUser = new QLineEdit();
    m_sshUser->setPlaceholderText("SSH user");
    m_sshUser->setFixedWidth(100);

    m_sshPort = new QLineEdit("22");
    m_sshPort->setFixedWidth(fm.horizontalAdvance("65535") + 8);

    m_sshPass = new QLineEdit();
    m_sshPass->setPlaceholderText("SSH password (first-time setup only)");
    m_sshPass->setEchoMode(QLineEdit::Password);
    m_sshPass->setFixedWidth(150);

    m_sshKeyPath = new QLineEdit();
    m_sshKeyPath->setPlaceholderText("~/.ssh/id_ed25519");
    m_sshKeyPath->setFixedWidth(200);

    // ── Transfer widgets (defined ONCE, live in row2) ──────────────────────────

    m_xferBar = new QProgressBar;
    m_xferBar->setMinimum(0);
    m_xferBar->setMaximum(100);
    m_xferBar->setValue(0);
    m_xferBar->setTextVisible(true);
    // Let the progress bar consume all the spare horizontal space
    m_xferBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_uploadBtn     = new QPushButton("Upload");
    m_downloadBtn   = new QPushButton("Download");
    m_cancelXferBtn = new QPushButton("Cancel");
    m_cancelXferBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_downloadBtn->setEnabled(false);

    // Row 2 — each label is sized individually (no shared fixed width) so there
    // are no artificial gaps between the checkbox and "User:", or between the
    // user field and "Port:".
    auto makeR2Label = [](const QString& t) -> QLabel* {
        auto* l = new QLabel(t);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    row2->addSpacing(20);
    row2->addWidget(m_useSsh);
    row2->addSpacing(4);           // one small deliberate gap after the checkbox
    row2->addWidget(makeR2Label("User:"));
    row2->addSpacing(4);
    row2->addWidget(m_sshUser);
    row2->addSpacing(4);
    row2->addWidget(makeR2Label("Port:"));
    row2->addSpacing(4);
    row2->addWidget(m_sshPort);
    row2->addSpacing(4);
    row2->addWidget(makeR2Label("SSH pass:"));
    row2->addSpacing(4);
    row2->addWidget(m_sshPass);
    row2->addSpacing(4);
    row2->addWidget(makeR2Label("Key:"));
    row2->addSpacing(4);
    row2->addWidget(m_sshKeyPath);
    row2->addSpacing(8);
    // Status label — just enough for short messages; progress bar fills the rest
    row2->addWidget(m_status);
    m_status->setFixedWidth(200);

    row2->addSpacing(6);

    row2->addWidget(m_xferBar, 1);   // stretch=1 → takes all spare space
    row2->addSpacing(4);
    row2->addWidget(m_uploadBtn);
    row2->addWidget(m_downloadBtn);
    row2->addWidget(m_cancelXferBtn);
    row2->addSpacing(25);

    topBar->addLayout(row1);
    topBar->addLayout(row2);

    // ── VNC view ───────────────────────────────────────────────────────────────

    m_view = new VncView();

    root->addLayout(topBar);
    root->addWidget(m_view, 1);

    // ── Footer ─────────────────────────────────────────────────────────────────

    auto* link = new QLabel(R"(<a href="https://beeralator.com">Beeralator.com</a>)");
    link->setTextFormat(Qt::RichText);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    link->setOpenExternalLinks(true);
    link->setAlignment(Qt::AlignHCenter);
    link->setStyleSheet("QLabel { padding: 6px; }");
    root->addWidget(link);

    setCentralWidget(central);
    resize(1000, 800);

    // ── SFTP worker thread (created once, reused) ──────────────────────────────

    m_xferThread = new QThread(this);
    m_xferWorker = new SftpTransferWorker;
    m_xferWorker->moveToThread(m_xferThread);
    m_xferThread->start();

    // ── SFTP signals ───────────────────────────────────────────────────────────

    connect(m_xferWorker, &SftpTransferWorker::status, this, [this](const QString& s) {
        m_status->setText("SFTP: " + s);
    });

    connect(m_xferWorker, &SftpTransferWorker::progress, this, [this](qint64 done, qint64 total) {
        if (total <= 0) {
            m_xferBar->setMaximum(0);   // busy/indeterminate
            return;
        }
        if (m_xferBar->maximum() == 0) m_xferBar->setMaximum(100);
        const int pct = (int)((done * 100) / total);
        m_xferBar->setValue(pct);
        m_xferBar->setFormat(QString("%1%)").arg(pct).arg(done).arg(total));
    });

    connect(m_xferWorker, &SftpTransferWorker::finished, this, [this](bool ok, const QString& err) {
        m_cancelXferBtn->setEnabled(false);
        m_uploadBtn->setEnabled(true);
        m_downloadBtn->setEnabled(true);
        if (!ok) {
            m_status->setText("SFTP error: " + err);
            m_xferBar->setMaximum(100);
            m_xferBar->setValue(0);
        } else {
            m_status->setText("SFTP: done.");
            m_xferBar->setMaximum(100);
            m_xferBar->setValue(100);
        }
    });

    connect(m_cancelXferBtn, &QPushButton::clicked, m_xferWorker, &SftpTransferWorker::cancel);

    // ── Upload button ──────────────────────────────────────────────────────────

    connect(m_uploadBtn, &QPushButton::clicked, this, [this]() {
        const QString local = QFileDialog::getOpenFileName(
            this,
            "Select file to upload",
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        if (local.isEmpty()) return;

        const QString localName = QFileInfo(local).fileName();
        const QString sshUser   = m_sshUser->text().trimmed();

        // Build a sensible default remote path
        QString remote;
        if (!sshUser.isEmpty())
            remote = "/home/" + sshUser + "/Downloads/" + localName;
        else
            remote = "/tmp/" + localName;

        const QString host        = m_host->text().trimmed();
        const int     sshPort     = m_sshPort->text().trimmed().toInt();
        const QString keyPath     = m_sshKeyPath->text().trimmed();
        const QString passFallback= m_sshPass->text();

        m_uploadBtn->setEnabled(false);
        m_downloadBtn->setEnabled(false);
        m_cancelXferBtn->setEnabled(true);
        m_xferBar->setMaximum(100);
        m_xferBar->setValue(0);

        m_xferWorker->setConnectionParams(host, sshPort, sshUser, keyPath, passFallback);
        m_xferWorker->setTransfer(SftpTransferWorker::Direction::Upload, local, remote);
        QMetaObject::invokeMethod(m_xferWorker, "start", Qt::QueuedConnection);
    });

    // ── Download button ────────────────────────────────────────────────────────

    connect(m_downloadBtn, &QPushButton::clicked, this, [this]() {
        const QString host        = m_host->text().trimmed();
        const int     sshPort     = m_sshPort->text().trimmed().toInt();
        const QString user        = m_sshUser->text().trimmed();
        const QString keyPath     = m_sshKeyPath->text().trimmed();
        const QString passFallback= m_sshPass->text();

        if (host.isEmpty() || user.isEmpty()) {
            m_status->setText("SFTP: set Host + SSH User first.");
            return;
        }

        // Open the remote file browser
        RemoteBrowserDialog dlg(this);
        dlg.setConnectionParams(host, sshPort, user, keyPath, passFallback);
        dlg.setStartDir("/home/" + user);
        if (dlg.exec() != QDialog::Accepted) return;

        const QString remote = dlg.selectedRemotePath().trimmed();
        if (remote.isEmpty()) return;

        // Save to local Downloads with the same filename
        QString filename = QFileInfo(remote).fileName();
        if (filename.isEmpty()) filename = "download.bin";

        const QString dlDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        const QString local = QDir(dlDir).filePath(filename);

        if (QFileInfo::exists(local)) {
            const auto r = QMessageBox::question(
                this, "Overwrite?",
                QString("This file already exists:\n\n%1\n\nOverwrite it?").arg(local),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r != QMessageBox::Yes) return;
        }

        m_uploadBtn->setEnabled(false);
        m_downloadBtn->setEnabled(false);
        m_cancelXferBtn->setEnabled(true);
        m_xferBar->setMaximum(100);
        m_xferBar->setValue(0);

        m_xferWorker->setConnectionParams(host, sshPort, user, keyPath, passFallback);
        m_xferWorker->setTransfer(SftpTransferWorker::Direction::Download, local, remote);
        QMetaObject::invokeMethod(m_xferWorker, "start", Qt::QueuedConnection);
    });

    // ── VNC client ─────────────────────────────────────────────────────────────

    m_client = new VncClient(this);

    // Clipboard sync: local → remote
    QClipboard* cb = QApplication::clipboard();
    connect(cb, &QClipboard::dataChanged, this, [this, cb]() {
        if (!m_disconnect->isEnabled()) return;
        const QString text = cb->text();
        if (text.isEmpty()) return;
        if (text.toUtf8().size() > 256 * 1024) return;
        m_client->sendClipboardText(text);
    });

    // Clipboard sync: remote → local
    connect(m_client, &VncClient::clipboardTextReceived, this, [cb](const QString& text) {
        if (text.isEmpty()) return;
        const QSignalBlocker blocker(cb);
        cb->setText(text);
    });

    m_view->setClient(m_client);

    // ── SSH tunnel ─────────────────────────────────────────────────────────────

    m_tunnel = new SshTunnel(this);
    connect(m_tunnel, &SshTunnel::tunnelDied, this, [this](const QString& why) {
        m_status->setText("Tunnel died: " + why);
        m_client->disconnectFromHost();
        m_connect->setEnabled(true);
        m_disconnect->setEnabled(false);
    });

    // ── Connect / Disconnect buttons ───────────────────────────────────────────

    connect(m_connect,    &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);

    // ── VNC client → UI ────────────────────────────────────────────────────────

    connect(m_client, &VncClient::frameReady, m_view, &VncView::setFrame);

    connect(m_client, &VncClient::status, this, [this](const QString& s) {
        m_status->setText(s);
        appLogLine("STATUS: " + s);
    });

    connect(m_client, &VncClient::connected, this, [this]() {
        m_connect->setEnabled(false);
        m_disconnect->setEnabled(true);
        m_view->setFocus();
        m_view->grabKeyboard();
        m_uploadBtn->setEnabled(true);
        m_downloadBtn->setEnabled(true);
    });

    connect(m_client, &VncClient::disconnected, this, [this]() {
        m_connect->setEnabled(true);
        m_disconnect->setEnabled(false);
        if (m_tunnel)       m_tunnel->stopTunnel();
        if (m_view)         m_view->releaseKeyboard();
        m_uploadBtn->setEnabled(false);
        m_downloadBtn->setEnabled(false);
        m_cancelXferBtn->setEnabled(false);
    });

    // ── Profiles ───────────────────────────────────────────────────────────────

    connect(m_profiles, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onProfileChanged);
    connect(m_saveProfile,   &QPushButton::clicked, this, &MainWindow::onSaveProfileClicked);
    connect(m_deleteProfile, &QPushButton::clicked, this, &MainWindow::onDeleteProfileClicked);

    loadProfilesIntoCombo();

    QSettings s = makeSettings();
    m_savePass->setChecked(s.value("ui/save_password", false).toBool());
}

MainWindow::~MainWindow() = default;

// ── Profile helpers ────────────────────────────────────────────────────────────

QString MainWindow::currentProfileName() const {
    if (!m_profiles) return {};
    return m_profiles->currentData().toString();
}

void MainWindow::loadProfilesIntoCombo() {
    QSettings s = makeSettings();

    m_profiles->blockSignals(true);
    m_profiles->clear();

    s.beginGroup("profiles");
    const QStringList groups = s.childGroups();
    s.endGroup();

    m_profiles->addItem("(No profile)", QString());
    for (const QString& name : groups)
        m_profiles->addItem(name, name);

    const QString last = s.value("ui/last_profile", QString()).toString();
    int idxToSelect = 0;
    if (!last.isEmpty()) {
        for (int i = 0; i < m_profiles->count(); ++i) {
            if (m_profiles->itemData(i).toString() == last) {
                idxToSelect = i;
                break;
            }
        }
    }
    m_profiles->setCurrentIndex(idxToSelect);
    m_profiles->blockSignals(false);

    onProfileChanged(m_profiles->currentIndex());
    m_deleteProfile->setEnabled(!currentProfileName().isEmpty());
}

void MainWindow::loadProfileToFields(const QString& profileName) {
    if (profileName.isEmpty()) return;

    QSettings s = makeSettings();
    s.beginGroup("profiles");
    s.beginGroup(profileName);

    const QString host        = s.value("host", "").toString();
    const int     port        = s.value("port", 5900).toInt();
    const int     qualityIndex= s.value("quality_index", 0).toInt();
    const bool    useSsh      = s.value("ssh/use", false).toBool();
    const QString sshUser     = s.value("ssh/user", "").toString();
    const int     sshPort     = s.value("ssh/port", 22).toInt();
    const QString sshKey      = s.value("ssh/key", "").toString();

    s.endGroup();
    s.endGroup();

    if (!host.isEmpty()) m_host->setText(host);
    m_port->setText(QString::number(port));

    if (m_quality) {
        const int safeIdx = qBound(0, qualityIndex, m_quality->count() - 1);
        m_quality->setCurrentIndex(safeIdx);
    }

    if (m_useSsh)    m_useSsh->setChecked(useSsh);
    if (m_sshUser)   m_sshUser->setText(sshUser);
    if (m_sshPort)   m_sshPort->setText(QString::number(sshPort));
    if (m_sshKeyPath)m_sshKeyPath->setText(sshKey);
}

void MainWindow::saveFieldsToProfile(const QString& profileName) {
    if (profileName.isEmpty()) return;

    QSettings s = makeSettings();
    s.beginGroup("profiles");
    s.beginGroup(profileName);

    s.setValue("host",          m_host->text().trimmed());
    s.setValue("port",          m_port->text().trimmed().toInt());
    if (m_quality)   s.setValue("quality_index", m_quality->currentIndex());
    if (m_useSsh)    s.setValue("ssh/use",  m_useSsh->isChecked());
    if (m_sshUser)   s.setValue("ssh/user", m_sshUser->text().trimmed());
    if (m_sshPort)   s.setValue("ssh/port", m_sshPort->text().trimmed().toInt());
    if (m_sshKeyPath)s.setValue("ssh/key",  m_sshKeyPath->text().trimmed());

    s.endGroup();
    s.endGroup();

    s.setValue("ui/last_profile", profileName);
}

void MainWindow::onProfileChanged(int /*idx*/) {
    const QString name = currentProfileName();
    m_deleteProfile->setEnabled(!name.isEmpty());

    QSettings s = makeSettings();
    s.setValue("ui/last_profile", name);

    if (!name.isEmpty()) {
        loadProfileToFields(name);
        m_status->setText(QString("Loaded profile: %1").arg(name));

        if (m_pass)    m_pass->clear();
        if (m_sshPass) m_sshPass->clear();

        loadPasswordForProfile(name);
        loadSshPasswordForProfile(name);
    }
}

void MainWindow::onSaveProfileClicked() {
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "Save Profile", "Profile name:",
        QLineEdit::Normal,
        currentProfileName().isEmpty() ? QString("Home") : currentProfileName(),
        &ok);

    name = name.trimmed();
    if (!ok || name.isEmpty()) return;

    QSettings s = makeSettings();
    s.beginGroup("profiles");
    const bool exists = s.childGroups().contains(name);
    s.endGroup();

    if (exists) {
        const auto res = QMessageBox::question(
            this, "Overwrite Profile",
            QString("Profile \"%1\" already exists. Overwrite?").arg(name));
        if (res != QMessageBox::Yes) return;
    }

    saveFieldsToProfile(name);

    if (m_savePass->isChecked()) {
        if (m_pass && !m_pass->text().isEmpty())
            savePasswordForProfile(name, m_pass->text());
        if (m_sshPass && !m_sshPass->text().isEmpty())
            saveSshPasswordForProfile(name, m_sshPass->text());
    }

    loadProfilesIntoCombo();
    for (int i = 0; i < m_profiles->count(); ++i) {
        if (m_profiles->itemData(i).toString() == name) {
            m_profiles->setCurrentIndex(i);
            break;
        }
    }

    m_status->setText(QString("Saved profile: %1").arg(name));
}

void MainWindow::onDeleteProfileClicked() {
    const QString name = currentProfileName();
    if (name.isEmpty()) return;

    const auto res = QMessageBox::question(
        this, "Delete Profile",
        QString("Delete profile \"%1\"?").arg(name));
    if (res != QMessageBox::Yes) return;

    QSettings s = makeSettings();
    s.beginGroup("profiles");
    s.remove(name);
    s.endGroup();

    deletePasswordForProfile(name);
    deleteSshPasswordForProfile(name);

    if (s.value("ui/last_profile").toString() == name)
        s.setValue("ui/last_profile", QString());

    loadProfilesIntoCombo();
    m_status->setText(QString("Deleted profile: %1").arg(name));
}

void MainWindow::onConnectClicked() {
    const QString host       = m_host->text().trimmed();
    const int     vncPort    = m_port->text().trimmed().toInt();
    const QString vncPass    = m_pass->text();
    const int     qualityIndex = m_quality->currentIndex();

    if (m_tunnel) m_tunnel->stopTunnel();

    if (m_useSsh && m_useSsh->isChecked()) {
        const QString sshUser    = m_sshUser->text().trimmed();
        const int     sshPort    = m_sshPort->text().trimmed().toInt();
        QString       keyPath    = m_sshKeyPath->text().trimmed();
        const QString sshPassword= m_sshPass ? m_sshPass->text() : QString();

        QString pubKey, err;
        if (!SshKeyBootstrap::ensureLocalKeypair(keyPath, pubKey, &err)) {
            m_status->setText("Key setup failed: " + err);
            return;
        }
        if (m_sshKeyPath) m_sshKeyPath->setText(keyPath);

        if (!sshPassword.isEmpty()) {
            m_status->setText("Installing SSH key on server...");
            QString installErr;
            if (!SshKeyBootstrap::installPubkeyViaPassword(host, sshPort, sshUser, sshPassword, pubKey, &installErr))
                m_status->setText("Key install warning: " + installErr);
        }

        int localPort = 0;
        QString tunnelErr;
        m_status->setText("Starting SSH tunnel...");
        const bool ok = m_tunnel->startTunnel(
            host, sshPort, sshUser, keyPath,
            "127.0.0.1", vncPort,
            localPort, &tunnelErr);

        if (!ok) {
            m_status->setText("SSH tunnel failed: " + tunnelErr);
            m_connect->setEnabled(true);
            m_disconnect->setEnabled(false);
            return;
        }

        m_status->setText(QString("Tunnel up: 127.0.0.1:%1 → %2:%3")
                              .arg(localPort).arg(host).arg(vncPort));
        m_client->connectToHost("127.0.0.1", localPort, vncPass, qualityIndex);
        return;
    }

    m_client->connectToHost(host, vncPort, vncPass, qualityIndex);
}

void MainWindow::onDisconnectClicked() {
    m_client->disconnectFromHost();
    if (m_tunnel) m_tunnel->stopTunnel();
}

// ── Keychain helpers ───────────────────────────────────────────────────────────

static QString keychainKeyForProfile(const QString& profileName) {
    return QString("QtVncClient/%1").arg(profileName);
}

static QString keychainKeyForSshPass(const QString& profileName) {
    return QString("QtVncClient/%1/sshpass").arg(profileName);
}

void MainWindow::loadPasswordForProfile(const QString& profileName) {
    if (profileName.isEmpty()) return;
    auto* job = new QKeychain::ReadPasswordJob("QtVncClient", this);
    job->setKey(keychainKeyForProfile(profileName));
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error()) return;
        const QString pass = static_cast<QKeychain::ReadPasswordJob*>(job)->textData();
        if (!pass.isEmpty()) m_pass->setText(pass);
    });
    job->start();
}

void MainWindow::savePasswordForProfile(const QString& profileName, const QString& password) {
    if (profileName.isEmpty()) return;
    auto* job = new QKeychain::WritePasswordJob("QtVncClient", this);
    job->setKey(keychainKeyForProfile(profileName));
    job->setTextData(password);
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error())
            m_status->setText(QString("Password save failed: %1").arg(job->errorString()));
    });
    job->start();
}

void MainWindow::deletePasswordForProfile(const QString& profileName) {
    if (profileName.isEmpty()) return;
    auto* job = new QKeychain::DeletePasswordJob("QtVncClient", this);
    job->setKey(keychainKeyForProfile(profileName));
    connect(job, &QKeychain::Job::finished, this, [job]() { Q_UNUSED(job); });
    job->start();
}

void MainWindow::loadSshPasswordForProfile(const QString& profileName) {
    if (profileName.isEmpty()) return;
    auto* job = new QKeychain::ReadPasswordJob("QtVncClient", this);
    job->setKey(keychainKeyForSshPass(profileName));
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error()) return;
        const QString pass = static_cast<QKeychain::ReadPasswordJob*>(job)->textData();
        if (!pass.isEmpty() && m_sshPass) m_sshPass->setText(pass);
    });
    job->start();
}

void MainWindow::saveSshPasswordForProfile(const QString& profileName, const QString& password) {
    if (profileName.isEmpty()) return;
    auto* job = new QKeychain::WritePasswordJob("QtVncClient", this);
    job->setKey(keychainKeyForSshPass(profileName));
    job->setTextData(password);
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        if (job->error())
            m_status->setText(QString("SSH password save failed: %1").arg(job->errorString()));
    });
    job->start();
}

void MainWindow::deleteSshPasswordForProfile(const QString& profileName) {
    if (profileName.isEmpty()) return;
    auto* job = new QKeychain::DeletePasswordJob("QtVncClient", this);
    job->setKey(keychainKeyForSshPass(profileName));
    connect(job, &QKeychain::Job::finished, this, [job]() { Q_UNUSED(job); });
    job->start();
}
