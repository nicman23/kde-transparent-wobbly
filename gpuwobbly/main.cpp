/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpuwobbly.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(GpuWobblyEffect,
                              "metadata.json",
                              return GpuWobblyEffect::supported();)

} // namespace KWin

#include "main.moc"
