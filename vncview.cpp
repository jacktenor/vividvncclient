#include "vncview.h"
#include "vncclient.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <Qt>
#include <QDebug>

VncView::VncView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(640, 360);
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);

    setMouseTracking(true);

    setFocus(Qt::OtherFocusReason);

    // Load placeholder image
    m_placeholder = QPixmap(":/icons/panther.png");
}

void VncView::setClient(VncClient* client) {
    m_client = client;
}

void VncView::setFrame(const QImage& img) {
    QMutexLocker locker(&m_mutex);
    m_frame = img;
    update();
}

void VncView::paintEvent(QPaintEvent* e) {
    Q_UNUSED(e);
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    QMutexLocker locker(&m_mutex);

    // No VNC frame yet → draw placeholder
    if (m_frame.isNull()) {
        if (!m_placeholder.isNull()) {
            QSize target = m_placeholder.size();
            target.scale(size() * 0.5, Qt::KeepAspectRatio); // scale to ~50% of view

            QRect r(QPoint(0, 0), target);
            r.moveCenter(rect().center());

            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawPixmap(r, m_placeholder);
        }

        // Optional text under image
        //p.setPen(QColor(180, 180, 180));
        //p.drawText(
        //    QRect(0, rect().center().y() + 80, width(), 30),
        //    Qt::AlignHCenter,
        //    "Beeralator.com"
        //    );
        //return;
    }

    // Draw remote framebuffer
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QSize target = m_frame.size();
    target.scale(size(), Qt::KeepAspectRatio);

    m_drawRect = QRect(QPoint(0, 0), target);
    m_drawRect.moveCenter(rect().center());

    p.drawImage(m_drawRect, m_frame);
}

bool VncView::mapToRemote(const QPoint& widgetPos, int& outX, int& outY) const {
    QMutexLocker locker(&m_mutex);
    if (m_frame.isNull() || m_drawRect.isNull()) return false;

    if (!m_drawRect.contains(widgetPos)) return false;

    const double fx = double(widgetPos.x() - m_drawRect.x()) / double(m_drawRect.width());
    const double fy = double(widgetPos.y() - m_drawRect.y()) / double(m_drawRect.height());

    outX = int(fx * m_frame.width());
    outY = int(fy * m_frame.height());

    // clamp
    if (outX < 0) outX = 0;
    if (outY < 0) outY = 0;
    if (outX >= m_frame.width()) outX = m_frame.width() - 1;
    if (outY >= m_frame.height()) outY = m_frame.height() - 1;

    return true;
}

void VncView::mousePressEvent(QMouseEvent* e)
{
    setFocus(Qt::MouseFocusReason);
    //grabKeyboard();

    if (e->button() == Qt::LeftButton)   m_leftDown = true;
    if (e->button() == Qt::MiddleButton) m_midDown  = true;
    if (e->button() == Qt::RightButton)  m_rightDown = true;

    int rx, ry;
    if (m_client && mapToRemote(e->pos(), rx, ry)) {
        m_client->sendPointerEvent(rx, ry, m_leftDown, m_midDown, m_rightDown);
    }
    e->accept();
}

void VncView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_leftDown = false;
    if (e->button() == Qt::MiddleButton) m_midDown = false;
    if (e->button() == Qt::RightButton) m_rightDown = false;

    int rx, ry;
    if (m_client && mapToRemote(e->pos(), rx, ry)) {
        m_client->sendPointerEvent(rx, ry, m_leftDown, m_midDown, m_rightDown);
    }
}

void VncView::mouseMoveEvent(QMouseEvent* e) {
    int rx, ry;
    if (m_client && mapToRemote(e->pos(), rx, ry)) {
        m_client->sendPointerEvent(rx, ry, m_leftDown, m_midDown, m_rightDown);
    }
}

void VncView::wheelEvent(QWheelEvent* e)
{
    if (!m_client) { e->ignore(); return; }

    // Qt6: wheel event position in widget coordinates
    const QPoint pos = e->position().toPoint();

    int rx = 0, ry = 0;
    if (!mapToRemote(pos, rx, ry)) {
        e->ignore();
        return;
    }

    int dy = e->angleDelta().y();
    if (dy == 0) dy = e->pixelDelta().y(); // trackpads

    if (dy == 0) { e->accept(); return; }

    int steps = dy / 120;
    if (steps == 0) steps = (dy > 0) ? 1 : -1;

    m_client->sendWheelSteps(rx, ry, steps);
    e->accept();
}

unsigned int VncView::qtKeyToX11Keysym(QKeyEvent* e) const
{
    switch (e->key()) {
    case Qt::Key_Backspace: return 0xff08;
    case Qt::Key_Tab:       return 0xff09;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return 0xff0d;
    case Qt::Key_Escape:    return 0xff1b;
    case Qt::Key_Delete:    return 0xffff;

    case Qt::Key_Left:      return 0xff51;
    case Qt::Key_Up:        return 0xff52;
    case Qt::Key_Right:     return 0xff53;
    case Qt::Key_Down:      return 0xff54;

    case Qt::Key_Home:      return 0xff50;
    case Qt::Key_End:       return 0xff57;
    case Qt::Key_PageUp:    return 0xff55;
    case Qt::Key_PageDown:  return 0xff56;
    case Qt::Key_Insert:    return 0xff63;

    case Qt::Key_Shift:     return 0xffe1;
    case Qt::Key_Control:   return 0xffe3;
    case Qt::Key_Alt:       return 0xffe9;
    case Qt::Key_Meta:      return 0xffeb;
    default:
        break;
    }

    // Do NOT rely on e->text() (often empty on keyRelease)
    const int k = e->key();

    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return (unsigned int)('a' + (k - Qt::Key_A));

    if (k >= Qt::Key_0 && k <= Qt::Key_9)
        return (unsigned int)('0' + (k - Qt::Key_0));

    if (k == Qt::Key_Space)
        return (unsigned int)(' ');

    // ASCII punctuation etc
    if (k >= 0x21 && k <= 0x7E)
        return (unsigned int)k;

    // Last resort: only on KeyPress
    if (e->type() == QEvent::KeyPress) {
        const QString t = e->text();
        if (!t.isEmpty())
            return (unsigned int)t.at(0).unicode();
    }

    return 0;
}

void VncView::keyPressEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) { e->accept(); return; }

    if (!m_client) {
        QWidget::keyPressEvent(e);
        return;
    }

    unsigned int ks = qtKeyToX11Keysym(e);
    if (ks != 0) {
        m_client->sendKeyEvent((uint)ks, true);
    }

    // Always accept so Qt doesn’t steal keys for shortcuts/focus changes
    e->accept();
}

void VncView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) { e->accept(); return; }

    if (!m_client) {
        QWidget::keyReleaseEvent(e);
        return;
    }

    unsigned int ks = qtKeyToX11Keysym(e);
    if (ks != 0) {
        m_client->sendKeyEvent((uint)ks, false);
    }

    e->accept();
}

void VncView::focusInEvent(QFocusEvent* e) {
    QWidget::focusInEvent(e);
    // No-op, but useful hook if we later want to show a “keyboard focus” indicator
}

void VncView::focusOutEvent(QFocusEvent* e)
{
    releaseKeyboard();
    QWidget::focusOutEvent(e);
}

