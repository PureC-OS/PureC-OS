#ifndef QPURECINTEGRATION_H
#define QPURECINTEGRATION_H

#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>
#include <QtCore/qhash.h>
#include <QtCore/qsocketnotifier.h>
#include <QtCore/qtimer.h>

struct vb_window;
struct vb_event;

QT_BEGIN_NAMESPACE

class QPurecWindow;

class QPurecScreen : public QPlatformScreen
{
public:
    QPurecScreen();
    QRect geometry() const override { return m_geometry; }
    QRect availableGeometry() const override { return m_available; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QSizeF physicalSize() const override;
    QString name() const override { return QStringLiteral("videod"); }
    void setAvailable(const QRect &rect);
    void setSize(int width, int height);

private:
    QRect m_geometry;
    QRect m_available;
};

class QPurecIntegration : public QPlatformIntegration
{
public:
    explicit QPurecIntegration(const QStringList &parameters);
    ~QPurecIntegration() override;

    void initialize() override;
    bool hasCapability(QPlatformIntegration::Capability cap) const override;
    QPlatformFontDatabase *fontDatabase() const override;
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;

    void attach(vb_window *native, QPurecWindow *window);
    void detach(vb_window *native);
    QPurecScreen *screen() const { return m_screen; }
    void pumpSoon();

    static QPurecIntegration *instance();

private:
    void pump();
    void dispatch(const vb_event &event);

    mutable QPlatformFontDatabase *m_fontDatabase = nullptr;
    QPurecScreen *m_screen = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_timer;
    QHash<vb_window *, QPurecWindow *> m_windows;
};

QT_END_NAMESPACE

#endif
