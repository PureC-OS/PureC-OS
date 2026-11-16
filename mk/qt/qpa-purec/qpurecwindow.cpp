#include "qpurecwindow.h"
#include "qpurecintegration.h"

#include <QtGui/qguiapplication.h>
#include <QtGui/qpainter.h>
#include <QtGui/qwindow.h>
#include <qpa/qwindowsysteminterface.h>

extern "C" {
#include "vb.h"
}

QT_BEGIN_NAMESPACE

static const int TitleHeight = 26;
static const int CloseWidth = 34;

QPurecWindow::QPurecWindow(QWindow *window)
    : QPlatformWindow(window)
{
    m_title = window->title();
}

QPurecWindow::~QPurecWindow()
{
    destroyNative();
}

bool QPurecWindow::isPopup() const
{
    Qt::WindowType type = window()->type();
    return type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen ||
           (window()->flags() & Qt::WindowStaysOnTopHint && window()->flags() & Qt::FramelessWindowHint &&
            window()->transientParent());
}

bool QPurecWindow::wantsDecoration() const
{
    if (isPopup())
        return false;
    if (window()->flags() & Qt::FramelessWindowHint)
        return false;
    if (window()->windowStates() & Qt::WindowFullScreen)
        return false;
    return true;
}

QMargins QPurecWindow::frameMargins() const
{
    return m_decorated ? QMargins(0, TitleHeight, 0, 0) : QMargins();
}

void QPurecWindow::create()
{
    if (m_native)
        return;
    QRect g = geometry();
    if (g.isEmpty())
        g.setSize(QSize(640, 480));
    QPurecScreen *screen = QPurecIntegration::instance()->screen();
    Qt::WindowStates states = window()->windowStates();
    bool fullscreen = states & Qt::WindowFullScreen;
    bool maximized = states & Qt::WindowMaximized;
    m_decorated = wantsDecoration();
    m_titleHeight = m_decorated ? TitleHeight : 0;
    int role = isPopup() ? VB_ROLE_POPUP : VB_ROLE_NORMAL;
    int x = -1, y = -1;
    if (fullscreen) {
        g = screen->geometry();
        x = 0;
        y = 0;
    } else if (maximized) {
        QRect a = screen->availableGeometry();
        g = QRect(a.x(), a.y() + m_titleHeight, a.width(), a.height() - m_titleHeight);
        x = a.x();
        y = a.y();
    } else if (role == VB_ROLE_POPUP || window()->transientParent()) {
        x = g.x();
        y = g.y() - m_titleHeight;
    }
    QByteArray title = m_title.toUtf8();
    m_native = vb_window_create_role(x, y, g.width(), g.height() + m_titleHeight, title.constData(), role);
    if (!m_native) {
        qWarning("purec: cannot create a %dx%d window", g.width(), g.height());
        return;
    }
    QPurecIntegration::instance()->attach(m_native, this);
    int nx = 0, ny = 0;
    vb_window_position(m_native, &nx, &ny);
    QRect actual(nx, ny + m_titleHeight, g.width(), g.height());
    QPlatformWindow::setGeometry(actual);
    QWindowSystemInterface::handleGeometryChange(window(), actual);
    drawDecoration();
    QPurecIntegration::instance()->pumpSoon();
}

void QPurecWindow::destroyNative()
{
    if (!m_native)
        return;
    QPurecIntegration::instance()->detach(m_native);
    vb_window_destroy(m_native);
    m_native = nullptr;
}

void QPurecWindow::setVisible(bool visible)
{
    if (visible) {
        create();
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
        if (!isPopup())
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
    } else {
        destroyNative();
        QWindowSystemInterface::handleExposeEvent(window(), QRegion());
    }
    QWindowSystemInterface::flushWindowSystemEvents();
}

void QPurecWindow::setGeometry(const QRect &rect)
{
    QRect old = geometry();
    QPlatformWindow::setGeometry(rect);
    if (m_native && rect.size() != old.size()) {
        destroyNative();
        create();
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
        return;
    }
    QWindowSystemInterface::handleGeometryChange(window(), rect);
}

void QPurecWindow::setWindowState(Qt::WindowStates state)
{
    QPlatformWindow::setWindowState(state);
    if (m_native) {
        destroyNative();
        create();
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
    }
}

void QPurecWindow::setWindowTitle(const QString &title)
{
    m_title = title;
    if (m_native) {
        vb_window_title(m_native, title.toUtf8().constData());
        drawDecoration();
    }
}

void QPurecWindow::requestActivateWindow()
{
    if (m_native && !isPopup())
        vb_window_raise(m_native);
}

void QPurecWindow::raise()
{
    if (m_native && !isPopup())
        vb_window_raise(m_native);
}

