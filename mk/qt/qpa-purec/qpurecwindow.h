#ifndef QPURECWINDOW_H
#define QPURECWINDOW_H

#include <qpa/qplatformwindow.h>
#include <QtGui/qimage.h>

struct vb_window;
struct vb_event;

QT_BEGIN_NAMESPACE

class QPurecWindow : public QPlatformWindow
{
public:
    explicit QPurecWindow(QWindow *window);
    ~QPurecWindow() override;

    void setVisible(bool visible) override;
    void setGeometry(const QRect &rect) override;
    void setWindowTitle(const QString &title) override;
    void setWindowState(Qt::WindowStates state) override;
    void requestActivateWindow() override;
    WId winId() const override { return WId(this); }
    QMargins frameMargins() const override;
    void propagateSizeHints() override {}
    void raise() override;
    void lower() override {}

    void present(const QImage &image, const QRegion &region);
    void handleEvent(const vb_event &event);

private:
    bool isPopup() const;
    bool wantsDecoration() const;
    void create();
    void destroyNative();
    void drawDecoration();
    void syncGeometryFromNative();
    void pointer(const vb_event &event);
    void key(const vb_event &event);

    vb_window *m_native = nullptr;
    bool m_decorated = false;
    bool m_focused = false;
    bool m_closeHover = false;
    int m_titleHeight = 0;
    QString m_title;
    Qt::MouseButtons m_buttons;
    QPoint m_lastLocal;
};

QT_END_NAMESPACE

#endif
