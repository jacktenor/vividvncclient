#pragma once
#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QRect>
#include <QPixmap>

class VncClient;

class VncView : public QWidget {
    Q_OBJECT
public:
    explicit VncView(QWidget* parent = nullptr);

    void setClient(VncClient* client);

public slots:
    void setFrame(const QImage& img);

protected:
    void paintEvent(QPaintEvent* e) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;

    void focusOutEvent(QFocusEvent* e) override;private:

    bool mapToRemote(const QPoint& widgetPos, int& outX, int& outY) const;
    unsigned int qtKeyToX11Keysym(QKeyEvent* e) const;

private:
    QImage m_frame;
    mutable QMutex m_mutex;

    VncClient* m_client{nullptr};

    // Where we drew the image last paint (for coordinate mapping)
    QRect m_drawRect;

    QPixmap m_placeholder;

    int  m_lastRx = 0;
    int  m_lastRy = 0;
    bool m_haveLast = false;

    // current button states
    bool m_leftDown{false};
    bool m_midDown{false};
    bool m_rightDown{false};
};

