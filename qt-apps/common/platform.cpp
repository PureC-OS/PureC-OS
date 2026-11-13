#include "platform.h"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtGui/QFontDatabase>
#include <QtPlugin>

Q_IMPORT_PLUGIN(QPurecIntegrationPlugin)

namespace purec {

void preparePlatform(int &, char **)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "purec");
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "/usr/share/fonts");
    if (qEnvironmentVariableIsEmpty("QT_NO_FONTCONFIG"))
        qputenv("QT_NO_FONTCONFIG", "1");
}

void installFonts()
{
    const QStringList files = {
        QStringLiteral("/usr/share/fonts/DejaVuSansMono.ttf"),
        QStringLiteral("/usr/share/fonts/DejaVuSansMono-Bold.ttf"),
        QStringLiteral("/usr/share/fonts/DejaVuSans.ttf"),
        QStringLiteral("/usr/share/fonts/DejaVuSans-Bold.ttf"),
    };
    for (const QString &file : files)
        QFontDatabase::addApplicationFont(file);
}

QString fontFamily(bool monospace)
{
    return monospace ? QStringLiteral("DejaVu Sans Mono") : QStringLiteral("DejaVu Sans");
}

}
