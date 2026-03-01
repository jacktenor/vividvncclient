#include "vncclient.h"

#include <QTimer>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QByteArray>
#include <cstring>
#include <cstdlib>
#include "applog.h"
#include <QString>
#include <QDebug>

static int g_vncClientTag = 0;

extern "C" {
#include <rfb/rfbclient.h>
}

class VncClient::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(QObject* parent = nullptr) : QObject(parent) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(5);
        connect(m_pollTimer, &QTimer::timeout, this, &Worker::pollOnce);
    }

    ~Worker() override {
        disconnectNow();
    }

public slots:
    void connectNow(QString host, int port, QString password, int qualityPreset)
    {

        appLogLine(QString("WORKER connectNow host=%1 port=%2 quality=%3")
                       .arg(m_host).arg(m_port).arg(qualityPreset));

        m_host = std::move(host);
        m_port = port;
        m_password = std::move(password);

        emit status("Initializing VNC client...");

        m_client = rfbGetClient(8, 3, 4);
        if (!m_client) {
            emit status("Failed to create VNC client.");
            emit frameReady(QImage());
            emit disconnected();
            return;
        }

        // IMPORTANT: set client-data + callbacks BEFORE init
        rfbClientSetClientData(m_client, &g_vncClientTag, this);
        m_client->GetPassword = &Worker::cbGetPassword;
        m_client->GotFrameBufferUpdate = &Worker::cbUpdate;

        m_client->GotXCutText = &Worker::cbGotCutText;

        // IMPORTANT: set host/port BEFORE init
        QByteArray h = m_host.toUtf8();
        m_client->serverHost = strdup(h.constData());
        m_client->serverPort = m_port;

        // Request 32bpp truecolor
        m_client->format.depth = 24;
        m_client->format.bitsPerPixel = 32;
        m_client->format.trueColour = 1;
        m_client->format.redShift = 16;
        m_client->format.greenShift = 8;
        m_client->format.blueShift = 0;
        m_client->format.redMax = 255;
        m_client->format.greenMax = 255;
        m_client->format.blueMax = 255;

        appLogLine("WORKER calling rfbInitClient...");

        // Encodings / quality
        // NOW init (only once)
        if (!rfbInitClient(m_client, nullptr, nullptr)) {
            emit status("Failed to connect (check host/port/password).");

            // cleanup safely
            rfbClientSetClientData(m_client, &g_vncClientTag, nullptr);
            m_client->GotFrameBufferUpdate = nullptr;
            m_client->GetPassword = nullptr;

            rfbClientCleanup(m_client);
            m_client = nullptr;

            appLogLine("WORKER rfbInitClient FAILED");

            m_backBuffer = QImage();
            emit frameReady(QImage());
            emit disconnected();
            return;
        }
        appLogLine("WORKER rfbInitClient1 OK");

        ensureBackBuffer(m_client->width, m_client->height);

        appLogLine("WORKER rfbInitClient2 OK");

        emit status("Connected.");
        emit connected();

        appLogLine("WORKER rfbInitClient3 OK");

        // Kickstart (full frame once)
        m_requestInFlight = true;
        m_reqIntervalMs = 16;      // request updates ~60Hz for responsiveness
        m_reqTimer.invalidate();   // restart timing logic
        SendFramebufferUpdateRequest(m_client, 0, 0, m_client->width, m_client->height, FALSE);

        // Poll in worker thread
        m_pollTimer->start();

    }

    void disconnectNow() {

        appLogLine("WORKER disconnectNow()");

        if (m_pollTimer) m_pollTimer->stop();

        if (m_client) {
            // Prevent any future callbacks from resolving "this"
            rfbClientSetClientData(m_client, &g_vncClientTag, nullptr);
            m_client->GotFrameBufferUpdate = nullptr;
            m_client->GetPassword = nullptr;

            m_client->GotXCutText = nullptr;

            rfbClientCleanup(m_client);
            m_client = nullptr;
        }

        m_backBuffer = QImage();
        emit frameReady(QImage());   // <-- clears the view immediately
        emit disconnected();
    }

    void sendPointer(int x, int y, int mask) {
        if (!m_client) return;
        SendPointerEvent(m_client, x, y, mask);
    }

   void sendKey(uint keysym, bool down) {
        if (!m_client) return;
        SendKeyEvent(m_client, keysym, down ? TRUE : FALSE);
    }

    void sendClipboardText(QString text) {
        if (!m_client) return;
        QByteArray utf8 = text.toUtf8();
        SendClientCutText(m_client, utf8.data(), utf8.size());
    }

    void pollOnce() {
        if (!m_client) return;

        int rc = WaitForMessage(m_client, 10); // wait up to 10ms
        if (rc < 0) {
            emit status("Connection closed.");
            disconnectNow();
            return;
        }

        if (rc > 0) {
            if (!HandleRFBServerMessage(m_client)) {
                emit status("Server message handling failed / disconnected.");
                disconnectNow();
                return;
            }
        }


        // If we're connected and no request is currently "in flight", request another update.
        // This keeps responsiveness high even if UI FPS is lower (we can drop frames later).
        if (m_client && !m_requestInFlight) {
            if (!m_reqTimer.isValid()) m_reqTimer.start();

            // Request cadence: ~60Hz (16ms). You can tune this.
            if (m_reqTimer.elapsed() >= m_reqIntervalMs) {
                m_reqTimer.restart();
                m_requestInFlight = true;
                SendFramebufferUpdateRequest(m_client, 0, 0, m_client->width, m_client->height, TRUE);
            }
        }
    }

