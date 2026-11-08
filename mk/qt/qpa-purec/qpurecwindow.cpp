#include "qpurecwindow.h"
#include "qpurecintegration.h"

#include <qpa/qwindowsysteminterface.h>
#include <QtGui/qwindow.h>

QT_BEGIN_NAMESPACE

QPurecWindow::QPurecWindow(QWindow *window)
    : QPlatformWindow(window)
{
    m_pg = {};
    m_requested = window->geometry();
    if (auto *integration = QPurecIntegration::instance())
        integration->registerWindow(this);
}

QPurecWindow::~QPurecWindow()
{
    closeNative();
    if (auto *integration = QPurecIntegration::instance())
        integration->unregisterWindow(this);
}

bool QPurecWindow::openNative()
{
    auto *integration = QPurecIntegration::instance();
    if (!integration || integration->nativeOwner())
        return false;
    QWindow *w = window();
    if (w->type() == Qt::Popup || w->type() == Qt::ToolTip)
        return false;
    QRect screen = w->screen() ? w->screen()->geometry() : QRect(0, 0, 1024, 768);
    int cw = qMax(w->width(), 160);
    int ch = qMax(w->height(), 96);
    uint32_t fw = uint32_t(cw + PG_WINDOW_BORDER * 2);
    uint32_t fh = uint32_t(ch + PG_TITLEBAR_HEIGHT + PG_WINDOW_BORDER * 2);
    if (int(fw) > screen.width()) fw = uint32_t(screen.width());
    if (int(fh) > screen.height()) fh = uint32_t(screen.height());
    QByteArray title = w->title().toUtf8();
    bool ok;
    if (w->geometry().topLeft() == QPoint(0, 0) && w->flags().testFlag(Qt::MaximizeUsingFullscreenGeometryHint))
        ok = pg_window_init(&m_pg, title.constData(), 0, 0, fw, fh);
    else
        ok = pg_window_center(&m_pg, title.constData(), fw, fh);
    if (!ok)
        return false;
    if (w->flags().testFlag(Qt::CustomizeWindowHint) && !w->flags().testFlag(Qt::WindowCloseButtonHint))
        pg_window_set_closable(&m_pg, false);
    pg_window_set_esc_closes(&m_pg, false);
    m_native = true;
    integration->claimNative(this);
    syncGeometry();
    redrawFrame();
    return true;
}

void QPurecWindow::closeNative()
{
    if (!m_native)
        return;
    pg_window_close(&m_pg);
    m_native = false;
    if (auto *integration = QPurecIntegration::instance())
        integration->unregisterWindow(this);
}

void QPurecWindow::setVisible(bool visible)
{
    if (visible) {
        if (!m_native)
            openNative();
        QRect rect(QPoint(0, 0), geometry().size());
        QWindowSystemInterface::handleExposeEvent(window(), rect);
    } else {
        closeNative();
        QWindowSystemInterface::handleExposeEvent(window(), QRegion());
    }
}

void QPurecWindow::setGeometry(const QRect &rect)
{
    m_requested = rect;
    QPlatformWindow::setGeometry(rect);
}

void QPurecWindow::setWindowTitle(const QString &title)
{
    if (!m_native)
        return;
    QByteArray bytes = title.toUtf8();
    qstrncpy(m_pg.title, bytes.constData(), sizeof(m_pg.title));
    redrawFrame();
}

void QPurecWindow::syncGeometry()
{
    if (!m_native)
        return;
    QRect client(0, 0, int(m_pg.client.width), int(m_pg.client.height));
    if (client.size() != geometry().size() && !client.isEmpty()) {
        QPlatformWindow::setGeometry(QRect(QPoint(0, 0), client.size()));
        QWindowSystemInterface::handleGeometryChange(window(), QRect(QPoint(0, 0), client.size()));
    }
}

void QPurecWindow::redrawFrame()
{
    if (!m_native)
        return;
    pg_window_begin(&m_pg);
    pg_window_end(&m_pg);
    if (!m_last.isNull() && !pg_window_is_minimized(&m_pg)) {
        pg_window_blit(&m_pg, 0, 0, uint32_t(m_last.width()), uint32_t(m_last.height()),
                       reinterpret_cast<const uint32_t *>(m_last.constBits()),
                       uint32_t(m_last.bytesPerLine() / 4));
    }
}

void QPurecWindow::present(const QImage &image, const QRegion &region)
{
    if (!m_native || pg_window_is_minimized(&m_pg) || image.isNull())
        return;
    m_last = image;
    Q_UNUSED(region);
    pg_window_blit(&m_pg, 0, 0, uint32_t(image.width()), uint32_t(image.height()),
                   reinterpret_cast<const uint32_t *>(image.constBits()),
                   uint32_t(image.bytesPerLine() / 4));
}