void QPurecWindow::drawDecoration()
{
    if (!m_native || !m_decorated)
        return;
    int w = vb_window_width(m_native);
    QImage strip(reinterpret_cast<uchar *>(vb_window_pixels(m_native)), w, TitleHeight, w * 4,
                 QImage::Format_RGB32);
    QPainter p(&strip);
    QColor bar = m_focused ? QColor(0x2E, 0x3A, 0x55) : QColor(0x26, 0x28, 0x30);
    p.fillRect(strip.rect(), bar);
    p.fillRect(QRect(0, TitleHeight - 1, w, 1), m_focused ? QColor(0x5B, 0x8D, 0xEF) : QColor(0x3A, 0x3F, 0x4B));
    QRect close(w - CloseWidth, 0, CloseWidth, TitleHeight - 1);
    if (m_closeHover)
        p.fillRect(close, QColor(0xC8, 0x40, 0x40));
    p.setPen(QColor(0xE6, 0xE6, 0xE6));
    p.setRenderHint(QPainter::Antialiasing);
    QPointF c = QRectF(close).center();
    p.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5));
    p.drawLine(c + QPointF(-5, 5), c + QPointF(5, -5));
    QFont font = QGuiApplication::font();
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    p.setPen(m_focused ? QColor(0xF0, 0xF0, 0xF0) : QColor(0xA0, 0xA4, 0xAC));
    p.drawText(QRect(10, 0, w - CloseWidth - 20, TitleHeight - 1), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(m_title, Qt::ElideRight, w - CloseWidth - 20));
    p.end();
    vb_window_damage(m_native, 0, 0, w, TitleHeight);
}

void QPurecWindow::present(const QImage &image, const QRegion &region)
{
    if (!m_native)
        return;
    uint32_t *dst = vb_window_pixels(m_native);
    int w = vb_window_width(m_native), h = vb_window_height(m_native) - m_titleHeight;
    QRect bounds;
    for (const QRect &r0 : region) {
        QRect r = r0.intersected(QRect(0, 0, qMin(w, image.width()), qMin(h, image.height())));
        if (r.isEmpty())
            continue;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const uint32_t *src = reinterpret_cast<const uint32_t *>(image.constScanLine(y)) + r.left();
            memcpy(dst + size_t(y + m_titleHeight) * size_t(w) + r.left(), src, size_t(r.width()) * 4);
        }
        bounds |= r;
    }
    if (!bounds.isEmpty())
        vb_window_damage(m_native, bounds.x(), bounds.y() + m_titleHeight, bounds.width(), bounds.height());
}

void QPurecWindow::syncGeometryFromNative()
{
    int x = 0, y = 0;
    vb_window_position(m_native, &x, &y);
    QRect g(QPoint(x, y + m_titleHeight), geometry().size());
    QPlatformWindow::setGeometry(g);
    QWindowSystemInterface::handleGeometryChange(window(), g);
}

static Qt::MouseButtons qtButtons(uint32_t b)
{
    Qt::MouseButtons out;
    if (b & VB_BUTTON_LEFT)
        out |= Qt::LeftButton;
    if (b & VB_BUTTON_RIGHT)
        out |= Qt::RightButton;
    if (b & VB_BUTTON_MIDDLE)
        out |= Qt::MiddleButton;
    return out;
}

static Qt::KeyboardModifiers qtModifiers(uint8_t mods)
{
    Qt::KeyboardModifiers out;
    if (mods & VB_MOD_SHIFT)
        out |= Qt::ShiftModifier;
    if (mods & VB_MOD_CTRL)
        out |= Qt::ControlModifier;
    if (mods & VB_MOD_ALT)
        out |= Qt::AltModifier;
    if (mods & VB_MOD_GUI)
        out |= Qt::MetaModifier;
    return out;
}

void QPurecWindow::pointer(const vb_event &e)
{
    QPoint local(e.x, e.y - m_titleHeight);
    if (m_decorated && e.y < m_titleHeight && !(m_buttons & Qt::LeftButton)) {
        int w = vb_window_width(m_native);
        bool overClose = e.x >= w - CloseWidth;
        if (overClose != m_closeHover) {
            m_closeHover = overClose;
            drawDecoration();
        }
        if ((e.changed & e.buttons & VB_BUTTON_LEFT)) {
            if (overClose)
                QWindowSystemInterface::handleCloseEvent(window());
            else
                vb_window_move(m_native);
        }
        return;
    }
    if (m_closeHover) {
        m_closeHover = false;
        drawDecoration();
    }
    QPoint global = geometry().topLeft() + local;
    Qt::MouseButtons buttons = qtButtons(e.buttons);
    Qt::MouseButtons changed = qtButtons(e.changed);
    Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    if (e.wheel) {
        QWindowSystemInterface::handleWheelEvent(window(), local, global, QPoint(), QPoint(0, -e.wheel * 120), mods);
    }
    if (!changed) {
        QWindowSystemInterface::handleMouseEvent(window(), local, global, buttons, Qt::NoButton, QEvent::MouseMove,
                                                 mods);
    } else {
        const Qt::MouseButton order[3] = {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton};
        for (Qt::MouseButton b : order) {
            if (!(changed & b))
                continue;
            QEvent::Type type = (buttons & b) ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
            QWindowSystemInterface::handleMouseEvent(window(), local, global, buttons, b, type, mods);
        }
    }
    m_buttons = buttons;
    m_lastLocal = local;
}

