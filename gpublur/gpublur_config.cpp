/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "gpublur_config.h"

#include "config-kwin.h"

// KConfigSkeleton
#include "gpublurconfig.h"

#include <KPluginFactory>
#include <kwineffects_interface.h>

K_PLUGIN_CLASS(KWin::GpuBlurEffectConfig)

namespace KWin
{

GpuBlurEffectConfig::GpuBlurEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    ui.setupUi(widget());
    GpuBlurConfig::instance(KWIN_CONFIG);
    addConfig(GpuBlurConfig::self(), widget());
}

GpuBlurEffectConfig::~GpuBlurEffectConfig()
{
}

void GpuBlurEffectConfig::save()
{
    KCModule::save();

    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                         QStringLiteral("/Effects"),
                                         QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("gpublur"));
    // GPU Wobbly Windows frosts with these settings too
    interface.reconfigureEffect(QStringLiteral("gpuwobbly"));
}

} // namespace KWin

#include "gpublur_config.moc"

#include "moc_gpublur_config.cpp"
