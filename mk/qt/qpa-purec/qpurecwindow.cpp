#include "qpurecwindow.h"
#include "qpurecintegration.h"

#include <QtGui/qguiapplication.h>
#include <QtGui/qpainter.h>
#include <QtGui/qwindow.h>
#include <QtCore/qelapsedtimer.h>
#include <qpa/qwindowsysteminterface.h>

extern "C" {
#include "vb.h"
}

QT_BEGIN_NAMESPACE

static const int TitleHeight = 34;
static const int ButtonWidth = 46;
static const int Border = 1;
static const int ResizeMargin = 6;

static qint64 monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

static Qt::WindowStates qtStates(int state)
{
    switch (state) {
    case VB_STATE_MAXIMIZED: return Qt::WindowMaximized;
    case VB_STATE_FULLSCREEN: return Qt::WindowFullScreen;
    case VB_STATE_MINIMIZED: return Qt::WindowMinimized;
    default: return Qt::WindowNoState;
    }
}

static int vbState(Qt::WindowStates states)
{
    if (states & Qt::WindowMinimized)
        return VB_STATE_MINIMIZED;
    if (states & Qt::WindowFullScreen)
        return VB_STATE_FULLSCREEN;
    if (states & Qt::WindowMaximized)
        return VB_STATE_MAXIMIZED;
    return VB_STATE_NORMAL;
}

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
    return true;
}

bool QPurecWindow::resizable() const
{
    return window()->minimumSize() != window()->maximumSize();
}

QMargins QPurecWindow::margins() const
{
    if (!m_decorated || m_state == VB_STATE_FULLSCREEN)
        return QMargins();
    if (m_state == VB_STATE_MAXIMIZED)
        return QMargins(0, TitleHeight, 0, 0);
    return QMargins(Border, Border + TitleHeight, Border, Border);
}

QMargins QPurecWindow::frameMargins() const
{
    return margins();
}

QSize QPurecWindow::nativeSize() const
{
    if (!m_native)
        return QSize();
    return QSize(vb_window_width(m_native), vb_window_height(m_native));
}

QRect QPurecWindow::contentRect() const
{
    QMargins m = margins();
    QSize n = nativeSize();
    return QRect(m.left(), m.top(), n.width() - m.left() - m.right(), n.height() - m.top() - m.bottom());
}

QRect QPurecWindow::buttonRect(Part part) const
{
    QMargins m = margins();
    int right = nativeSize().width() - m.right();
    int top = m.top() - TitleHeight;
    int index = 0;
    if (part == PartClose)
        index = 1;
    else if (part == PartMaximize)
        index = 2;
    else if (part == PartMinimize)
        index = resizable() ? 3 : 2;
    else
        return QRect();
    if (part == PartMaximize && !resizable())
        return QRect();
    return QRect(right - index * ButtonWidth, top, ButtonWidth, TitleHeight);
}

QPurecWindow::Part QPurecWindow::partAt(const QPoint &p) const
{
    if (!m_decorated || m_state == VB_STATE_FULLSCREEN)
        return PartNone;
    QMargins m = margins();
    QRect title(m.left(), m.top() - TitleHeight, nativeSize().width() - m.left() - m.right(), TitleHeight);
    if (!title.contains(p))
        return PartNone;
    for (Part part : {PartClose, PartMaximize, PartMinimize})
        if (buttonRect(part).contains(p))
            return part;
    return PartTitle;
}