private:
    static char* cbGetPassword(rfbClient* client) {
        auto* self = static_cast<Worker*>(rfbClientGetClientData(client, &g_vncClientTag));
        if (!self) return nullptr;

        QByteArray utf8 = self->m_password.toUtf8();
        char* out = (char*)malloc((size_t)utf8.size() + 1);
        if (!out) return nullptr;
        memcpy(out, utf8.constData(), (size_t)utf8.size());
        out[utf8.size()] = '\0';
        return out;
    }

    static void cbUpdate(rfbClient* client, int x, int y, int w, int h) {
        auto* self = static_cast<Worker*>(rfbClientGetClientData(client, &g_vncClientTag));
        if (!self) return;

        self->blitRectOpaque(x, y, w, h);

        // Mark that the last request was satisfied; pollOnce() will request the next one.
        self->m_requestInFlight = false;

        self->emitThrottledFrame();
    }


    void ensureBackBuffer(int w, int h) {
        if (w <= 0 || h <= 0) return;
        if (!m_backBuffer.isNull() && m_backBuffer.width() == w && m_backBuffer.height() == h) return;

        m_backBuffer = QImage(w, h, QImage::Format_ARGB32);
        m_backBuffer.fill(Qt::black);
    }

    void blitRectOpaque(int x, int y, int w, int h) {
        if (!m_client || !m_client->frameBuffer) return;

        const int fbW = m_client->width;
        const int fbH = m_client->height;
        if (fbW <= 0 || fbH <= 0) return;

        // Clamp
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > fbW) w = fbW - x;
        if (y + h > fbH) h = fbH - y;
        if (w <= 0 || h <= 0) return;

        ensureBackBuffer(fbW, fbH);

        const int stride = fbW * 4;

        for (int row = 0; row < h; ++row) {
            const uchar* src = (const uchar*)m_client->frameBuffer + (y + row) * stride + x * 4;
            uchar* dst = m_backBuffer.scanLine(y + row) + x * 4;

            memcpy(dst, src, (size_t)w * 4);

            // Force alpha = 0xFF to avoid “random alpha makes black blocks”
            quint32* d32 = reinterpret_cast<quint32*>(dst);
            for (int col = 0; col < w; ++col) {
                d32[col] |= 0xFF000000u;
            }
        }
    }

    void emitThrottledFrame() {
        if (!m_emitTimer.isValid()) m_emitTimer.start();
        if (m_emitTimer.elapsed() < m_minEmitMs) return;
        m_emitTimer.restart();

        if (!m_backBuffer.isNull()) {
            emit frameReady(m_backBuffer.copy());
        }
    }

signals:
    void frameReady(const QImage& frame);
    void status(const QString& msg);
    void connected();
    void disconnected();
    void clipboardTextReceived(const QString& text);
