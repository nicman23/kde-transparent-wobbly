/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ui_gpublur_config.h"
#include <KCModule>

namespace KWin
{

class GpuBlurEffectConfig : public KCModule
{
    Q_OBJECT

public:
    explicit GpuBlurEffectConfig(QObject *parent, const KPluginMetaData &data);
    ~GpuBlurEffectConfig() override;

    void save() override;

private:
    ::Ui::GpuBlurEffectConfigForm ui;
};

} // namespace KWin