int QPurecWindow::qtKeyForSpecial(int special)
{
    switch (special) {
    case 1: return Qt::Key_F1;
    case 2: return Qt::Key_F2;
    case 3: return Qt::Key_F3;
    case 4: return Qt::Key_Home;
    case 5: return Qt::Key_End;
    case 6: return Qt::Key_PageUp;
    case 7: return Qt::Key_PageDown;
    case 8: return Qt::Key_Left;
    case 9: return Qt::Key_Right;
    case 10: return Qt::Key_Up;
    case 11: return Qt::Key_Down;
    case 12: return Qt::Key_Delete;
    default: return Qt::Key_unknown;
    }
}

int QPurecWindow::qtKeyForChar(int ch, QString *text, Qt::KeyboardModifiers *mods)
{
    *mods = Qt::NoModifier;
    *text = QString();
    if (ch == '\r' || ch == '\n') {
        *text = QStringLiteral("\r");
        return Qt::Key_Return;
    }
    if (ch == '\b' || ch == 127)
        return Qt::Key_Backspace;
    if (ch == '\t') {
        *text = QStringLiteral("\t");
        return Qt::Key_Tab;
    }
    if (ch == 27)
        return Qt::Key_Escape;
    if (ch > 0 && ch < 27) {
        *mods = Qt::ControlModifier;
        *text = QString(QChar(ch));
        return Qt::Key_A + (ch - 1);
    }
    if (ch >= ' ' && ch < 127) {
        *text = QString(QChar(ch));
        if (ch >= 'a' && ch <= 'z')
            return Qt::Key_A + (ch - 'a');
        if (ch >= 'A' && ch <= 'Z') {
            *mods = Qt::ShiftModifier;
            return Qt::Key_A + (ch - 'A');
        }
        return ch;
    }
    if (ch >= 128) {
        *text = QString(QChar(ch));
        return Qt::Key_unknown;
    }
    return Qt::Key_unknown;
}

bool QPurecWindow::pollOne()
{
    if (!m_native || !pg_window_is_open(&m_pg))
        return false;
    struct pg_event ev;
    if (!pg_window_poll_event(&m_pg, &ev))
        return false;
    QWindow *w = window();
    switch (ev.type) {
    case PG_EVENT_CLOSE:
        m_native = false;
        QWindowSystemInterface::handleCloseEvent(w);
        break;
    case PG_EVENT_REPAINT:
    case PG_EVENT_MOVE:
    case PG_EVENT_MINIMIZE:
        syncGeometry();
        redrawFrame();
        QWindowSystemInterface::handleExposeEvent(w, QRect(QPoint(0, 0), geometry().size()));
        break;
    case PG_EVENT_FOCUS:
        QWindowSystemInterface::handleFocusWindowChanged(w);
        redrawFrame();
        break;
    case PG_EVENT_KEY: {
        QString text;
        Qt::KeyboardModifiers mods;
        int key = qtKeyForChar(ev.key, &text, &mods);
        QWindowSystemInterface::handleKeyEvent(w, QEvent::KeyPress, key, mods, text);
        QWindowSystemInterface::handleKeyEvent(w, QEvent::KeyRelease, key, mods, text);
        break;
    }
    case PG_EVENT_SPECIAL_KEY: {
        int key = qtKeyForSpecial(ev.key);
        QWindowSystemInterface::handleKeyEvent(w, QEvent::KeyPress, key, Qt::NoModifier);
        QWindowSystemInterface::handleKeyEvent(w, QEvent::KeyRelease, key, Qt::NoModifier);
        break;
    }
    case PG_EVENT_MOUSE_MOVE:
    case PG_EVENT_MOUSE_DOWN:
    case PG_EVENT_MOUSE_UP: {
        QPoint local(ev.x - int(m_pg.client.x), ev.y - int(m_pg.client.y));
        QPoint global(ev.x, ev.y);
        if (ev.type == PG_EVENT_MOUSE_DOWN || ev.type == PG_EVENT_MOUSE_UP) {
            int bit = ev.button == 1 ? Qt::LeftButton : ev.button == 2 ? Qt::RightButton : Qt::MiddleButton;
            if (ev.type == PG_EVENT_MOUSE_DOWN)
                m_buttons |= bit;
            else
                m_buttons &= ~bit;
        }
        QWindowSystemInterface::handleMouseEvent(w, local, global, Qt::MouseButtons(m_buttons));
        break;
    }
    default:
        break;
    }
    int32_t wheel = pg_window_take_wheel(&m_pg);
    if (wheel != 0) {
        QPoint pos = m_lastPos;
        QWindowSystemInterface::handleWheelEvent(w, pos, pos, QPoint(0, wheel * 120), QPoint(0, wheel * 120));
    }
    return true;
}

QT_END_NAMESPACE
