/*
    Wobbly windows with the spring physics, the mesh and a frosted background all on the GPU.

    Physics ported from KWin's wobblywindows effect:
    SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/region.h"
#include "effect/effect.h"
#include "scene/item.h"

#include <epoxy/gl.h>

#include <QMatrix4x4>

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>

namespace KWin
{

class ColorDescription;
class GLFramebuffer;
class GLShader;
class GLTexture;

class GpuWobblyEffect : public Effect
{
    Q_OBJECT

public:
    GpuWobblyEffect();
    ~GpuWobblyEffect() override;

    void reconfigure(ReconfigureFlags) override;
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override;
    bool drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data) override;
    void postPaintScreen() override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        // same slot as wobblywindows: after maximize, see there
        return 70;
    }

    static bool supported();

    struct ParameterSet
    {
        float stiffness;
        float drag;
        float moveFactor;
        int xTessellation;
        int yTessellation;
        float minVelocity;
        float maxVelocity;
        float stopVelocity;
        float minAcceleration;
        float maxAcceleration;
        float stopAcceleration;
    };

private:
    struct Offscreen
    {
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };

    struct Wobble
    {
        ~Wobble();

        Offscreen state[2];
        int current = 0;
        bool reset = true;
        RectF rect; // frame geometry the state is relative to
        uint constraints = 0;
        uint edges = 0;
        bool moving = true;
        bool wobbling = true;
        RectF resizeOriginalRect;
        // presentation time the physics has advanced to; not AnimationClock, which truncates
        // every frame to whole ms (6.9 ms counts as 6 at 144 Hz) and slows the wobble down
        std::optional<std::chrono::nanoseconds> lastTimestamp;

        // energy readback, consumed a frame later so it never stalls
        GLuint pbo = 0;
        GLsync fence = nullptr;

        Offscreen window;
        std::shared_ptr<ColorDescription> windowColor;
        bool windowDirty = true;
        bool capturing = false;
        ItemEffect itemEffect;
        QMetaObject::Connection damaged;
    };

    static Offscreen allocate(GLenum format, const QSize &size, GLenum filter);

    void slotWindowAdded(EffectWindow *w);
    void startMovedResized(EffectWindow *w);
    void stepMovedResized(EffectWindow *w);
    void finishMovedResized(EffectWindow *w);
    void remove(EffectWindow *w);

    void pollEnergy(Wobble &wobble);
    bool simulate(EffectWindow *w, Wobble &wobble, std::chrono::nanoseconds delta);
    bool capture(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, Wobble &wobble);
    GLTexture *blurBackground(const RenderTarget &renderTarget, const RenderViewport &viewport, const Region &deviceRegion, const Rect &backgroundRect);
    RectF bounds(EffectWindow *w) const;

    ParameterSet m_params;
    float m_opacity = 0.85; // window opacity while it wobbles, below 1 the frost shows through
    // the frost, from GPU Blur's settings
    int m_blurLevels = 4;
    float m_blurSpread = 2.0;
    float m_noise = 0.0;
    QMatrix4x4 m_colorMatrix;
    bool m_moveWobble = true;
    bool m_resizeWobble = true;

    std::unordered_map<EffectWindow *, std::unique_ptr<Wobble>> m_windows;
    Region m_updateRegion;

    std::unique_ptr<GLShader> m_physicsShader;
    std::unique_ptr<GLShader> m_blurShader;
    std::unique_ptr<GLShader> m_compositeShader;
    GLuint m_vao = 0; // draws here are attribute-less, keep them off KWin's vertex array state

    Offscreen m_background; // copy of what is behind the window
    Offscreen m_blur[4][2]; // per level (1/2 .. 1/16 size): horizontal pass, vertical pass
};

} // namespace KWin