uint32_t QPurecWindow::edgesAt(const QPoint &p) const
{
    if (!m_decorated || m_state != VB_STATE_NORMAL || !resizable())
        return 0;
    QSize n = nativeSize();
    uint32_t edges = 0;
    if (p.x() < ResizeMargin)
        edges |= VB_EDGE_LEFT;
    else if (p.x() >= n.width() - ResizeMargin)
        edges |= VB_EDGE_RIGHT;
    if (p.y() < ResizeMargin / 2 + 1)
        edges |= VB_EDGE_TOP;
    else if (p.y() >= n.height() - ResizeMargin)
        edges |= VB_EDGE_BOTTOM;
    if (edges & (VB_EDGE_LEFT | VB_EDGE_RIGHT)) {
        if (p.y() < ResizeMargin * 3)
            edges |= VB_EDGE_TOP;
        else if (p.y() >= n.height() - ResizeMargin * 3)
            edges |= VB_EDGE_BOTTOM;
    }
    return edges;
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
    m_decorated = wantsDecoration();
    m_state = VB_STATE_NORMAL;
    m_hover = PartNone;
    m_pressed = PartNone;
    m_cursor = VB_CURSOR_ARROW;
    int role = isPopup() ? VB_ROLE_POPUP : VB_ROLE_NORMAL;
    QMargins m = margins();
    QSize native(g.width() + m.left() + m.right(), g.height() + m.top() + m.bottom());
    int x = -1, y = -1;
    if (fullscreen) {
        m_state = VB_STATE_FULLSCREEN;
        native = screen->geometry().size();
        x = 0;
        y = 0;
    } else if (role == VB_ROLE_POPUP || window()->transientParent()) {
        x = g.x() - m.left();
        y = g.y() - m.top();
    }
    QByteArray title = m_title.toUtf8();
    m_native = vb_window_create_role(x, y, native.width(), native.height(), title.constData(), role);
    if (!m_native) {
        qWarning("purec: cannot create a %dx%d window", native.width(), native.height());
        return;
    }
    QPurecIntegration::instance()->attach(m_native, this);
    if (role == VB_ROLE_NORMAL && m_state != VB_STATE_NORMAL)
        vb_window_set_state(m_native, m_state);
    syncGeometryFromNative();
    drawDecoration();
    if (role == VB_ROLE_NORMAL && (states & Qt::WindowMaximized) && !fullscreen)
        vb_window_set_state(m_native, VB_STATE_MAXIMIZED);
    QPurecIntegration::instance()->pumpSoon();
}

void QPurecWindow::destroyNative()
{
    if (!m_native)
        return;
    QPurecIntegration::instance()->detach(m_native);
    vb_window_destroy(m_native);
    m_native = nullptr;
    m_inClient = false;
}

void QPurecWindow::recreate()
{
    destroyNative();
    create();
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
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
    if (!m_native || rect.size() == old.size()) {
        QWindowSystemInterface::handleGeometryChange(window(), rect);
        return;
    }
    QMargins m = margins();
    QSize native(rect.width() + m.left() + m.right(), rect.height() + m.top() + m.bottom());
    if (isPopup() || vb_window_resize(m_native, native.width(), native.height()) < 0) {
        recreate();
        return;
    }
    drawDecoration();
    QRect g(geometry().topLeft(), rect.size());
    QPlatformWindow::setGeometry(g);
    QWindowSystemInterface::handleGeometryChange(window(), g);
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), rect.size()));
}

void QPurecWindow::setWindowState(Qt::WindowStates state)
{
    QPlatformWindow::setWindowState(state);
    if (!m_native || isPopup())
        return;
    if (vb_server_version() < 3) {
        recreate();
        return;
    }
    vb_window_set_state(m_native, vbState(state));
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
    if (!m_native || !m_decorated || m_state == VB_STATE_FULLSCREEN)
        return;
    QSize n = nativeSize();
    QMargins m = margins();
    QImage img(reinterpret_cast<uchar *>(vb_window_pixels(m_native)), n.width(), n.height(), n.width() * 4,
               QImage::Format_RGB32);
    QPainter p(&img);
    QColor bar = m_focused ? QColor(0x1E, 0x1E, 0x2E) : QColor(0x18, 0x18, 0x25);
    QColor text = m_focused ? QColor(0xCD, 0xD6, 0xF4) : QColor(0x7F, 0x84, 0x9C);
    if (m.left()) {
        p.setPen(m_focused ? QColor(0x58, 0x5B, 0x70) : QColor(0x31, 0x32, 0x44));
        p.drawRect(0, 0, n.width() - 1, n.height() - 1);
    }
    QRect title(m.left(), m.top() - TitleHeight, n.width() - m.left() - m.right(), TitleHeight);
    p.fillRect(title, bar);
    p.fillRect(QRect(title.left(), title.bottom(), title.width(), 1), QColor(0x31, 0x32, 0x44));
    p.setRenderHint(QPainter::Antialiasing);
    int buttonsLeft = title.right() + 1;
    for (Part part : {PartMinimize, PartMaximize, PartClose}) {
        QRect r = buttonRect(part);
        if (r.isEmpty())
            continue;
        r.setBottom(title.bottom() - 1);
        buttonsLeft = qMin(buttonsLeft, r.left());
        bool hot = m_hover == part || m_pressed == part;
        if (hot)
            p.fillRect(r, part == PartClose ? QColor(0xE6, 0x45, 0x53) : QColor(0x31, 0x32, 0x44));
        QPen pen(hot && part == PartClose ? QColor(0xFF, 0xFF, 0xFF) : text, 1.2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QPointF c = QRectF(r).center();
        if (part == PartMinimize) {
            p.drawLine(c + QPointF(-5, 0.5), c + QPointF(5, 0.5));
        } else if (part == PartMaximize) {
            if (m_state == VB_STATE_MAXIMIZED) {
                p.drawRect(QRectF(c.x() - 5, c.y() - 3, 8, 8));
                p.drawLine(c + QPointF(-3, -5), c + QPointF(5, -5));
                p.drawLine(c + QPointF(5, -5), c + QPointF(5, 3));
            } else {
                p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
            }
        } else {
            p.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5));
            p.drawLine(c + QPointF(-5, 5), c + QPointF(5, -5));
        }
    }
    QFont font = QGuiApplication::font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    p.setPen(text);
    QRect textRect(title.left() + 14, title.top(), buttonsLeft - title.left() - 28, title.height() - 1);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(m_title, Qt::ElideRight, qMax(0, textRect.width())));
    p.end();
    vb_window_damage(m_native, 0, 0, n.width(), m.top());
    if (m.left()) {
        vb_window_damage(m_native, 0, 0, m.left(), n.height());
        vb_window_damage(m_native, n.width() - m.right(), 0, m.right(), n.height());
        vb_window_damage(m_native, 0, n.height() - m.bottom(), n.width(), m.bottom());
    }
}

