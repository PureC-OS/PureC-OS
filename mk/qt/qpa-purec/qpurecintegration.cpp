#include "qpurecintegration.h"
#include "qpurecwindow.h"
#include "qpurecbackingstore.h"

#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#include <QtGui/private/qfreetypefontdatabase_p.h>
#include <qpa/qwindowsysteminterface.h>
#include <QtGui/qguiapplication.h>

extern "C" {
#include "puregui.h"
#include "purec.h"
}

QT_BEGIN_NAMESPACE

static QPurecIntegration *g_instance = nullptr;

class QPurecFontDatabase : public QFreeTypeFontDatabase
{
public:
    void populateFontDatabase() override {}
};

QPurecScreen::QPurecScreen()
{
    refresh();
}

void QPurecScreen::refresh()
{
    struct pc_display_info info;
    if (pc_display_get_info(&info) && info.width && info.height)
        m_geometry = QRect(0, 0, int(info.width), int(info.height));
    else
        m_geometry = QRect(0, 0, 1024, 768);
}

QSizeF QPurecScreen::physicalSize() const
{
    return QSizeF(m_geometry.width() * 25.4 / 96.0, m_geometry.height() * 25.4 / 96.0);
}

QPurecIntegration::QPurecIntegration(const QStringList &)
{
    g_instance = this;
    m_screen = new QPurecScreen;
    QWindowSystemInterface::handleScreenAdded(m_screen);
}

QPurecIntegration::~QPurecIntegration()
{
    m_timer.stop();
    QWindowSystemInterface::handleScreenRemoved(m_screen);
    delete m_fontDatabase;
    g_instance = nullptr;
}

QPurecIntegration *QPurecIntegration::instance()
{
    return g_instance;
}

void QPurecIntegration::initialize()
{
    m_timer.setInterval(8);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, [this] { pump(); });
    m_timer.start();
}

bool QPurecIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps:
        return true;
    case MultipleWindows:
        return false;
    default:
        return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformFontDatabase *QPurecIntegration::fontDatabase() const
{
    if (!m_fontDatabase)
        m_fontDatabase = new QPurecFontDatabase;
    return m_fontDatabase;
}

QPlatformWindow *QPurecIntegration::createPlatformWindow(QWindow *window) const
{
    auto *platformWindow = new QPurecWindow(window);
    return platformWindow;
}

QPlatformBackingStore *QPurecIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QPurecBackingStore(window);
}

QAbstractEventDispatcher *QPurecIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

void QPurecIntegration::registerWindow(QPurecWindow *window)
{
    if (!m_windows.contains(window))
        m_windows.append(window);
}

void QPurecIntegration::unregisterWindow(QPurecWindow *window)
{
    m_windows.removeAll(window);
    if (m_owner == window)
        m_owner = nullptr;
}

void QPurecIntegration::pump()
{
    if (!m_owner)
        return;
    QPurecWindow *owner = m_owner;
    for (int budget = 64; budget > 0; --budget) {
        if (!owner->pollOne())
            break;
        if (m_owner != owner)
            break;
    }
}

QT_END_NAMESPACE
