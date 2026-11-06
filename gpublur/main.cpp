/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpublur.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(GpuBlurEffect,
                              "metadata.json",
                              return GpuBlurEffect::supported();)

} // namespace KWin

#include "main.moc"
