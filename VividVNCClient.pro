QT += core gui widgets network
CONFIG += c++17

TEMPLATE = app
TARGET = VividVNCServer

SOURCES += \
    applog.cpp \
    main.cpp \
    mainwindow.cpp \
    remotebrowserdialog.cpp \
    sftplistworker.cpp \
    sftptransferworker.cpp \
    sshkeybootstrap.cpp \
    sshtunnel.cpp \
    vncclient.cpp \
    vncview.cpp

HEADERS += \
    applog.h \
    mainwindow.h \
    remotebrowserdialog.h \
    sftplistworker.h \
    sftptransferworker.h \
    sshkeybootstrap.h \
    sshtunnel.h \
    vncclient.h \
    vncview.h

contains(QT_CONFIG, dbus) {
    QT += dbus
}

# ---- Platform-specific configuration ----
unix:!win32 {
    INCLUDEPATH += /usr/include
    LIBS += -lvncclient
    LIBS += -lqt6keychain -lssh2

    QT_LIBDIR = /home/<USERNAME>/dev/qt/Qt/6.10.2/gcc_64/lib

    # Force old-style DT_RPATH (searched BEFORE system dirs), not DT_RUNPATH
    QMAKE_LFLAGS += -Wl,--disable-new-dtags
    QMAKE_LFLAGS += -Wl,-rpath,$$QT_LIBDIR
}
win32 {
    MXE = /home/<USERNAME>/dev/mxe
    TRIP = x86_64-w64-mingw32.static
    PREFIX = $$MXE/usr/$$TRIP

    INCLUDEPATH += $$PREFIX/include
    LIBS += -L$$PREFIX/lib


    # Windows MXE cross-compile build
    INCLUDEPATH += /home/<USERNAME>/dev/mxe/usr/x86_64-w64-mingw32.static/include
    INCLUDEPATH += /home/<USERNAME>/dev/mxe/usr/x86_64-w64-mingw32.static/include/qt6keychain
    LIBS += -L/home/<USERNAME>/dev/mxe/usr/x86_64-w64-mingw32.static/lib

    # Main libraries
    LIBS += -lvncclient -lvncserver -lz
    LIBS += -llzo2
    LIBS += -lssh2 -lws2_32

    # Crypto stack with explicit full paths to force proper linking
    LIBS += -lgnutls
    LIBS += -lidn2
    LIBS += -lunistring
    LIBS += /home/<USERNAME>/dev/mxe/usr/x86_64-w64-mingw32.static/lib/libnettle.a

    LIBS += /home/<USERNAME>/dev/mxe/usr/x86_64-w64-mingw32.static/lib/libhogweed.a
    LIBS += -lgmp -ltasn1
    LIBS += -lssl -lcrypto

    LIBS += -llzo2
    LIBS += -lbrotlidec -lbrotlienc -lbrotlicommon
    LIBS += -liconv

    LIBS += -lqt6keychain

# Windows system libraries
    LIBS += -lws2_32 -lgdi32 -lcrypt32 -lncrypt -lbcrypt -liphlpapi -luserenv -ladvapi32 -lole32 -lshell32 -luuid
}

RESOURCES += \
    resources.qrc

RC_FILE = appicon.rc

DISTFILES += \
    appicon.rc
