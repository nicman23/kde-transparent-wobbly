/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>
    SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <kcmodule.h>

#include "ui_gpuwobbly_config.h"

namespace KWin
{

class GpuWobblyEffectConfig : public KCModule
{
    Q_OBJECT

public:
    explicit GpuWobblyEffectConfig(QObject *parent, const KPluginMetaData &data);
    ~GpuWobblyEffectConfig() override;

public Q_SLOTS:
    void save() override;

private Q_SLOTS:
    void wobblinessChanged();

private:
    ::Ui::GpuWobblyEffectConfigForm m_ui;
};

} // namespace
