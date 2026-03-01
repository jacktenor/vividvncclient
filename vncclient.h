#pragma once
#include <QObject>
#include <QThread>
#include <QImage>
#include <QString>

enum QualityPreset {
    SharpText = 0,
    Balanced = 1,
    SmoothMotion = 2
};

class VncClient : public QObject {
    Q_OBJECT

public:
    void sendPointerMask(int x, int y, int mask);
    void sendWheelSteps(int x, int y, int stepsY);
    void sendClipboardText(const QString& text);

    explicit VncClient(QObject* parent = nullptr);
    ~VncClient() override;

    void connectToHost(const QString& host, int port, const QString& password, int qualityPreset);
    void connectToHost(const QString& host, int port, const QString& password) {
        connectToHost(host, port, password, 1); // 1 = Balanced preset
    }
    void disconnectFromHost();

    void sendPointerEvent(int x, int y, bool left, bool middle, bool right);
    void sendKeyEvent(uint keysym, bool down);

signals:
    void frameReady(const QImage& frame);
    void status(const QString& msg);
    void connected();
    void disconnected();
    void clipboardTextReceived(const QString& text);

private:
    class Worker;
    QThread m_thread;
    Worker* m_worker{nullptr};
};

