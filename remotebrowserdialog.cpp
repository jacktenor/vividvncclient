#include "remotebrowserdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QHeaderView>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QStyle>
#include <QFrame>
#include <QMouseEvent>
#include <algorithm>
#include <QMessageBox>
#include <QSettings>
#include  <QInputDialog>
// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

QString RemoteBrowserDialog::humanSize(quint64 bytes) const {
    if (bytes == 0)              return QString("0 B");
    if (bytes < 1024)            return QString("%1 B").arg(bytes);
    if (bytes < 1024*1024)       return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024*1024*1024)  return QString("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
    return                              QString("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 2);
}

QIcon RemoteBrowserDialog::iconForEntry(const RemoteEntry& e) const {
    if (e.isDir)
        return QApplication::style()->standardIcon(QStyle::SP_DirIcon);

    const QString ext = e.name.section('.', -1).toLower();

    static const QSet<QString> media = {"mp3","wav","flac","ogg","mp4","mkv","avi","mov","webm"};
    if (media.contains(ext))
        return QApplication::style()->standardIcon(QStyle::SP_MediaPlay);

    return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

RemoteBrowserDialog::RemoteBrowserDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Remote File Browser (SFTP)");
    resize(900, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(2);

    auto makeToolBtn = [&](QStyle::StandardPixmap icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setIcon(QApplication::style()->standardIcon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize({20, 20});
        return b;
    };

    m_btnBack    = makeToolBtn(QStyle::SP_ArrowBack,     "Back");
    m_btnForward = makeToolBtn(QStyle::SP_ArrowForward,  "Forward");
    m_btnUp      = makeToolBtn(QStyle::SP_ArrowUp,       "Parent Directory");
    m_btnRefresh = makeToolBtn(QStyle::SP_BrowserReload, "Refresh");

    m_btnHidden = makeToolBtn(QStyle::SP_FileDialogDetailedView, "Show Hidden Files");
    m_btnHidden->setCheckable(true);
    m_btnHidden->setChecked(false);
    toolRow->addWidget(m_btnHidden);  // add this after m_btnRefresh

    connect(m_btnHidden, &QToolButton::toggled,
            this, &RemoteBrowserDialog::toggleHiddenFiles);

    m_btnBack->setEnabled(false);
    m_btnForward->setEnabled(false);

    toolRow->addWidget(m_btnBack);
    toolRow->addWidget(m_btnForward);
    toolRow->addWidget(m_btnUp);
    toolRow->addWidget(m_btnRefresh);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    toolRow->addWidget(sep);

    // ── Breadcrumb bar ────────────────────────────────────────────────────────
    m_breadcrumbBar = new QWidget;
    m_breadcrumbBar->setCursor(Qt::ArrowCursor);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbBar);
    m_breadcrumbLayout->setContentsMargins(4, 0, 4, 0);
    m_breadcrumbLayout->setSpacing(0);

    // The plain text box is hidden by default; it appears when you
    // double-click the breadcrumb bar so you can type a path directly.
    m_dir = new QLineEdit;
    m_dir->setPlaceholderText("/home/user");
    m_dir->hide();

    toolRow->addWidget(m_breadcrumbBar, 1);
    toolRow->addWidget(m_dir, 1);

    root->addLayout(toolRow);

    // ── Splitter: sidebar | file tree ─────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal);

    
// Sidebar bookmarks (built-in + persistent custom)
m_sidebar = new QListWidget;
m_sidebar->setMaximumWidth(180);
m_sidebar->setMinimumWidth(120);
m_sidebar->setFrameShape(QFrame::NoFrame);
m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
m_sidebar->setStyleSheet(
    "QListWidget { background: palette(window); }"
    "QListWidget::item { padding: 4px 8px; border-radius: 4px; }"
    "QListWidget::item:selected { background: palette(highlight)"
    );

auto addBuiltin = [&](QListWidgetItem*& outItem,
                      const QString& label,
                      const QString& path,
                      QStyle::StandardPixmap icon) {
    outItem = new QListWidgetItem(QApplication::style()->standardIcon(icon), label);
    outItem->setData(kRolePath, path);
    outItem->setData(kRoleIsCustom, false);
    m_sidebar->addItem(outItem);
};

addBuiltin(m_bmRoot,      "Root",      "/",      QStyle::SP_DriveHDIcon);
addBuiltin(m_bmHome,      "Home",      "/home",  QStyle::SP_DirHomeIcon);
addBuiltin(m_bmTmp,       "tmp",       "/tmp",   QStyle::SP_DirIcon);
addBuiltin(m_bmDownloads, "Downloads", "/home",  QStyle::SP_DirIcon);

// Custom bookmarks will be loaded after setConnectionParams() sets host/user.

    // File tree — 3 columns (no permissions since worker doesn't provide them)
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({"Name", "Size", "Type"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setSortingEnabled(false);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIconSize({16, 16});

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_tree);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({140, 700});

    root->addWidget(m_splitter, 1);

    // ── Bottom bar ────────────────────────────────────────────────────────────
    auto* bottom = new QHBoxLayout();
    m_status = new QLabel("Idle");
    m_status->setStyleSheet("color: palette(mid);");
    m_select = new QPushButton("Download");
    m_select->setDefault(true);
    m_select->setEnabled(false);
    m_cancel = new QPushButton("Cancel");
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_select);
    bottom->addWidget(m_cancel);
    root->addLayout(bottom);

    // ── Worker thread ─────────────────────────────────────────────────────────
    m_thread = new QThread(this);
    m_worker = new SftpListWorker;
    m_worker->moveToThread(m_thread);
    m_thread->start();

    // ── Signal / slot connections ─────────────────────────────────────────────
    connect(m_btnRefresh, &QToolButton::clicked,
            this, &RemoteBrowserDialog::refresh);
    connect(m_btnUp,      &QToolButton::clicked,
            this, &RemoteBrowserDialog::goUp);
    connect(m_btnBack,    &QToolButton::clicked,
            this, &RemoteBrowserDialog::goBack);
    connect(m_btnForward, &QToolButton::clicked,
            this, &RemoteBrowserDialog::goForward);

    connect(m_tree, &QTreeWidget::itemActivated,
            this, &RemoteBrowserDialog::onItemActivated);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &RemoteBrowserDialog::onItemSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &RemoteBrowserDialog::showContextMenu);

    connect(m_select, &QPushButton::clicked,
            this, &RemoteBrowserDialog::acceptSelection);
    connect(m_cancel, &QPushButton::clicked,
            this, &QDialog::reject);

    connect(m_sidebar, &QListWidget::itemClicked,
            this, &RemoteBrowserDialog::onSidebarItemClicked);

    connect(m_worker, &SftpListWorker::listed,
            this, &RemoteBrowserDialog::onListed);
    connect(m_worker, &SftpListWorker::error,
            this, &RemoteBrowserDialog::onError);

    // Pressing Enter in the typed-path box navigates there and hides the box
    connect(m_dir, &QLineEdit::returnPressed, this, [this]() {
        const QString path = m_dir->text().trimmed();
        if (!path.isEmpty()) navigateTo(path);
        m_dir->hide();
        m_breadcrumbBar->show();
    });

    // Double-clicking the breadcrumb bar switches to the editable text box
    m_breadcrumbBar->installEventFilter(this);
}

RemoteBrowserDialog::~RemoteBrowserDialog() {
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    delete m_worker;
}

// ─────────────────────────────────────────────────────────────────────────────
// Event filter — double-click breadcrumb → show editable path box
// ─────────────────────────────────────────────────────────────────────────────

bool RemoteBrowserDialog::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_breadcrumbBar &&
        ev->type() == QEvent::MouseButtonDblClick) {
        m_dir->setText(m_currentDir);
        m_breadcrumbBar->hide();
        m_dir->show();
        m_dir->setFocus();
        m_dir->selectAll();
        return true;
    }
    return QDialog::eventFilter(obj, ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void RemoteBrowserDialog::setConnectionParams(const QString& host, int port,
                                              const QString& user,
                                              const QString& keyPath,
                                              const QString& passwordFallback)
{
    m_host = host.trimmed();
    m_port = (port > 0 ? port : 22);
    m_user = user.trimmed();
    m_key  = keyPath.trimmed();
    m_pass = passwordFallback;

    // Auto-set start dir from username if none has been set yet
    if (m_currentDir.isEmpty()) {
        if (m_user == "root")
            m_currentDir = "/root";
        else if (!m_user.isEmpty())
            m_currentDir = "/home/" + m_user;
    }


    // Now that we know who we're logged in as, update built-in bookmarks and load persistent custom ones
    updateBuiltinBookmarks();
    loadCustomBookmarks();
}

void RemoteBrowserDialog::setStartDir(const QString& dir) {
    m_currentDir = dir;
    m_dir->setText(dir);
}

QString RemoteBrowserDialog::selectedRemotePath() const {
    return m_selected;
}

void RemoteBrowserDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    if (!m_autoRefreshedOnce) {
        m_autoRefreshedOnce = true;
        const QString startDir = m_currentDir.isEmpty()
                                     ? m_dir->text().trimmed()
                                     : m_currentDir;
        if (!startDir.isEmpty())
            navigateTo(startDir, false);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────

QString RemoteBrowserDialog::joinPath(const QString& dir, const QString& name) const {
    if (dir.endsWith('/')) return dir + name;
    return dir + "/" + name;
}

void RemoteBrowserDialog::navigateTo(const QString& dir, bool addToHistory) {
    if (dir.isEmpty()) return;

    if (addToHistory && !m_currentDir.isEmpty() && m_currentDir != dir) {
        m_backStack.push(m_currentDir);
        m_forwardStack.clear();
    }

    m_currentDir = dir;
    rebuildBreadcrumbs(dir);

    m_btnBack->setEnabled(!m_backStack.isEmpty());
    m_btnForward->setEnabled(!m_forwardStack.isEmpty());
    m_btnUp->setEnabled(dir != "/");

    refresh();
}

void RemoteBrowserDialog::rebuildBreadcrumbs(const QString& dir) {
    // Remove all existing widgets from the breadcrumb bar
    while (QLayoutItem* item = m_breadcrumbLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    // Root "/" button
    auto* rootBtn = new QToolButton;
    rootBtn->setText("/");
    rootBtn->setAutoRaise(true);
    rootBtn->setStyleSheet("QToolButton { font-weight: bold; }");
    connect(rootBtn, &QToolButton::clicked, this, [this]() { navigateTo("/"); });
    m_breadcrumbLayout->addWidget(rootBtn);

    // One button per path segment
    const QStringList parts = dir.split('/', Qt::SkipEmptyParts);
    QStringList cumulative;
    for (const QString& part : parts) {
        cumulative << part;
        const QString fullPath = "/" + cumulative.join('/');

        auto* arrow = new QLabel(" › ");
        arrow->setStyleSheet("color: palette(mid);");
        m_breadcrumbLayout->addWidget(arrow);

        auto* btn = new QToolButton;
        btn->setText(part);
        btn->setAutoRaise(true);
        connect(btn, &QToolButton::clicked, this, [this, fullPath]() {
            navigateTo(fullPath);
        });
        m_breadcrumbLayout->addWidget(btn);
    }

    m_breadcrumbLayout->addStretch(1);
}

void RemoteBrowserDialog::refresh() {
    if (m_currentDir.isEmpty()) return;

    m_status->setText(QString("Listing %1 …").arg(m_currentDir));
    m_tree->clear();
    m_selected.clear();
    m_select->setEnabled(false);

    m_worker->setConnectionParams(m_host, m_port, m_user, m_key, m_pass);
    QMetaObject::invokeMethod(m_worker, "listDirectory", Qt::QueuedConnection,
                              Q_ARG(QString, m_currentDir));
}

void RemoteBrowserDialog::goUp() {
    QString dir = m_currentDir;
    if (dir.isEmpty() || dir == "/") return;
    if (dir.endsWith('/') && dir.size() > 1) dir.chop(1);
    const int idx = dir.lastIndexOf('/');
    navigateTo(idx <= 0 ? "/" : dir.left(idx));
}

void RemoteBrowserDialog::goBack() {
    if (m_backStack.isEmpty()) return;
    m_forwardStack.push(m_currentDir);
    navigateTo(m_backStack.pop(), false);
}

void RemoteBrowserDialog::goForward() {
    if (m_forwardStack.isEmpty()) return;
    m_backStack.push(m_currentDir);
    navigateTo(m_forwardStack.pop(), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker callbacks
// ─────────────────────────────────────────────────────────────────────────────

void RemoteBrowserDialog::onListed(const QString& dir,
                                   const QVector<RemoteEntry>& entries) {
    m_lastEntries = entries;   // cache for show/hide toggle
    populateTree(dir, entries);
}

// Add this new private helper (also declare it in the .h as:  void populateTree(const QString& dir, const QVector<RemoteEntry>& entries); )
void RemoteBrowserDialog::populateTree(const QString& dir,
                                       const QVector<RemoteEntry>& entries) {
    m_tree->setSortingEnabled(false);
    m_tree->clear();
    m_selected.clear();
    m_select->setEnabled(false);

    QVector<RemoteEntry> filtered;
    filtered.reserve(entries.size());

    for (const auto& e : entries) {
        if (!m_showHidden && e.name.startsWith('.'))
            continue;
        filtered.append(e);
    }

    // Folders first, then alphabetical within each group
    std::sort(filtered.begin(), filtered.end(),
              [](const RemoteEntry& a, const RemoteEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir > b.isDir;
                  return a.name.toLower() < b.name.toLower();
              });

    int folderCount = 0, fileCount = 0;

    for (const auto& e : filtered) {
        auto* row = new QTreeWidgetItem(m_tree);
        row->setIcon(0, iconForEntry(e));
        row->setText(0, e.name);
        row->setText(1, e.isDir ? QString() : humanSize(e.size));
        row->setText(2, e.isDir ? "Folder" : e.name.section('.', -1).toUpper());
        row->setData(0, kRolePath, e.name);
        row->setData(0, kRoleIsDir, e.isDir);
        row->setData(1, kRolePath, e.isDir ? quint64(0) : e.size);

        e.isDir ? ++folderCount : ++fileCount;
    }

    m_tree->setSortingEnabled(false);
    

    m_status->setText(QString("%1 folder(s), %2 file(s) in %3")
                          .arg(folderCount).arg(fileCount).arg(dir));
}

void RemoteBrowserDialog::onError(const QString& msg) {
    m_status->setText("Error: " + msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI interaction
// ─────────────────────────────────────────────────────────────────────────────

void RemoteBrowserDialog::onItemActivated(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;

    const QString name  = item->data(0, kRolePath).toString();
    const bool    isDir = item->data(0, kRoleIsDir).toBool();

    if (isDir) {
        navigateTo(joinPath(m_currentDir, name));
        return;
    }

    // For files, reuse the same path as the Select/Download button so we get confirmation.
    m_tree->setCurrentItem(item);
    acceptSelection();
}

void RemoteBrowserDialog::onItemSelectionChanged() {
    const auto sel = m_tree->selectedItems();
    if (sel.isEmpty()) {
        m_select->setEnabled(false);
        m_status->setText(QString("%1 item(s)").arg(m_tree->topLevelItemCount()));
        return;
    }
    const bool isDir = sel.first()->data(0, kRoleIsDir).toBool();
    m_select->setEnabled(!isDir);
    if (!isDir)
        m_status->setText(joinPath(m_currentDir,
                                   sel.first()->data(0, kRolePath).toString()));
}

void RemoteBrowserDialog::acceptSelection() {
    auto* item = m_tree->currentItem();
    if (!item) return;

    const bool    isDir = item->data(0, kRoleIsDir).toBool();
    const QString name  = item->data(0, kRolePath).toString();

    if (isDir) { navigateTo(joinPath(m_currentDir, name)); return; }

    const QString remotePath = joinPath(m_currentDir, name);
    const quint64 bytes = item->data(1, kRolePath).toULongLong();

    const QString msg =
        "Download this file? Remote:" + remotePath + (bytes ? ("Size: " + humanSize(bytes)) : QString());

    const auto reply = QMessageBox::question(
        this,
        "Confirm download",
        msg,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (reply != QMessageBox::Yes)
        return;

    m_selected = remotePath;
    accept();
}

void RemoteBrowserDialog::onSidebarItemClicked(QListWidgetItem* item) {
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) navigateTo(path);
}

void RemoteBrowserDialog::showContextMenu(const QPoint& pos) {

    auto* item = m_tree->itemAt(pos);
    QMenu menu(this);


// ── Bookmarks ─────────────────────────────────────────────────
QAction* addBmCurrent = menu.addAction("Add current folder to bookmarks…");
connect(addBmCurrent, &QAction::triggered, this, [this]() {
    const QString path = m_currentDir.isEmpty() ? "/" : m_currentDir;
    bool ok = false;
    const QString label = QInputDialog::getText(
        this, "Add Bookmark", "Name:",
        QLineEdit::Normal, "Bookmark", &ok);
    if (!ok || label.trimmed().isEmpty()) return;
    addCustomBookmark(label.trimmed(), path);
});

if (item) {
    const bool isDir = item->data(0, kRoleIsDir).toBool();
    const QString name = item->data(0, kRolePath).toString();
    if (isDir) {
        QAction* addBmSel = menu.addAction("Add selected folder to bookmarks…");
        connect(addBmSel, &QAction::triggered, this, [this, name]() {
            const QString path = joinPath(m_currentDir, name);
            bool ok = false;
            const QString label = QInputDialog::getText(
                this, "Add Bookmark", "Name:",
                QLineEdit::Normal, name, &ok);
            if (!ok || label.trimmed().isEmpty()) return;
            addCustomBookmark(label.trimmed(), path);
        });
    }
}

menu.addSeparator();



    QAction* hiddenAct = menu.addAction(m_showHidden ? "Hide Hidden Files" : "Show Hidden Files");
    hiddenAct->setCheckable(true);
    hiddenAct->setChecked(m_showHidden);
    connect(hiddenAct, &QAction::triggered, this, [this](bool checked) {
        m_showHidden = checked;
        m_btnHidden->setChecked(checked);   // keep button in sync
        populateTree(m_currentDir, m_lastEntries);
    });
    menu.addSeparator();



    if (item) {
        const bool    isDir = item->data(0, kRoleIsDir).toBool();
        const QString name  = item->data(0, kRolePath).toString();
        const QString full  = joinPath(m_currentDir, name);

        if (isDir) {
            menu.addAction(
                QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon),
                "Open Folder", this, [this, full]() { navigateTo(full); });
        } else {
            menu.addAction(
                QApplication::style()->standardIcon(QStyle::SP_ArrowDown),
                "Download", this, [this, item]() { if (item) { m_tree->setCurrentItem(item); acceptSelection(); } });
        }
        menu.addSeparator();
        menu.addAction("Copy Path", this, [full]() {
            QApplication::clipboard()->setText(full);
        });
        menu.addSeparator();
    }

    menu.addAction(
        QApplication::style()->standardIcon(QStyle::SP_BrowserReload),
        "Refresh", this, &RemoteBrowserDialog::refresh);

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void RemoteBrowserDialog::toggleHiddenFiles() {
    m_showHidden = m_btnHidden->isChecked();
    m_btnHidden->setToolTip(m_showHidden ? "Hide Hidden Files" : "Show Hidden Files");
    if (!m_lastEntries.isEmpty())
        populateTree(m_currentDir, m_lastEntries);
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistent bookmarks
// ─────────────────────────────────────────────────────────────────────────────

QString RemoteBrowserDialog::makeBookmarkSettingsKey() const
{
    // key per connection profile so each host/user keeps its own bookmarks
    return QString("%1:%2:%3").arg(m_host).arg(m_port).arg(m_user);
}

void RemoteBrowserDialog::updateBuiltinBookmarks()
{
    // Compute home dir from username (simple heuristic)
    QString home = "/home";
    if (m_user == "root") home = "/root";
    else if (!m_user.isEmpty()) home = "/home/" + m_user;

    if (m_bmRoot)      m_bmRoot->setData(kRolePath, "/");
    if (m_bmHome)      m_bmHome->setData(kRolePath, home);
    if (m_bmTmp)       m_bmTmp->setData(kRolePath, "/tmp");

    // Downloads: try common casing; store as QStringList so click handler can fall back
    if (m_bmDownloads) {
        if (home == "/root") {
            // root often doesn't have Downloads; still provide a sane default
            m_bmDownloads->setData(kRolePath, QStringList({"/root/Downloads", "/root/downloads", "/root"}));
        } else if (home.startsWith("/home/")) {
            m_bmDownloads->setData(kRolePath, QStringList({home + "/Downloads", home + "/downloads", home}));
        } else {
            m_bmDownloads->setData(kRolePath, QStringList({"/home", "/tmp"}));
        }
    }
}

void RemoteBrowserDialog::clearCustomBookmarkItems()
{
    // Remove sidebar items that are marked as custom
    for (int i = m_sidebar->count() - 1; i >= 0; --i) {
        QListWidgetItem* it = m_sidebar->item(i);
        if (it && it->data(kRoleIsCustom).toBool()) {
            delete m_sidebar->takeItem(i);
        }
    }
}

void RemoteBrowserDialog::loadCustomBookmarks()
{
    if (!m_sidebar) return;
    clearCustomBookmarkItems();
    m_customBookmarks.clear();

    const QString key = makeBookmarkSettingsKey();
    if (key.trimmed().isEmpty() || m_host.isEmpty() || m_user.isEmpty()) {
        // We can still browse without bookmarks; we'll load once params are set.
        return;
    }

    QSettings s;
    s.beginGroup("RemoteBrowserDialog");
    s.beginGroup("Bookmarks");
    s.beginGroup(key);

    const int n = s.beginReadArray("items");
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        const QString label = s.value("label").toString();
        const QString path  = s.value("path").toString();
        if (label.isEmpty() || path.isEmpty()) continue;

        m_customBookmarks.push_back({label, path});

        auto* it = new QListWidgetItem(QApplication::style()->standardIcon(QStyle::SP_DirIcon), label);
        it->setData(kRolePath, path);
        it->setData(kRoleIsCustom, true);
        m_sidebar->addItem(it);
    }
    s.endArray();

    s.endGroup(); // key
    s.endGroup(); // Bookmarks
    s.endGroup(); // RemoteBrowserDialog
}

void RemoteBrowserDialog::saveCustomBookmarks() const
{
    const QString key = makeBookmarkSettingsKey();
    if (key.trimmed().isEmpty() || m_host.isEmpty() || m_user.isEmpty())
        return;

    QSettings s;
    s.beginGroup("RemoteBrowserDialog");
    s.beginGroup("Bookmarks");
    s.beginGroup(key);

    s.remove(""); // wipe group first so deletes persist
    s.beginWriteArray("items", m_customBookmarks.size());
    for (int i = 0; i < m_customBookmarks.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("label", m_customBookmarks[i].label);
        s.setValue("path",  m_customBookmarks[i].path);
    }
    s.endArray();

    s.endGroup();
    s.endGroup();
    s.endGroup();
}

void RemoteBrowserDialog::addCustomBookmark(const QString& label, const QString& path)
{
    const QString cleanLabel = label.trimmed();
    const QString cleanPath  = path.trimmed();
    if (cleanLabel.isEmpty() || cleanPath.isEmpty() || !m_sidebar) return;

    // de-dupe by path
    for (const auto& b : m_customBookmarks) {
        if (b.path == cleanPath) return;
    }

    m_customBookmarks.push_back({cleanLabel, cleanPath});

    auto* it = new QListWidgetItem(QApplication::style()->standardIcon(QStyle::SP_DirIcon), cleanLabel);
    it->setData(kRolePath, cleanPath);
    it->setData(kRoleIsCustom, true);
    m_sidebar->addItem(it);

    saveCustomBookmarks();
}

void RemoteBrowserDialog::showSidebarContextMenu(const QPoint& pos)
{
    if (!m_sidebar) return;

    QListWidgetItem* clicked = m_sidebar->itemAt(pos);

    QMenu menu(this);

    QAction* addCurrent = menu.addAction("Add current folder to bookmarks…");
    connect(addCurrent, &QAction::triggered, this, [this]() {
        const QString suggested = m_currentDir.isEmpty() ? "/" : m_currentDir;
        bool ok = false;
        const QString label = QInputDialog::getText(this, "Add Bookmark", "Name:", QLineEdit::Normal, "Bookmark", &ok);
        if (!ok || label.trimmed().isEmpty()) return;
        addCustomBookmark(label, suggested);
    });

    if (clicked && clicked->data(kRoleIsCustom).toBool()) {
        menu.addSeparator();

        QAction* rename = menu.addAction("Rename…");
        connect(rename, &QAction::triggered, this, [this, clicked]() {
            const int idx = m_sidebar->row(clicked);
            if (idx < 0) return;

            bool ok = false;
            const QString newName = QInputDialog::getText(
                this, "Rename Bookmark", "Name:",
                QLineEdit::Normal, clicked->text(), &ok);
            if (!ok || newName.trimmed().isEmpty()) return;

            clicked->setText(newName.trimmed());

            // Update model list
            const QString path = clicked->data(kRolePath).toString();
            for (auto& b : m_customBookmarks) {
                if (b.path == path) {
                    b.label = newName.trimmed();
                    break;
                }
            }
            saveCustomBookmarks();
        });

        QAction* remove = menu.addAction("Remove");
        connect(remove, &QAction::triggered, this, [this, clicked]() {
            const QString label = clicked->text();
            const auto reply = QMessageBox::question(
                this, "Remove bookmark",
                "Remove bookmark \"" + label + "\"?",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply != QMessageBox::Yes) return;

            const QString path = clicked->data(kRolePath).toString();

            // Remove from model list
            for (int i = 0; i < m_customBookmarks.size(); ++i) {
                if (m_customBookmarks[i].path == path) {
                    m_customBookmarks.remove(i);
                    break;
                }
            }
            // Remove from UI
            delete m_sidebar->takeItem(m_sidebar->row(clicked));

            saveCustomBookmarks();
        });
    }

    menu.exec(m_sidebar->viewport()->mapToGlobal(pos));
}
