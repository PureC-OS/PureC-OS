#include "qpurecbackingstore.h"
#include "qpurecwindow.h"

#include <QtGui/qpainter.h>
#include <qpa/qplatformscreen.h>

QT_BEGIN_NAMESPACE

QPurecBackingStore::QPurecBackingStore(QWindow *window)
    : QPlatformBackingStore(window)
{
}

QPurecBackingStore::~QPurecBackingStore() = default;

QPaintDevice *QPurecBackingStore::paintDevice()
{
    return &m_image;
}

void QPurecBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &)
{
    if (!window || !window->handle())
        return;
    auto *platformWindow = static_cast<QPurecWindow *>(window->handle());
    platformWindow->present(m_image, region);
}

void QPurecBackingStore::resize(const QSize &size, const QRegion &)
{
    if (m_image.size() != size && !size.isEmpty())
        m_image = QImage(size, QImage::Format_RGB32);
}

QT_END_NAMESPACE
