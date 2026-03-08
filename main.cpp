#include "mainwindow.h"
#include "applog.h"
#include <signal.h>

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QColor>
#include <QStyleFactory>
#ifndef Q_OS_WIN
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

static void applyDarkPalette(QApplication& app)
{
    // Optional: Fusion style is consistent and respects palettes well
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;

    p.setColor(QPalette::Window, QColor(35, 35, 35));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Base, QColor(35, 35, 35));
    p.setColor(QPalette::AlternateBase, QColor(20, 20, 20));
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Button, QColor(45, 45, 45));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::BrightText, Qt::red);

    p.setColor(QPalette::Highlight, QColor(90, 140, 255));
    p.setColor(QPalette::HighlightedText, Qt::black);

    // Links
    p.setColor(QPalette::Link, QColor(90, 140, 255));
    p.setColor(QPalette::LinkVisited, QColor(180, 120, 255));

    // Disabled colors
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 140, 140));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(140, 140, 140));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 140, 140));

    app.setPalette(p);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    applyDarkPalette(app);
    appLogInit();

    app.setWindowIcon(QIcon(":/icons/appicon.png")); // from resources (.qrc)

    MainWindow w;
    w.show();

#ifndef Q_OS_WIN
    // Unix only: prevents hard crash on socket write after disconnect
    signal(SIGPIPE, SIG_IGN);
#else
    // Windows only: initialize Winsock (safe even if not strictly required)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif


#ifdef Q_OS_WIN
    WSACleanup();
#endif
    return app.exec();
}

