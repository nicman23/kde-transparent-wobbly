/*
    Background blur on the GPU. Which windows get blurred, and where, follows KWin's blur effect:
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpublur.h"
#include "gpublurconfig.h"
#include "blursettings.h"
#include "core/output.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "opengl/eglcontext.h"
#include "opengl/glframebuffer.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "scene/backgroundeffectitem.h"
#include "scene/borderradius.h"
#include "scene/scene.h"
#include "scene/windowitem.h"
#include "wayland/backgroundeffect_v1.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "window.h"
#if KWIN_BUILD_X11
#include "utils/xcbutils.h"
#include "x11window.h"
#endif

#include <KDecoration3/Decoration>

#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QLoggingCategory>
#include <QWindow>

Q_LOGGING_CATEGORY(KWIN_GPUBLUR, "kwin_effect_gpublur", QtWarningMsg)

namespace KWin
{

static const QByteArray s_blurAtomName = QByteArrayLiteral("_KDE_NET_WM_BLUR_BEHIND_REGION");

static std::unique_ptr<GLShader> loadShader(const QString &vertex, const QString &fragment)
{
    QFile vertexFile(QStringLiteral(":/effects/gpublur/shaders/") + vertex);
    QFile fragmentFile(QStringLiteral(":/effects/gpublur/shaders/") + fragment);
    if (!vertexFile.open(QIODevice::ReadOnly) || !fragmentFile.open(QIODevice::ReadOnly)) {
        qCWarning(KWIN_GPUBLUR) << "Missing shader" << vertex << fragment;
        return nullptr;
    }
    auto shader = ShaderManager::instance()->loadShaderFromCode(vertexFile.readAll(), fragmentFile.readAll());
    if (!shader) {
        qCWarning(KWIN_GPUBLUR) << "Failed to build shader" << vertex << fragment;
    }
    return shader;
}

GpuBlurEffect::GpuBlurEffect()
{
    m_blurShader = loadShader(QStringLiteral("fullscreen.vert"), QStringLiteral("blur.frag"));
    m_onscreenShader = loadShader(QStringLiteral("onscreen.vert"), QStringLiteral("onscreen.frag"));
    glGenVertexArrays(1, &m_vao);

    GpuBlurConfig::instance(effects->config());
    reconfigure(ReconfigureAll);

#if KWIN_BUILD_X11
    if (effects->xcbConnection()) {
        m_blurAtom = effects->announceSupportProperty(s_blurAtomName, this);
    }
    connect(effects, &EffectsHandler::propertyNotify, this, &GpuBlurEffect::slotPropertyNotify);
    connect(effects, &EffectsHandler::xcbConnectionChanged, this, [this]() {
        m_blurAtom = effects->announceSupportProperty(s_blurAtomName, this);
    });
#endif

    // tells Wayland clients that they can ask for blur
    waylandServer()->backgroundEffectManager()->addBlurCapability();

    connect(effects, &EffectsHandler::windowAdded, this, &GpuBlurEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &GpuBlurEffect::slotWindowDeleted);
    connect(effects, &EffectsHandler::viewRemoved, this, &GpuBlurEffect::slotViewRemoved);

    const auto stackingOrder = effects->stackingOrder();
    for (EffectWindow *window : stackingOrder) {
        slotWindowAdded(window);
    }
}

GpuBlurEffect::~GpuBlurEffect()
{
    waylandServer()->backgroundEffectManager()->removeBlurCapability();
    m_windows.clear();
    glDeleteVertexArrays(1, &m_vao);
}

void GpuBlurEffect::reconfigure(ReconfigureFlags)
{
    GpuBlurConfig::self()->read();

    const BlurSettings settings = readBlurSettings(effects->config());
    m_levels = settings.levels;
    m_spread = settings.spread;
    m_margin = settings.margin;
    m_noise = settings.noise;
    m_colorMatrix = settings.colorMatrix;
    const bool blurAll = GpuBlurConfig::blurAllTranslucent();
    const bool blurAllChanged = blurAll != m_blurAll;
    m_blurAll = blurAll;

    for (auto &[window, data] : m_windows) {
        updateItem(window, data);
        data.render.clear(); // level count may have changed
    }
    if (blurAllChanged) {
        const auto stackingOrder = effects->stackingOrder();
        for (EffectWindow *window : stackingOrder) {
            updateBlurRegion(window);
        }
    }
    effects->addRepaintFull();
}

bool GpuBlurEffect::supported()
{
    return effects->isOpenGLCompositing();
}

bool GpuBlurEffect::provides(Feature feature)
{
    return feature == Blur || Effect::provides(feature);
}

bool GpuBlurEffect::isActive() const
{
    return m_blurShader && m_onscreenShader && !effects->isScreenLocked();
}

bool GpuBlurEffect::blocksDirectScanout() const
{
    return false;
}

GpuBlurEffect::Offscreen GpuBlurEffect::allocate(GLenum format, const QSize &size)
{
    Offscreen ret;
    ret.texture = GLTexture::allocate(format, size);
    if (!ret.texture) {
        return {};
    }
    ret.texture->setFilter(GL_LINEAR);
    ret.texture->setWrapMode(GL_CLAMP_TO_EDGE);
    ret.framebuffer = std::make_unique<GLFramebuffer>(ret.texture.get());
    if (!ret.framebuffer->valid()) {
        return {};
    }
    GLFramebuffer::pushFramebuffer(ret.framebuffer.get());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    GLFramebuffer::popFramebuffer();
    return ret;
}

void GpuBlurEffect::updateBlurRegion(EffectWindow *w)
{
    std::optional<RegionF> content;
    std::optional<RegionF> frame;

#if KWIN_BUILD_X11
    if (m_blurAtom != XCB_ATOM_NONE) {
        if (const auto x11Window = qobject_cast<X11Window *>(w->window())) {
            Xcb::Property wmBlurRegionProperty(false, x11Window->window(), m_blurAtom, XCB_ATOM_CARDINAL, 0, 32768);
            if (const auto cardinals = wmBlurRegionProperty.array<uint32_t>()) {
                if (cardinals->size() == 0 || cardinals->size() == 1) {
                    // It means blur background behind whole window.
                    content = RegionF();
                } else if (cardinals->size() % 4 == 0) {
                    RegionF region;
                    for (uint i = 0; i < cardinals->size();) {
                        const int x = (*cardinals)[i++];
                        const int y = (*cardinals)[i++];
                        const int w = (*cardinals)[i++];
                        const int h = (*cardinals)[i++];
                        region += Xcb::fromXNative(Rect(x, y, w, h));
                    }
                    content = region;
                }
            }
        }
    }
#endif

    if (SurfaceInterface *surface = w->surface()) {
        if (!surface->blurRegion().isEmpty()) {
            content = surface->blurRegion();
        }
    }

    if (auto internal = w->internalWindow()) {
        const auto property = internal->property("kwin_blur");
        if (property.isValid()) {
            content = property.value<RegionF>();
        }
    }

    if (w->decorationHasAlpha() && w->decoration() && !w->decoration()->blurRegion().isNull()) {
        frame = decorationBlurRegion(w);
    }

    // Blur all translucent windows: blur the whole window behind what does not ask for it.
    if (m_blurAll && !w->isDesktop()) {
        if (!content.has_value() && isTranslucent(w)) {
            content = RegionF(); // empty means the whole window
        }
        if (!frame.has_value() && w->decorationHasAlpha() && w->decoration()) {
            frame = RegionF(w->decoration()->rect()) - w->contentsRect();
        }
    }

    if (content.has_value() || frame.has_value()) {
        BlurData &data = m_windows[w];
        data.content = content;
        data.frame = frame;
        if (!data.item) {
            data.item = std::make_unique<BackgroundEffectItem>(w->windowItem());
        }
        updateItem(w, data);
    } else if (auto it = m_windows.find(w); it != m_windows.end()) {
        effects->makeOpenGLContextCurrent();
        m_windows.erase(it);
    }
}

void GpuBlurEffect::updateItem(EffectWindow *w, BlurData &data)
{
    // When anything below changes inside this rect, the scene repaints all of it, which keeps the copy
    // of the background valid. The scene clips the rect to the window, so the blur can only rely on
    // pixels inside the region: like the blur effect, it clamps at the edge instead of reading beyond.
    data.item->setPixelsToExpandRepaintsBelowOpaqueRegions(m_margin);
    data.item->setEffectBoundingRect(blurRegion(w).boundingRect());
}

bool GpuBlurEffect::isTranslucent(const EffectWindow *w) const
{
    if (w->opacity() < 1.0) {
        return true;
    }
    // Wayland and Xwayland windows declare what of them is opaque; anything else may show through
    const SurfaceInterface *surface = w->surface();
    if (!surface) {
        return false;
    }
    const RectF rect(QPointF(0, 0), surface->size());
    return !rect.isEmpty() && !(RegionF(rect) - surface->opaque()).isEmpty();
}

void GpuBlurEffect::slotWindowAdded(EffectWindow *w)
{
    if (SurfaceInterface *surface = w->surface()) {
        m_blurChangedConnections[w] = connect(surface, &SurfaceInterface::blurChanged, this, [this, w]() {
            updateBlurRegion(w);
        });
        // for blurring all translucent windows
        connect(surface, &SurfaceInterface::opaqueChanged, this, [this, w]() {
            updateBlurRegion(w);
        });
    }
    connect(w, &EffectWindow::windowOpacityChanged, this, &GpuBlurEffect::updateBlurRegion);
    if (auto internal = w->internalWindow()) {
        internal->installEventFilter(this);
    }

    setupDecorationConnections(w);
    connect(w, &EffectWindow::windowDecorationChanged, this, [this, w]() {
        setupDecorationConnections(w);
        updateBlurRegion(w);
    });

    updateBlurRegion(w);
}

void GpuBlurEffect::slotWindowDeleted(EffectWindow *w)
{
    if (auto it = m_windows.find(w); it != m_windows.end()) {
        effects->makeOpenGLContextCurrent();
        m_windows.erase(it);
    }
    if (auto it = m_blurChangedConnections.find(w); it != m_blurChangedConnections.end()) {
        disconnect(it->second);
        m_blurChangedConnections.erase(it);
    }
}

void GpuBlurEffect::slotViewRemoved(RenderView *view)
{
    for (auto &[window, data] : m_windows) {
        if (auto it = data.render.find(view); it != data.render.end()) {
            effects->makeOpenGLContextCurrent();
            data.render.erase(it);
        }
    }
}

#if KWIN_BUILD_X11
void GpuBlurEffect::slotPropertyNotify(EffectWindow *w, long atom)
{
    if (w && atom == m_blurAtom && m_blurAtom != XCB_ATOM_NONE) {
        updateBlurRegion(w);
    }
}
#endif

void GpuBlurEffect::setupDecorationConnections(EffectWindow *w)
{
    if (!w->decoration()) {
        return;
    }
    connect(w->decoration(), &KDecoration3::Decoration::blurRegionChanged, this, [this, w]() {
        updateBlurRegion(w);
    });
}

bool GpuBlurEffect::eventFilter(QObject *watched, QEvent *event)
{
    auto internal = qobject_cast<QWindow *>(watched);
    if (internal && event->type() == QEvent::DynamicPropertyChange) {
        auto pe = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (pe->propertyName() == "kwin_blur") {
            if (auto w = effects->findWindow(internal)) {
                updateBlurRegion(w);
            }
        }
    }
    return false;
}

RegionF GpuBlurEffect::decorationBlurRegion(const EffectWindow *w) const
{
    // only the parts of the decoration's blur region that belong to the decoration
    const RegionF decorationRegion = RegionF(w->decoration()->rect()) - w->contentsRect();
    return decorationRegion.intersected(RegionF(w->decoration()->blurRegion()));
}

RegionF GpuBlurEffect::blurRegion(EffectWindow *w) const
{
    RegionF region;
    if (auto it = m_windows.find(w); it != m_windows.end()) {
        const std::optional<RegionF> &content = it->second.content;
        const std::optional<RegionF> &frame = it->second.frame;
        if (content.has_value()) {
            if (content->isEmpty()) {
                // An empty region means that the blur effect should be enabled
                // for the whole window.
                region = w->contentsRect();
            } else if (w->decoration()) {
                region = content->translated(w->contentsRect().topLeft()) & w->contentsRect();
            } else {
                region = *content & w->contentsRect();
            }
            if (frame.has_value()) {
                region += frame.value();
            }
        } else if (frame.has_value()) {
            region = frame.value();
        }
    }
    return region;
}

bool GpuBlurEffect::shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data) const
{
    if (effects->activeFullScreenEffect() && !w->data(WindowForceBlurRole).toBool()) {
        return false;
    }
    if (w->isDesktop()) {
        return false;
    }
    // windows being moved or resized are left to the wobbly effect
    if (w->isUserMove() || w->isUserResize()) {
        return false;
    }

    bool scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    bool translated = data.xTranslation() || data.yTranslation();
    if ((scaled || (translated || (mask & PAINT_WINDOW_TRANSFORMED))) && !w->data(WindowForceBlurRole).toBool()) {
        return false;
    }
    return true;
}

void GpuBlurEffect::prePaintScreen(ScreenPrePaintData &data)
{
    m_currentView = data.view;
    effects->prePaintScreen(data);
}

bool GpuBlurEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data)
{
    if (isActive() && m_windows.contains(w) && shouldBlur(w, mask, data)) {
        blur(renderTarget, viewport, w, deviceRegion, data);
    }
    // Draw the window over the blurred area
    return effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

void GpuBlurEffect::blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, const Region &deviceRegion, WindowPaintData &data)
{
    // Compute the effective blur shape. Note that if the window is transformed, so will be the blur shape.
    RegionF shape = blurRegion(w);
    if (data.xScale() != 1 || data.yScale() != 1) {
        shape.scale(data.xScale(), data.yScale());
    }
    if (data.xTranslation() || data.yTranslation()) {
        shape.translate(data.xTranslation(), data.yTranslation());
    }
    shape.translate(w->pos());

    // What ends up on screen: the shape, clipped to what is being repainted.
    Region deviceShape;
    for (const RectF &rect : shape.rects()) {
        deviceShape += viewport.mapToDeviceCoordinatesAligned(rect);
    }
    if (deviceRegion != Region::infinite()) {
        deviceShape = deviceShape & deviceRegion;
    }
    const Rect backgroundRect = (shape.boundingRect() & viewport.renderRect()).toAlignedRect();
    if (deviceShape.isEmpty() || backgroundRect.isEmpty()) {
        return;
    }

    // a blit cannot convert between float and fixed point, match the output
    const GLenum format = renderTarget.texture() ? renderTarget.texture()->internalFormat() : GL_RGBA8;
    RenderData &render = m_windows[w].render[m_currentView];
    if (!render.blur[m_levels - 1][0].texture || !render.background.texture || render.background.texture->size() != backgroundRect.size() || render.background.texture->internalFormat() != format) {
        render.background = allocate(format, backgroundRect.size());
        bool ok = bool(render.background.framebuffer);
        for (int level = 0; level < m_levels; ++level) {
            const QSize size = (backgroundRect.size() / (2 << level)).expandedTo(QSize(1, 1));
            for (Offscreen &pass : render.blur[level]) {
                pass = allocate(format, size);
                ok = ok && pass.framebuffer;
            }
        }
        if (!ok) {
            qCWarning(KWIN_GPUBLUR) << "Failed to allocate offscreen textures";
            render = {};
            return;
        }
        // the new copy only fills where this frame repaints, get the rest next frame
        effects->addRepaint(backgroundRect);
    }

    // What is in the render target now is everything below this window. Parts outside the
    // repainted region would be stale there, those keep the previous frame's copy instead.
    Region dirty(backgroundRect);
    if (deviceRegion != Region::infinite()) {
        dirty = dirty & viewport.mapFromDeviceCoordinatesContained(deviceRegion);
    }
    for (const Rect &rect : dirty.rects()) {
        render.background.framebuffer->blitFromRenderTarget(renderTarget, viewport, rect, rect.translated(-backgroundRect.topLeft()));
    }

    VertexArrayBinder vao(m_vao);
    glDisable(GL_BLEND);
    GLTexture *source = render.background.texture.get();
    {
        ShaderBinder binder(m_blurShader.get());
        binder.shader()->setUniform("source", 0);
        for (int level = 0; level < m_levels; ++level) {
            for (const int axis : {0, 1}) {
                const Offscreen &target = render.blur[level][axis];
                const QSize size = target.texture->size();
                binder.shader()->setUniform("direction", axis == 0 ? QVector2D(m_spread / size.width(), 0) : QVector2D(0, m_spread / size.height()));
                GLFramebuffer::pushFramebuffer(target.framebuffer.get());
                source->bind();
                glDrawArrays(GL_TRIANGLES, 0, 3);
                source->unbind();
                GLFramebuffer::popFramebuffer();
                source = target.texture.get();
            }
        }
    }

    const qreal scale = viewport.scale();
    const RectF frame = w->frameGeometry();
    const float opacity = w->opacity() * data.opacity();
    ShaderBinder binder(m_onscreenShader.get());
    GLShader *shader = binder.shader();
    shader->setUniform("source", 0);
    shader->setUniform("projection", viewport.projectionMatrix());
    shader->setUniform("rect", QVector4D(backgroundRect.x() * scale, backgroundRect.y() * scale, backgroundRect.width() * scale, backgroundRect.height() * scale));
    shader->setUniform("box", QVector4D(frame.center().x() * scale, frame.center().y() * scale, frame.width() * scale / 2, frame.height() * scale / 2));
    shader->setUniform("cornerRadius", w->window()->borderRadius().scaled(scale).toVector());
    shader->setUniform("opacity", opacity * opacity);
    shader->setUniform("colorMatrix", m_colorMatrix);
    shader->setUniform("noise", m_noise);
    // the blur effect's noise grains grow with the screen DPI
    shader->setUniform("noiseScale", float(std::max(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0)));

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    source->bind();
    const Region clip = viewport.transform().map(deviceShape, renderTarget.transformedSize());
    const int height = GLFramebuffer::currentFramebuffer()->size().height();
    for (const Rect &rect : clip.rects()) {
        glScissor(rect.x(), height - (rect.y() + rect.height()), rect.width(), rect.height());
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    source->unbind();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

} // namespace KWin

#include "moc_gpublur.cpp"
