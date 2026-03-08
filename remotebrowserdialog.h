#pragma once

#include <QDialog>
#include <QThread>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QSplitter>
#include <QListWidget>
#include <QStack>
#include <QHBoxLayout>
#include <QEvent>

#include "sftplistworker.h"

// Item data roles
static constexpr int kRolePath     = Qt::UserRole;
static constexpr int kRoleIsDir    = Qt::UserRole + 1;
static constexpr int kRoleIsCustom = Qt::UserRole + 20;

class RemoteBrowserDialog : public QDialog {
    Q_OBJECT
public:
    explicit RemoteBrowserDialog(QWidget* parent = nullptr);
    ~RemoteBrowserDialog() override;

    void setConnectionParams(const QString& host, int port,
                             const QString& user,
                             const QString& keyPath,
                             const QString& passwordFallback);

    void setStartDir(const QString& dir);
    QString selectedRemotePath() const;

protected:
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

protected slots:
    void toggleHiddenFiles();

private slots:
    void refresh();
    void goUp();
    void goBack();
    void goForward();
    void onItemActivated(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();
    void onListed(const QString& dir, const QVector<RemoteEntry>& entries);
    void onError(const QString& msg);
    void acceptSelection();
    void onSidebarItemClicked(QListWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    void showSidebarContextMenu(const QPoint& pos);

private:
    QString joinPath(const QString& dir, const QString& name) const;
    void navigateTo(const QString& dir, bool addToHistory = true);
    void rebuildBreadcrumbs(const QString& dir);
    QIcon iconForEntry(const RemoteEntry& e) const;
    QString humanSize(quint64 bytes) const;

    // Persistent bookmarks
    QString makeBookmarkSettingsKey() const;
    void updateBuiltinBookmarks();
    void clearCustomBookmarkItems();
    void loadCustomBookmarks();
    void saveCustomBookmarks() const;
    void addCustomBookmark(const QString& label, const QString& path);

    // Toolbar
    QToolButton*  m_btnBack    = nullptr;
    QToolButton*  m_btnForward = nullptr;
    QToolButton*  m_btnUp      = nullptr;
    QToolButton*  m_btnRefresh = nullptr;

    // Breadcrumb / path area
    QWidget*      m_breadcrumbBar    = nullptr;
    QHBoxLayout*  m_breadcrumbLayout = nullptr;
    QLineEdit*    m_dir              = nullptr;

    // Main content
    QSplitter*    m_splitter = nullptr;
    QListWidget*  m_sidebar  = nullptr;

    // Built-in bookmark items
    QListWidgetItem* m_bmRoot = nullptr;
    QListWidgetItem* m_bmHome = nullptr;
    QListWidgetItem* m_bmTmp = nullptr;
    QListWidgetItem* m_bmDownloads = nullptr;

    struct CustomBookmark { QString label; QString path; };
    QVector<CustomBookmark> m_customBookmarks;
    QTreeWidget*  m_tree     = nullptr;

    // Bottom bar
    QLabel*       m_status  = nullptr;
    QPushButton*  m_select  = nullptr;
    QPushButton*  m_cancel  = nullptr;

    // Navigation state
    QString         m_selected;
    QString         m_currentDir;
    QStack<QString> m_backStack;
    QStack<QString> m_forwardStack;

    // Worker
    QThread*        m_thread = nullptr;
    SftpListWorker* m_worker = nullptr;

    QString m_host, m_user, m_key, m_pass;
    int     m_port = 22;

    bool m_autoRefreshedOnce = false;

    void populateTree(const QString& dir, const QVector<RemoteEntry>& entries);
    bool                 m_showHidden  = false;
    QToolButton*         m_btnHidden   = nullptr;
    QVector<RemoteEntry> m_lastEntries;
};