static int hidToQt(int usage, bool shift, QString *text)
{
    static const char lower[] = "abcdefghijklmnopqrstuvwxyz1234567890\r\x1b\b\t -=[]\\#;'`,./";
    static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1b\b\t _+{}|~:\"~<>?";
    if (usage >= 0x04 && usage <= 0x38) {
        char ch = (shift ? upper : lower)[usage - 0x04];
        switch (usage) {
        case 0x28: *text = QStringLiteral("\r"); return Qt::Key_Return;
        case 0x29: *text = QString(QChar(0x1b)); return Qt::Key_Escape;
        case 0x2A: *text = QString(QChar(0x08)); return Qt::Key_Backspace;
        case 0x2B: *text = QStringLiteral("\t"); return shift ? Qt::Key_Backtab : Qt::Key_Tab;
        default: break;
        }
        *text = QString(QChar(ch));
        if (ch >= 'a' && ch <= 'z')
            return Qt::Key_A + (ch - 'a');
        return ch;
    }
    if (usage >= 0x3A && usage <= 0x45)
        return Qt::Key_F1 + (usage - 0x3A);
    switch (usage) {
    case 0x49: return Qt::Key_Insert;
    case 0x4A: return Qt::Key_Home;
    case 0x4B: return Qt::Key_PageUp;
    case 0x4C: *text = QString(QChar(0x7f)); return Qt::Key_Delete;
    case 0x4D: return Qt::Key_End;
    case 0x4E: return Qt::Key_PageDown;
    case 0x4F: return Qt::Key_Right;
    case 0x50: return Qt::Key_Left;
    case 0x51: return Qt::Key_Down;
    case 0x52: return Qt::Key_Up;
    case 0x54: *text = QStringLiteral("/"); return Qt::Key_Slash;
    case 0x55: *text = QStringLiteral("*"); return Qt::Key_Asterisk;
    case 0x56: *text = QStringLiteral("-"); return Qt::Key_Minus;
    case 0x57: *text = QStringLiteral("+"); return Qt::Key_Plus;
    case 0x58: *text = QStringLiteral("\r"); return Qt::Key_Enter;
    case 0x62: *text = QStringLiteral("0"); return Qt::Key_0;
    case 0x63: *text = QStringLiteral("."); return Qt::Key_Period;
    case 0xE0: case 0xE4: return Qt::Key_Control;
    case 0xE1: case 0xE5: return Qt::Key_Shift;
    case 0xE2: return Qt::Key_Alt;
    case 0xE6: return Qt::Key_AltGr;
    case 0xE3: case 0xE7: return Qt::Key_Meta;
    default: break;
    }
    if (usage >= 0x59 && usage <= 0x61) {
        *text = QString(QChar('1' + (usage - 0x59)));
        return Qt::Key_1 + (usage - 0x59);
    }
    return 0;
}

void QPurecWindow::key(const vb_event &e)
{
    Qt::KeyboardModifiers mods = qtModifiers(e.mods);
    QString text;
    int qtKey = hidToQt(e.key, mods & Qt::ShiftModifier, &text);
    if (!qtKey)
        return;
    if ((mods & Qt::ControlModifier) && text.size() == 1) {
        QChar c = text.at(0);
        if (c.isLetter())
            text = QString(QChar(c.toUpper().unicode() - '@'));
    }
    QEvent::Type type = e.pressed ? QEvent::KeyPress : QEvent::KeyRelease;
    QWindowSystemInterface::handleExtendedKeyEvent(window(), type, qtKey, mods, quint32(e.key), quint32(e.key),
                                                   quint32(e.mods), e.pressed ? text : QString(), e.repeat);
}

void QPurecWindow::handleEvent(const vb_event &e)
{
    switch (e.type) {
    case VB_EVENT_POINTER:
        pointer(e);
        break;
    case VB_EVENT_ENTER:
        QWindowSystemInterface::handleEnterEvent(window(), QPointF(e.x, e.y - m_titleHeight),
                                                 QPointF(geometry().topLeft() + QPoint(e.x, e.y - m_titleHeight)));
        break;
    case VB_EVENT_LEAVE:
        if (m_closeHover) {
            m_closeHover = false;
            drawDecoration();
        }
        QWindowSystemInterface::handleLeaveEvent(window());
        break;
    case VB_EVENT_KEY:
        key(e);
        break;
    case VB_EVENT_FOCUS:
        m_focused = e.focused;
        drawDecoration();
        if (e.focused)
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
        else if (QGuiApplication::focusWindow() == window())
            QWindowSystemInterface::handleFocusWindowChanged(nullptr, Qt::ActiveWindowFocusReason);
        break;
    case VB_EVENT_CLOSE:
        QWindowSystemInterface::handleCloseEvent(window());
        break;
    case VB_EVENT_POPUP_DONE:
        QWindowSystemInterface::handleCloseEvent(window());
        break;
    case VB_EVENT_CONFIGURE:
        syncGeometryFromNative();
        break;
    default:
        break;
    }
}

QT_END_NAMESPACE
