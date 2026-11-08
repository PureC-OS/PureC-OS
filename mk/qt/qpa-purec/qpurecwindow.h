#ifndef QPURECWINDOW_H
#define QPURECWINDOW_H

#include <qpa/qplatformwindow.h>
#include <QtGui/qimage.h>

extern "C" {
#include "puregui.h"
}

QT_BEGIN_NAMESPACE

class QPurecWindow : public QPlatformWindow
{
public:
    explicit QPurecWindow(QWindow *window);
    ~QPurecWindow() override;

    void setVisible(bool visible) override;
    void setGeometry(const QRect &rect) override;
    void setWindowTitle(const QString &title) override;
    WId winId() const override { return WId(this); }
    void propagateSizeHints() override {}
    void raise() override {}
    void lower() override {}

    bool native() const { return m_native; }
    bool pollOne();
    void present(const QImage &image, const QRegion &region);

private:
    bool openNative();
    void closeNative();
    void redrawFrame();
    void syncGeometry();
    static int qtKeyForChar(int ch, QString *text, Qt::KeyboardModifiers *mods);
    static int qtKeyForSpecial(int special);

    struct pg_window m_pg;
    bool m_native = false;
    QImage m_last;
    QRect m_requested;
    int m_buttons = 0;
    QPoint m_lastPos;
};

QT_END_NAMESPACE

#endif
