/*
    The blur both effects draw, set up from GPU Blur's settings. The strength steps and the
    saturation matrix follow KWin's blur effect:
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KConfigGroup>
#include <KSharedConfig>

#include <QList>
#include <QMatrix4x4>

#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>

namespace KWin
{

struct BlurSettings
{
    int levels; // Gaussian passes at 1/2, 1/4, ... size, up to 4
    float spread; // tap distance, in texels of the level
    int margin; // how far the blur reaches, logical px
    float noise; // strength of the additive noise, 0 is off
    QMatrix4x4 colorMatrix; // saturation
};

namespace BlurSettingsDetail
{

struct Offset
{
    float minOffset;
    float maxOffset;
};
inline constexpr Offset s_offsets[4] = {{1.0, 2.0}, {2.0, 3.0}, {2.0, 5.0}, {3.0, 8.0}};

struct Strength
{
    int iteration;
    float offset;
};

// The blur effect's strength steps: {downsample iterations, dual Kawase offset} for strength 1..15,
// built like BlurEffect::initBlurStrengthValues().
inline QList<Strength> strengthValues()
{
    const int numOfBlurSteps = 15;
    int remainingSteps = numOfBlurSteps;
    float offsetSum = 0;
    for (const Offset &offset : s_offsets) {
        offsetSum += offset.maxOffset - offset.minOffset;
    }
    QList<Strength> values;
    for (int i = 0; i < 4; i++) {
        const float offsetDifference = s_offsets[i].maxOffset - s_offsets[i].minOffset;
        int iterationNumber = std::ceil(offsetDifference / offsetSum * numOfBlurSteps);
        remainingSteps -= iterationNumber;
        if (remainingSteps < 0) {
            iterationNumber += remainingSteps;
        }
        for (int j = 1; j <= iterationNumber; j++) {
            values.append({i + 1, s_offsets[i].minOffset + (offsetDifference / iterationNumber) * j});
        }
    }
    return values;
}

// the blur effect's saturation matrix (with contrast 1)
inline QMatrix4x4 colorTransformMatrix(qreal saturation)
{
    QMatrix4x4 saturationMatrix;
    if (!qFuzzyCompare(saturation, 1.0)) {
        const qreal rval = (1.0 - saturation) * 0.2126;
        const qreal gval = (1.0 - saturation) * 0.7152;
        const qreal bval = (1.0 - saturation) * 0.0722;
        saturationMatrix = QMatrix4x4(rval + saturation, rval, rval, 0.0,
                                      gval, gval + saturation, gval, 0.0,
                                      bval, bval, bval + saturation, 0.0,
                                      0.0, 0.0, 0.0, 1.0);
    }
    return saturationMatrix;
}

} // namespace BlurSettingsDetail

// Reads GPU Blur's settings from kwinrc. A strength step picks the number of levels (the blur
// effect's downsample iterations) and, through where its offset sits in that iteration's range, a
// spread of 1..2 texels. Per level the Gaussian has sigma ~1.4 * spread texels; level l texels are
// 2^l logical px, so the steps run continuously from sigma ~4 px (strength 1) to ~52 px (15).
inline BlurSettings readBlurSettings(const KSharedConfigPtr &config)
{
    using namespace BlurSettingsDetail;
    static const QList<Strength> strengths = strengthValues();

    const KConfigGroup group = config->group(QStringLiteral("Effect-gpublur"));
    const Strength strength = strengths[std::clamp(group.readEntry("BlurStrength", 15), 1, int(strengths.size())) - 1];
    const Offset &range = s_offsets[strength.iteration - 1];

    BlurSettings settings;
    settings.levels = strength.iteration;
    settings.spread = 1.0 + (strength.offset - range.minOffset) / (range.maxOffset - range.minOffset);
    // each pass reads 3.23 taps out, over the levels 3.23 * spread * (2 + 4 + ...) px
    settings.margin = std::ceil(3.3 * settings.spread * ((2 << settings.levels) - 2));
    settings.noise = std::max(0, group.readEntry("NoiseStrength", 5)) / 255.0;
    settings.colorMatrix = colorTransformMatrix(group.readEntry("Saturation", 150) / 100.0);
    return settings;
}

// The effects' draws are attribute-less, this keeps them off KWin's vertex array state.
class VertexArrayBinder
{
public:
    explicit VertexArrayBinder(GLuint vao)
    {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_previous);
        glBindVertexArray(vao);
    }
    ~VertexArrayBinder()
    {
        glBindVertexArray(m_previous);
    }

private:
    GLint m_previous = 0;
};

} // namespace KWin