\
private:
    static void cbGotCutText(rfbClient* client, const char* text, int textlen)
    {
        auto* self = static_cast<Worker*>(rfbClientGetClientData(client, &g_vncClientTag));
        if (!self || !text || textlen <= 0) return;

        // Try UTF-8 first (most servers send UTF-8 even via the old API)
        QString s = QString::fromUtf8(text, textlen);

        // If decoding looks broken, fall back to Latin1
        if (s.contains(QChar::ReplacementCharacter)) {
            s = QString::fromLatin1(text, textlen);
        }

        emit self->clipboardTextReceived(s);
    }


    QTimer* m_pollTimer{nullptr};
    rfbClient* m_client{nullptr};

    QString m_host;
    int m_port{5900};
    QString m_password;

    QImage m_backBuffer;

    QElapsedTimer m_emitTimer;
    int m_minEmitMs{16};

    QElapsedTimer m_reqTimer;
    int m_reqIntervalMs{16};        // request cadence (~60Hz)
    bool m_requestInFlight{false};  // true after we request until we receive an update

};

VncClient::VncClient(QObject* parent) : QObject(parent) {
    m_worker = new Worker();
    m_worker->moveToThread(&m_thread);

    // Forward worker signals to GUI
    connect(m_worker, &Worker::frameReady, this, &VncClient::frameReady, Qt::QueuedConnection);
    connect(m_worker, &Worker::status, this, &VncClient::status, Qt::QueuedConnection);
    connect(m_worker, &Worker::connected, this, &VncClient::connected, Qt::QueuedConnection);
    connect(m_worker, &Worker::disconnected, this, &VncClient::disconnected, Qt::QueuedConnection);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &Worker::clipboardTextReceived, this, &VncClient::clipboardTextReceived, Qt::QueuedConnection);
    m_thread.start();
}

VncClient::~VncClient() {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "disconnectNow", Qt::BlockingQueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
}

void VncClient::connectToHost(const QString& host, int port, const QString& password, int qualityPreset) {
    if (!m_worker) return;
    QMetaObject::invokeMethod(
        m_worker, "connectNow", Qt::QueuedConnection,
        Q_ARG(QString, host),
        Q_ARG(int, port),
        Q_ARG(QString, password),
        Q_ARG(int, qualityPreset)
        );
}


void VncClient::disconnectFromHost() {
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "disconnectNow", Qt::QueuedConnection);
}


void VncClient::sendPointerEvent(int x, int y, bool left, bool middle, bool right) {
    if (!m_worker) return;

    int mask = 0;
    if (left)   mask |= 1;
    if (middle) mask |= 2;
    if (right)  mask |= 4;

    QMetaObject::invokeMethod(
        m_worker,
        "sendPointer",
        Qt::QueuedConnection,
        Q_ARG(int, x),
        Q_ARG(int, y),
        Q_ARG(int, mask)
        );
}

void VncClient::sendPointerMask(int x, int y, int mask)
{
    if (!m_worker) return;

    QMetaObject::invokeMethod(
        m_worker,
        "sendPointer",
        Qt::QueuedConnection,
        Q_ARG(int, x),
        Q_ARG(int, y),
        Q_ARG(int, mask)
    );
}

void VncClient::sendWheelSteps(int x, int y, int stepsY)
{
    if (!m_worker) return;
    if (stepsY == 0) return;

    // VNC pointer mask:
    // 1=left, 2=middle, 4=right, 8=wheel up (button4), 16=wheel down (button5)
    const int mask = (stepsY > 0) ? 8 : 16;
    int count = (stepsY > 0) ? stepsY : -stepsY;

    // Safety cap (trackpads can generate huge deltas)
    if (count > 20) count = 20;

    for (int i = 0; i < count; ++i) {
        sendPointerMask(x, y, mask); // press
        sendPointerMask(x, y, 0);    // release
    }
}


void VncClient::sendKeyEvent(uint keysym, bool down) {
    if (!m_worker) return;

    bool ok = QMetaObject::invokeMethod(
        m_worker,
        "sendKey",
        Qt::QueuedConnection,
        Q_ARG(uint, keysym),
        Q_ARG(bool, down)
        );

    if (!ok) {
        appLogLine("invokeMethod(sendKey) FAILED (check slot signature / Q_OBJECT / moc)");
    }
}

void VncClient::sendClipboardText(const QString& text)
{
    if (!m_worker) return;

    QMetaObject::invokeMethod(
        m_worker,
        "sendClipboardText",
        Qt::QueuedConnection,
        Q_ARG(QString, text)
        );
}

#include "vncclient.moc"
