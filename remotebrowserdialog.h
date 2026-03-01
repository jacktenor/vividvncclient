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

private:
    QString joinPath(const QString& dir, const QString& name) const;
    void navigateTo(const QString& dir, bool addToHistory = true);
    void rebuildBreadcrumbs(const QString& dir);
    QIcon iconForEntry(const RemoteEntry& e) const;
    QString humanSize(quint64 bytes) const;

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
