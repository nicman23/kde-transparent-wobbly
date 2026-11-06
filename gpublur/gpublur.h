/*
    Background blur on the GPU. Which windows get blurred, and where, follows KWin's blur effect:
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "config-kwin.h"
#include "core/region.h"
#include "effect/effect.h"

#include <epoxy/gl.h>

#include <QMatrix4x4>

#include <memory>
#include <optional>
#include <unordered_map>

namespace KWin
{

class BackgroundEffectItem;
class GLFramebuffer;
class GLShader;
class GLTexture;
class RenderView;

class GpuBlurEffect : public Effect
{
    Q_OBJECT

public:
    GpuBlurEffect();
    ~GpuBlurEffect() override;

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data) override;
    bool drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data) override;
    bool provides(Feature feature) override;
    bool isActive() const override;
    bool blocksDirectScanout() const override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    int requestedEffectChainPosition() const override
    {
        // same slot as blur
        return 20;
    }

    static bool supported();

private:
    struct Offscreen
    {
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };

    struct RenderData
    {
        Offscreen background; // copy of what is behind the window, kept between frames
        Offscreen blur[4][2]; // per level (1/2 .. 1/16 size): horizontal pass, vertical pass
    };

    struct BlurData
    {
        std::optional<RegionF> content;
        std::optional<RegionF> frame;
        std::unique_ptr<BackgroundEffectItem> item;
        std::unordered_map<RenderView *, RenderData> render;
    };

    static Offscreen allocate(GLenum format, const QSize &size);

    void slotWindowAdded(EffectWindow *w);
    void slotWindowDeleted(EffectWindow *w);
    void slotViewRemoved(RenderView *view);
#if KWIN_BUILD_X11
    void slotPropertyNotify(EffectWindow *w, long atom);
#endif
    void setupDecorationConnections(EffectWindow *w);
    void updateBlurRegion(EffectWindow *w);
    void updateItem(EffectWindow *w, BlurData &data);
    RegionF decorationBlurRegion(const EffectWindow *w) const;
    RegionF blurRegion(EffectWindow *w) const;
    bool isTranslucent(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data) const;
    void blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, const Region &deviceRegion, WindowPaintData &data);

    // from the settings, see reconfigure()
    int m_levels = 3;
    float m_spread = 2.0;
    int m_margin = 0; // reach of the blur, logical px
    float m_noise = 0.0;
    bool m_blurAll = true;
    QMatrix4x4 m_colorMatrix;

    std::unordered_map<EffectWindow *, BlurData> m_windows;
    std::unordered_map<EffectWindow *, QMetaObject::Connection> m_blurChangedConnections;
    RenderView *m_currentView = nullptr;
    long m_blurAtom = 0;

    std::unique_ptr<GLShader> m_blurShader;
    std::unique_ptr<GLShader> m_onscreenShader;
    GLuint m_vao = 0; // draws here are attribute-less, keep them off KWin's vertex array state
};

} // namespace KWin
