#include "qpurecintegration.h"
#include "qpurecwindow.h"
#include "qpurecbackingstore.h"

#include <QtCore/qcoreapplication.h>
#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#include <QtGui/private/qfreetypefontdatabase_p.h>
#include <qpa/qwindowsysteminterface.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
#include "vb.h"
}

QT_BEGIN_NAMESPACE

static QPurecIntegration *g_instance = nullptr;

QPurecScreen::QPurecScreen()
{
    int w = 0, h = 0;
    vb_screen_size(&w, &h);
    if (w <= 0 || h <= 0) {
        w = 1024;
        h = 768;
    }
    m_geometry = QRect(0, 0, w, h);
    int x = 0, y = 0, aw = 0, ah = 0;
    vb_work_area(&x, &y, &aw, &ah);
    m_available = aw > 0 && ah > 0 ? QRect(x, y, aw, ah) : m_geometry;
}

QSizeF QPurecScreen::physicalSize() const
{
    return QSizeF(m_geometry.width() * 25.4 / 96.0, m_geometry.height() * 25.4 / 96.0);
}

void QPurecScreen::setAvailable(const QRect &rect)
{
    if (rect.isEmpty() || rect == m_available)
        return;
    m_available = rect;
    QWindowSystemInterface::handleScreenGeometryChange(screen(), m_geometry, m_available);
}

QPurecIntegration::QPurecIntegration(const QStringList &)
{
    g_instance = this;
    int tries = 0;
    while (vb_connect() < 0) {
        if (++tries >= 50)
            qFatal("purec: cannot connect to videod (%s); is the display server running?", VB_SOCKET_PATH);
        usleep(100000);
    }
    if (!getenv("QT_QPA_FONTDIR"))
        setenv("QT_QPA_FONTDIR", "/usr/share/fonts", 0);
    m_screen = new QPurecScreen;
    QWindowSystemInterface::handleScreenAdded(m_screen);
}

QPurecIntegration::~QPurecIntegration()
{
    m_timer.stop();
    delete m_notifier;
    QWindowSystemInterface::handleScreenRemoved(m_screen);
    delete m_fontDatabase;
    vb_disconnect();
    g_instance = nullptr;
}

QPurecIntegration *QPurecIntegration::instance()
{
    return g_instance;
}

void QPurecIntegration::initialize()
{
    m_notifier = new QSocketNotifier(vb_fd(), QSocketNotifier::Read);
    QObject::connect(m_notifier, &QSocketNotifier::activated, [this] { pump(); });
    m_timer.setInterval(50);
    QObject::connect(&m_timer, &QTimer::timeout, [this] { pump(); });
    m_timer.start();
}

void QPurecIntegration::pumpSoon()
{
    QTimer::singleShot(0, [this] { pump(); });
}

bool QPurecIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps:
    case MultipleWindows:
    case NonFullScreenWindows:
    case WindowManagement:
        return true;
    default:
        return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformFontDatabase *QPurecIntegration::fontDatabase() const
{
    if (!m_fontDatabase)
        m_fontDatabase = new QFreeTypeFontDatabase;
    return m_fontDatabase;
}

QPlatformWindow *QPurecIntegration::createPlatformWindow(QWindow *window) const
{
    return new QPurecWindow(window);
}

QPlatformBackingStore *QPurecIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QPurecBackingStore(window);
}

QAbstractEventDispatcher *QPurecIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

void QPurecIntegration::attach(vb_window *native, QPurecWindow *window)
{
    m_windows.insert(native, window);
}

void QPurecIntegration::detach(vb_window *native)
{
    m_windows.remove(native);
}

void QPurecIntegration::pump()
{
    vb_event event;
    for (int budget = 256; budget > 0; --budget) {
        int r = vb_next_event(&event, 0);
        if (r < 0) {
            m_timer.stop();
            if (m_notifier)
                m_notifier->setEnabled(false);
            QCoreApplication::exit(1);
            return;
        }
        if (r == 0)
            return;
        dispatch(event);
    }
}

void QPurecIntegration::dispatch(const vb_event &event)
{
    if (event.type == VB_EVENT_WORKAREA) {
        int x = 0, y = 0, w = 0, h = 0;
        vb_work_area(&x, &y, &w, &h);
        m_screen->setAvailable(QRect(x, y, w, h));
        return;
    }
    QPurecWindow *window = m_windows.value(event.window);
    if (window)
        window->handleEvent(event);
}

QT_END_NAMESPACE
