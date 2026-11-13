#ifndef QPURECINTEGRATION_H
#define QPURECINTEGRATION_H

#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>
#include <QtCore/qpointer.h>
#include <QtCore/qtimer.h>

QT_BEGIN_NAMESPACE

class QPurecWindow;

class QPurecScreen : public QPlatformScreen
{
public:
    QPurecScreen();
    QRect geometry() const override { return m_geometry; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QSizeF physicalSize() const override;
    void refresh();
private:
    QRect m_geometry;
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

    void registerWindow(QPurecWindow *window);
    void unregisterWindow(QPurecWindow *window);
    QPurecWindow *nativeOwner() const { return m_owner; }
    void claimNative(QPurecWindow *window) { if (!m_owner) m_owner = window; }

    static QPurecIntegration *instance();

private:
    void pump();

    mutable QPlatformFontDatabase *m_fontDatabase = nullptr;
    QPurecScreen *m_screen = nullptr;
    QTimer m_timer;
    QPurecWindow *m_owner = nullptr;
    QList<QPurecWindow *> m_windows;
    int m_buttons = 0;
};

QT_END_NAMESPACE

#endif
