#include "applog.h"

#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QMutex>

static QFile* g_file = nullptr;
static QMutex g_mtx;

static QString logPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + "/QtVnc.log";
}

void appLogInit() {
    QMutexLocker lock(&g_mtx);
    if (g_file) return;

    g_file = new QFile(logPath());
    g_file->open(QIODevice::Append | QIODevice::Text);

    QTextStream ts(g_file);
    ts << "\n==== START " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ====\n";
    ts.flush();
    g_file->flush();
}

void appLogLine(const QString& s) {
    QMutexLocker lock(&g_mtx);
    if (!g_file) appLogInit();
    if (!g_file || !g_file->isOpen()) return;

    QTextStream ts(g_file);
    ts << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << "  " << s << "\n";
    ts.flush();
    g_file->flush(); // important if you crash soon after
}
