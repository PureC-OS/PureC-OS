#include <qpa/qplatformintegrationplugin.h>
#include "qpurecintegration.h"

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

class QPurecIntegrationPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "purec.json")
public:
    QPlatformIntegration *create(const QString &, const QStringList &) override;
};

QPlatformIntegration *QPurecIntegrationPlugin::create(const QString &system, const QStringList &paramList)
{
    if (!system.compare("purec"_L1, Qt::CaseInsensitive))
        return new QPurecIntegration(paramList);
    return nullptr;
}

QT_END_NAMESPACE

#include "main.moc"