void QPurecWindow::present(const QImage &image, const QRegion &region)
{
    if (!m_native)
        return;
    uint32_t *dst = vb_window_pixels(m_native);
    QSize n = nativeSize();
    QMargins m = margins();
    QRect content = contentRect();
    QRect bounds;
    for (const QRect &r0 : region) {
        QRect r = r0.intersected(QRect(0, 0, qMin(content.width(), image.width()),
                                       qMin(content.height(), image.height())));
        if (r.isEmpty())
            continue;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const uint32_t *src = reinterpret_cast<const uint32_t *>(image.constScanLine(y)) + r.left();
            memcpy(dst + size_t(y + m.top()) * size_t(n.width()) + size_t(r.left() + m.left()), src,
                   size_t(r.width()) * 4);
        }
        bounds |= r;
    }
    if (!bounds.isEmpty())
        vb_window_damage(m_native, bounds.x() + m.left(), bounds.y() + m.top(), bounds.width(), bounds.height());
}

void QPurecWindow::syncGeometryFromNative()
{
    if (!m_native)
        return;
    int x = 0, y = 0;
    vb_window_position(m_native, &x, &y);
    QRect content = contentRect();
    QRect g(QPoint(x, y) + content.topLeft(), content.size());
    QPlatformWindow::setGeometry(g);
    QWindowSystemInterface::handleGeometryChange(window(), g);
}

void QPurecWindow::applyConfigure(const vb_event &e)
{
    if (!m_native)
        return;
    int previous = m_state;
    if (e.width > 0 && e.height > 0) {
        m_state = e.state;
        QMargins m = margins();
        QSize content(e.width - m.left() - m.right(), e.height - m.top() - m.bottom());
        if (m_state == VB_STATE_NORMAL)
            content = content.expandedTo(window()->minimumSize()).boundedTo(window()->maximumSize());
        content = content.expandedTo(QSize(1, 1));
        QSize native(content.width() + m.left() + m.right(), content.height() + m.top() + m.bottom());
        if (native != nativeSize())
            vb_window_resize(m_native, native.width(), native.height());
    }
    syncGeometryFromNative();
    drawDecoration();
    if (previous != m_state)
        QWindowSystemInterface::handleWindowStateChanged(window(), qtStates(m_state));
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
}

void QPurecWindow::toggleMaximize()
{
    if (!m_native || !resizable())
        return;
    vb_window_set_state(m_native, m_state == VB_STATE_MAXIMIZED ? VB_STATE_NORMAL : VB_STATE_MAXIMIZED);
}

void QPurecWindow::activatePart(Part part)
{
    switch (part) {
    case PartMinimize:
        vb_window_set_state(m_native, VB_STATE_MINIMIZED);
        QWindowSystemInterface::handleWindowStateChanged(window(), Qt::WindowMinimized);
        break;
    case PartMaximize:
        toggleMaximize();
        break;
    case PartClose:
        QWindowSystemInterface::handleCloseEvent(window());
        break;
    default:
        break;
    }
}

void QPurecWindow::setCursorShape(int cursor)
{
    if (cursor == m_cursor || !m_native)
        return;
    m_cursor = cursor;
    vb_window_set_cursor(m_native, cursor);
}

