/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>
    SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "gpuwobbly_config.h"

#include "config-kwin.h"

// KConfigSkeleton
#include "gpuwobblyconfig.h"
#include <kwineffects_interface.h>

#include <KLocalizedString>
#include <KPluginFactory>
#include <kconfiggroup.h>

K_PLUGIN_CLASS(KWin::GpuWobblyEffectConfig)

namespace KWin
{

//-----------------------------------------------------------------------------
// WARNING: This is (kinda) copied from gpuwobbly.cpp

struct ParameterSet
{
    int stiffness;
    int drag;
    int move_factor;
};

static const ParameterSet set_0 = {
    15,
    80,
    10};

static const ParameterSet set_1 = {
    10,
    85,
    10};

static const ParameterSet set_2 = {
    6,
    90,
    10};

static const ParameterSet set_3 = {
    3,
    92,
    20};

static const ParameterSet set_4 = {
    1,
    97,
    25};

ParameterSet pset[5] = {set_0, set_1, set_2, set_3, set_4};

//-----------------------------------------------------------------------------

GpuWobblyEffectConfig::GpuWobblyEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    GpuWobblyConfig::instance(KWIN_CONFIG);
    m_ui.setupUi(widget());

    addConfig(GpuWobblyConfig::self(), widget());
    connect(m_ui.kcfg_WobblynessLevel, &QSlider::valueChanged, this, &GpuWobblyEffectConfig::wobblinessChanged);
}

GpuWobblyEffectConfig::~GpuWobblyEffectConfig()
{
}

void GpuWobblyEffectConfig::save()
{
    KCModule::save();

    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                         QStringLiteral("/Effects"),
                                         QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("gpuwobbly"));
}

void GpuWobblyEffectConfig::wobblinessChanged()
{
    ParameterSet preset = pset[m_ui.kcfg_WobblynessLevel->value()];

    m_ui.kcfg_Stiffness->setValue(preset.stiffness);
    m_ui.kcfg_Drag->setValue(preset.drag);
    m_ui.kcfg_MoveFactor->setValue(preset.move_factor);
}

} // namespace

#include "gpuwobbly_config.moc"

#include "moc_gpuwobbly_config.cpp"
