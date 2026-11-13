#ifndef QPURECBACKINGSTORE_H
#define QPURECBACKINGSTORE_H

#include <qpa/qplatformbackingstore.h>
#include <QtGui/qimage.h>

QT_BEGIN_NAMESPACE

class QPurecBackingStore : public QPlatformBackingStore
{
public:
    explicit QPurecBackingStore(QWindow *window);
    ~QPurecBackingStore() override;

    QPaintDevice *paintDevice() override;
    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;
    void resize(const QSize &size, const QRegion &staticContents) override;

private:
    QImage m_image;
};

QT_END_NAMESPACE

#endif