static int cursorForEdges(uint32_t edges)
{
    bool h = edges & (VB_EDGE_LEFT | VB_EDGE_RIGHT), v = edges & (VB_EDGE_TOP | VB_EDGE_BOTTOM);
    if (h && v)
        return ((edges & VB_EDGE_LEFT) != 0) == ((edges & VB_EDGE_TOP) != 0) ? VB_CURSOR_RESIZE_NWSE
                                                                            : VB_CURSOR_RESIZE_NESW;
    if (h)
        return VB_CURSOR_RESIZE_H;
    if (v)
        return VB_CURSOR_RESIZE_V;
    return VB_CURSOR_ARROW;
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
    QPoint at(e.x, e.y);
    QMargins m = margins();
    QPoint local = at - QPoint(m.left(), m.top());
    bool leftPressed = e.changed & e.buttons & VB_BUTTON_LEFT;
    bool leftReleased = (e.changed & VB_BUTTON_LEFT) && !(e.buttons & VB_BUTTON_LEFT);
    if (m_decorated && m_state != VB_STATE_FULLSCREEN) {
        if (m_pressed != PartNone) {
            if (leftReleased) {
                Part done = m_pressed;
                m_pressed = PartNone;
                m_hover = partAt(at);
                drawDecoration();
                if (m_hover == done)
                    activatePart(done);
            }
            return;
        }
        bool clientGrab = m_buttons & Qt::LeftButton;
        uint32_t edges = clientGrab ? 0 : edgesAt(at);
        if (!clientGrab)
            setCursorShape(cursorForEdges(edges));
        bool inClient = contentRect().contains(at) && !edges;
        if (!inClient && !clientGrab) {
            Part part = edges ? PartNone : partAt(at);
            if (part != m_hover) {
                m_hover = part;
                drawDecoration();
            }
            if (m_inClient) {
                m_inClient = false;
                QWindowSystemInterface::handleLeaveEvent(window());
            }
            if (leftPressed) {
                if (edges) {
                    vb_window_start_resize(m_native, edges);
                } else if (part == PartTitle) {
                    qint64 now = monotonicMs();
                    if (now - m_lastTitleClick < 400) {
                        m_lastTitleClick = -1000;
                        toggleMaximize();
                    } else {
                        m_lastTitleClick = now;
                        if (m_state == VB_STATE_NORMAL)
                            vb_window_move(m_native);
                    }
                } else if (part != PartNone) {
                    m_pressed = part;
                    drawDecoration();
                }
            }
            return;
        }
        if (m_hover != PartNone) {
            m_hover = PartNone;
            drawDecoration();
        }
        if (!m_inClient) {
            m_inClient = true;
            QWindowSystemInterface::handleEnterEvent(window(), QPointF(local), QPointF(geometry().topLeft() + local));
        }
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
    case VB_EVENT_ENTER: {
        QMargins m = margins();
        QPoint at(e.x, e.y);
        if (!m_decorated || m_state == VB_STATE_FULLSCREEN || contentRect().contains(at)) {
            QPoint local = at - QPoint(m.left(), m.top());
            m_inClient = true;
            QWindowSystemInterface::handleEnterEvent(window(), QPointF(local), QPointF(geometry().topLeft() + local));
        }
        break;
    }
    case VB_EVENT_LEAVE:
        if (m_hover != PartNone && m_pressed == PartNone) {
            m_hover = PartNone;
            drawDecoration();
        }
        m_cursor = -1;
        if (m_inClient) {
            m_inClient = false;
            QWindowSystemInterface::handleLeaveEvent(window());
        }
        break;
    case VB_EVENT_KEY:
        key(e);
        break;
    case VB_EVENT_FOCUS:
        m_focused = e.focused;
        drawDecoration();
        if (e.focused) {
            if (window()->windowStates() & Qt::WindowMinimized)
                QWindowSystemInterface::handleWindowStateChanged(window(), qtStates(m_state));
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
        } else if (QGuiApplication::focusWindow() == window()) {
            QWindowSystemInterface::handleFocusWindowChanged(nullptr, Qt::ActiveWindowFocusReason);
        }
        break;
    case VB_EVENT_CLOSE:
        QWindowSystemInterface::handleCloseEvent(window());
        break;
    case VB_EVENT_POPUP_DONE:
        QWindowSystemInterface::handleCloseEvent(window());
        break;
    case VB_EVENT_CONFIGURE:
        applyConfigure(e);
        break;
    default:
        break;
    }
}

QT_END_NAMESPACE
