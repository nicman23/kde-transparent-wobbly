/*
    Wobbly windows with the spring physics, the mesh and a frosted background all on the GPU.

    Physics ported from KWin's wobblywindows effect:
    SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpuwobbly.h"
#include "blursettings.h"
#include "core/output.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "gpuwobblyconfig.h"
#include "opengl/eglcontext.h"
#include "opengl/glframebuffer.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "scene/scene.h"
#include "scene/windowitem.h"

#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(KWIN_GPUWOBBLY, "kwin_effect_gpuwobbly", QtWarningMsg)

namespace KWin
{

// wobblywindows' presets, picked by WobblynessLevel
static constexpr GpuWobblyEffect::ParameterSet s_presets[5] = {
    {0.15, 0.80, 0.10, 20, 20, 0.0, 1000.0, 0.5, 0.0, 1000.0, 0.5},
    {0.10, 0.85, 0.10, 20, 20, 0.0, 1000.0, 0.5, 0.0, 1000.0, 0.5},
    {0.06, 0.90, 0.10, 20, 20, 0.0, 1000.0, 0.5, 0.0, 1000.0, 0.5},
    {0.03, 0.92, 0.20, 20, 20, 0.0, 1000.0, 0.5, 0.0, 1000.0, 0.5},
    {0.01, 0.97, 0.25, 20, 20, 0.0, 1000.0, 0.5, 0.0, 1000.0, 0.5},
};
static constexpr int s_integrationStep = 10; // ms
static constexpr int s_maxSteps = 50; // after a stall, drop time instead of simulating all of it

// ponytail: fixed bound on how far a grid point may lag behind (logical px), it is also the repaint
// margin; make it a setting if fast flings visibly hit it
static constexpr qreal s_maxOffset = 300;


// GLTexture::allocate() hands out 8 bit BGRA on GLES whatever format is asked for
class FloatTexture : public GLTexture
{
public:
    static std::unique_ptr<GLTexture> allocate(const QSize &size)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, size.width(), size.height());
        glBindTexture(GL_TEXTURE_2D, 0);
        return std::unique_ptr<GLTexture>(new FloatTexture(id, size));
    }

private:
    FloatTexture(GLuint id, const QSize &size)
        : GLTexture(GL_TEXTURE_2D, id, GL_RGBA32F, size, 1, true, OutputTransform())
    {
    }
};

static QVector4D toVector(const RectF &rect)
{
    return QVector4D(rect.x(), rect.y(), rect.width(), rect.height());
}

static std::unique_ptr<GLShader> loadShader(const QString &vertex, const QString &fragment)
{
    QFile vertexFile(QStringLiteral(":/effects/gpuwobbly/shaders/") + vertex);
    QFile fragmentFile(QStringLiteral(":/effects/gpuwobbly/shaders/") + fragment);
    if (!vertexFile.open(QIODevice::ReadOnly) || !fragmentFile.open(QIODevice::ReadOnly)) {
        qCWarning(KWIN_GPUWOBBLY) << "Missing shader" << vertex << fragment;
        return nullptr;
    }
    auto shader = ShaderManager::instance()->loadShaderFromCode(vertexFile.readAll(), fragmentFile.readAll());
    if (!shader) {
        qCWarning(KWIN_GPUWOBBLY) << "Failed to build shader" << vertex << fragment;
    }
    return shader;
}

GpuWobblyEffect::Wobble::~Wobble()
{
    if (fence) {
        glDeleteSync(fence);
    }
    if (pbo) {
        glDeleteBuffers(1, &pbo);
    }
    QObject::disconnect(damaged);
}

GpuWobblyEffect::GpuWobblyEffect()
{
    GpuWobblyConfig::instance(effects->config());
    reconfigure(ReconfigureAll);

    m_physicsShader = loadShader(QStringLiteral("fullscreen.vert"), QStringLiteral("physics.frag"));
    m_blurShader = loadShader(QStringLiteral("fullscreen.vert"), QStringLiteral("blur.frag"));
    m_compositeShader = loadShader(QStringLiteral("composite.vert"), QStringLiteral("composite.frag"));
    glGenVertexArrays(1, &m_vao);

    connect(effects, &EffectsHandler::windowAdded, this, &GpuWobblyEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &GpuWobblyEffect::remove);
    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        slotWindowAdded(window);
    }
}

GpuWobblyEffect::~GpuWobblyEffect()
{
    m_windows.clear();
    glDeleteVertexArrays(1, &m_vao);
}

void GpuWobblyEffect::reconfigure(ReconfigureFlags)
{
    GpuWobblyConfig::self()->read();

    if (GpuWobblyConfig::settings() != QStringLiteral("Custom")) {
        m_params = s_presets[std::min(GpuWobblyConfig::wobblynessLevel(), 4u)];
        if (GpuWobblyConfig::advancedMode()) {
            m_params.stiffness = GpuWobblyConfig::stiffness() / 100.0;
            m_params.drag = GpuWobblyConfig::drag() / 100.0;
            m_params.moveFactor = GpuWobblyConfig::moveFactor() / 100.0;
        }
    } else { // Custom method, read all values from config file.
        m_params = {
            .stiffness = float(GpuWobblyConfig::stiffness() / 100.0),
            .drag = float(GpuWobblyConfig::drag() / 100.0),
            .moveFactor = float(GpuWobblyConfig::moveFactor() / 100.0),
            .xTessellation = std::max(1, int(GpuWobblyConfig::xTessellation())),
            .yTessellation = std::max(1, int(GpuWobblyConfig::yTessellation())),
            .minVelocity = float(GpuWobblyConfig::minVelocity()),
            .maxVelocity = float(GpuWobblyConfig::maxVelocity()),
            .stopVelocity = float(GpuWobblyConfig::stopVelocity()),
            .minAcceleration = float(GpuWobblyConfig::minAcceleration()),
            .maxAcceleration = float(GpuWobblyConfig::maxAcceleration()),
            .stopAcceleration = float(GpuWobblyConfig::stopAcceleration()),
        };
    }

    m_opacity = GpuWobblyConfig::movingOpacity() / 100.0;

    // the frost blurs like GPU Blur, with its settings
    const BlurSettings blur = readBlurSettings(effects->config());
    m_blurLevels = blur.levels;
    m_blurSpread = blur.spread;
    m_noise = blur.noise;
    m_colorMatrix = blur.colorMatrix;
    m_background = {}; // reallocated with the new level count
    m_moveWobble = GpuWobblyConfig::moveWobble();
    m_resizeWobble = GpuWobblyConfig::resizeWobble();
}

bool GpuWobblyEffect::supported()
{
    return effects->isOpenGLCompositing() && effects->animationsSupported();
}

bool GpuWobblyEffect::isActive() const
{
    return !m_windows.empty();
}

GpuWobblyEffect::Offscreen GpuWobblyEffect::allocate(GLenum format, const QSize &size, GLenum filter)
{
    Offscreen ret;
    ret.texture = format == GL_RGBA32F ? FloatTexture::allocate(size) : GLTexture::allocate(format, size);
    if (!ret.texture) {
        return {};
    }
    ret.texture->setFilter(filter);
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

void GpuWobblyEffect::slotWindowAdded(EffectWindow *w)
{
    connect(w, &EffectWindow::windowStartUserMovedResized, this, &GpuWobblyEffect::startMovedResized);
    connect(w, &EffectWindow::windowStepUserMovedResized, this, &GpuWobblyEffect::stepMovedResized);
    connect(w, &EffectWindow::windowFinishUserMovedResized, this, &GpuWobblyEffect::finishMovedResized);
}

void GpuWobblyEffect::startMovedResized(EffectWindow *w)
{
    if (w->isSpecialWindow() || !((w->isUserMove() && m_moveWobble) || (w->isUserResize() && m_resizeWobble))) {
        return;
    }
    if (!m_physicsShader || !m_blurShader || !m_compositeShader) {
        return;
    }

    std::unique_ptr<Wobble> &slot = m_windows[w];
    if (!slot) {
        slot = std::make_unique<Wobble>();
        slot->rect = w->frameGeometry();
        slot->itemEffect = ItemEffect(w->windowItem());
        slot->damaged = connect(w, &EffectWindow::windowDamaged, this, [this](EffectWindow *window) {
            if (const auto it = m_windows.find(window); it != m_windows.end()) {
                it->second->windowDirty = true;
            }
        });
    }

    Wobble &wobble = *slot;
    wobble.moving = true;

    // pin the grid point closest to the cursor
    const RectF rect = w->frameGeometry();
    const QPointF cursor = cursorPos();
    const int i = std::clamp<int>(std::lround((cursor.x() - rect.x()) * 3 / rect.width()), 0, 3);
    const int j = std::clamp<int>(std::lround((cursor.y() - rect.y()) * 3 / rect.height()), 0, 3);
    wobble.constraints |= 1u << (j * 4 + i);

    if (w->isUserResize()) {
        // on a resize, do not allow any edges to wobble until it has been moved from its original location
        wobble.edges = 0;
        wobble.resizeOriginalRect = rect;
    } else {
        wobble.edges = 0xf;
    }
}

void GpuWobblyEffect::stepMovedResized(EffectWindow *w)
{
    const auto it = m_windows.find(w);
    if (it == m_windows.end()) {
        return;
    }
    Wobble &wobble = *it->second;
    const RectF rect = w->frameGeometry();
    const RectF &original = wobble.resizeOriginalRect;
    if (rect.y() != original.y()) {
        wobble.edges |= 1;
    }
    if (rect.bottom() != original.bottom()) {
        wobble.edges |= 2;
    }
    if (rect.x() != original.x()) {
        wobble.edges |= 4;
    }
    if (rect.right() != original.right()) {
        wobble.edges |= 8;
    }
}

void GpuWobblyEffect::finishMovedResized(EffectWindow *w)
{
    stepMovedResized(w);
    if (const auto it = m_windows.find(w); it != m_windows.end()) {
        it->second->moving = false;
    }
}

void GpuWobblyEffect::remove(EffectWindow *w)
{
    const auto it = m_windows.find(w);
    if (it == m_windows.end()) {
        return;
    }
    if (!EglContext::currentContext()) {
        (void)effects->openglContext()->makeCurrent();
    }
    m_windows.erase(it);
}

RectF GpuWobblyEffect::bounds(EffectWindow *w) const
{
    return w->expandedGeometry().adjusted(-s_maxOffset, -s_maxOffset, s_maxOffset, s_maxOffset);
}

void GpuWobblyEffect::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    if (const auto it = m_windows.find(w); it != m_windows.end()) {
        Wobble &wobble = *it->second;
        pollEnergy(wobble);
        if (!wobble.moving && !wobble.wobbling) {
            remove(w);
            effects->addRepaintFull();
        } else {
            // Only move forward: with several outputs the views present at different times.
            const std::chrono::nanoseconds now = view->nextPresentationTimestamp();
            if (!wobble.lastTimestamp) {
                wobble.lastTimestamp = now;
            } else if (now > *wobble.lastTimestamp) {
                const auto delta = now - *wobble.lastTimestamp;
                wobble.lastTimestamp = now;
                if (!simulate(w, wobble, delta)) {
                    qCWarning(KWIN_GPUWOBBLY) << "Float render targets are not supported, not wobbling";
                    remove(w);
                    effects->prePaintWindow(view, w, data);
                    return;
                }
            }
            if (wobble.state[0].texture) {
                data.setTransformed();
                m_updateRegion = m_updateRegion.united(bounds(w).toAlignedRect());
            }
        }
    }

    effects->prePaintWindow(view, w, data);
}

void GpuWobblyEffect::pollEnergy(Wobble &wobble)
{
    if (!wobble.fence) {
        return;
    }
    const GLenum status = glClientWaitSync(wobble.fence, 0, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        return;
    }
    glDeleteSync(wobble.fence);
    wobble.fence = nullptr;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, wobble.pbo);
    if (const auto energy = static_cast<const float *>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 4 * sizeof(float), GL_MAP_READ_BIT))) {
        qCDebug(KWIN_GPUWOBBLY) << "energy" << energy[0] << energy[1];
        wobble.wobbling = !(energy[0] < m_params.stopAcceleration && energy[1] < m_params.stopVelocity);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

bool GpuWobblyEffect::simulate(EffectWindow *w, Wobble &wobble, std::chrono::nanoseconds delta)
{
    if (!wobble.state[0].texture) {
        for (Offscreen &state : wobble.state) {
            state = allocate(GL_RGBA32F, QSize(5, 4), GL_NEAREST);
            if (!state.framebuffer) {
                return false;
            }
        }
        glGenBuffers(1, &wobble.pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, wobble.pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4 * sizeof(float), nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    const double elapsed = std::chrono::duration<double, std::milli>(delta).count();
    int steps = std::ceil(elapsed / s_integrationStep);
    double lastStep = elapsed - (steps - 1) * s_integrationStep;
    if (steps > s_maxSteps) {
        steps = s_maxSteps;
        lastStep = s_integrationStep;
    }

    const RectF rect = w->frameGeometry();
    const Offscreen &source = wobble.state[wobble.current];
    const Offscreen &target = wobble.state[wobble.current ^ 1];

    VertexArrayBinder vao(m_vao);
    ShaderBinder binder(m_physicsShader.get());
    GLShader *shader = binder.shader();
    shader->setUniform("state", 0);
    shader->setUniform("oldRect", toVector(wobble.rect));
    shader->setUniform("newRect", toVector(rect));
    shader->setUniform("constraints", int(wobble.constraints));
    shader->setUniform("edges", int(wobble.edges));
    shader->setUniform("reset", int(wobble.reset));
    shader->setUniform("steps", steps);
    shader->setUniform("dt", float(s_integrationStep));
    shader->setUniform("lastDt", float(lastStep));
    shader->setUniform("stiffness", m_params.stiffness);
    shader->setUniform("drag", m_params.drag);
    shader->setUniform("moveFactor", m_params.moveFactor);
    shader->setUniform("velocityBounds", QVector2D(m_params.minVelocity, m_params.maxVelocity));
    shader->setUniform("accelerationBounds", QVector2D(m_params.minAcceleration, m_params.maxAcceleration));
    shader->setUniform("maxOffset", float(s_maxOffset));

    GLFramebuffer::pushFramebuffer(target.framebuffer.get());
    glDisable(GL_BLEND);
    source.texture->bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    source.texture->unbind();
    if (!wobble.fence) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, wobble.pbo);
        glReadPixels(4, 0, 1, 1, GL_RGBA, GL_FLOAT, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        wobble.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    GLFramebuffer::popFramebuffer();

    wobble.current ^= 1;
    wobble.rect = rect;
    wobble.reset = false;
    return true;
}

bool GpuWobblyEffect::capture(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, Wobble &wobble)
{
    const qreal scale = viewport.scale();
    const RectF geometry = w->expandedGeometry();
    const QSize size = (geometry.size() * scale).toSize();
    if (size.isEmpty()) {
        wobble.window = {};
        return true;
    }
    if (!wobble.window.texture || wobble.window.texture->size() != size) {
        wobble.window = allocate(GL_RGBA8, size, GL_LINEAR);
        if (!wobble.window.framebuffer) {
            return false;
        }
        wobble.windowDirty = true;
    }
    // capture in the encoding of the output, then window and background blend without conversions
    if (wobble.windowColor != renderTarget.colorDescription()) {
        wobble.windowColor = renderTarget.colorDescription();
        wobble.windowDirty = true;
    }
    if (!wobble.windowDirty) {
        return true;
    }

    RenderTarget target(wobble.window.framebuffer.get(), wobble.windowColor);
    RenderViewport targetViewport(geometry, scale, target, QPoint());
    GLFramebuffer::pushFramebuffer(wobble.window.framebuffer.get());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    WindowPaintData data;
    data.setOpacity(1.0);
    wobble.capturing = true;
    const bool ok = effects->drawWindow(target, targetViewport, w, PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT, Region::infinite(), data);
    wobble.capturing = false;
    GLFramebuffer::popFramebuffer();

    wobble.windowDirty = !ok;
    return ok;
}

GLTexture *GpuWobblyEffect::blurBackground(const RenderTarget &renderTarget, const RenderViewport &viewport, const Region &deviceRegion, const Rect &backgroundRect)
{
    // a blit cannot convert between float and fixed point, match the output
    const GLenum format = renderTarget.texture() ? renderTarget.texture()->internalFormat() : GL_RGBA8;
    if (!m_background.texture || m_background.texture->size() != backgroundRect.size() || m_background.texture->internalFormat() != format) {
        m_background = allocate(format, backgroundRect.size(), GL_LINEAR);
        bool ok = bool(m_background.framebuffer);
        for (int level = 0; level < m_blurLevels; ++level) {
            const QSize size = (backgroundRect.size() / (2 << level)).expandedTo(QSize(1, 1));
            for (Offscreen &pass : m_blur[level]) {
                pass = allocate(format, size, GL_LINEAR);
                ok = ok && pass.framebuffer;
            }
        }
        if (!ok) {
            m_background = {};
            return nullptr;
        }
    }

    // What is in the render target now is everything below this window. Parts outside the
    // repainted region would be stale there, those keep the previous frame's copy instead.
    Region visible = Region(backgroundRect) & viewport.renderRect().toAlignedRect();
    if (deviceRegion != Region::infinite()) {
        visible = visible & viewport.mapFromDeviceCoordinatesContained(deviceRegion);
    }
    for (const Rect &rect : visible.rects()) {
        m_background.framebuffer->blitFromRenderTarget(renderTarget, viewport, rect, rect.translated(-backgroundRect.topLeft()));
    }

    VertexArrayBinder vao(m_vao);
    ShaderBinder binder(m_blurShader.get());
    binder.shader()->setUniform("source", 0);
    glDisable(GL_BLEND);
    GLTexture *source = m_background.texture.get();
    for (int level = 0; level < m_blurLevels; ++level) {
        for (const int axis : {0, 1}) {
            const Offscreen &target = m_blur[level][axis];
            const QSize size = target.texture->size();
            binder.shader()->setUniform("direction", axis == 0 ? QVector2D(m_blurSpread / size.width(), 0) : QVector2D(0, m_blurSpread / size.height()));
            GLFramebuffer::pushFramebuffer(target.framebuffer.get());
            source->bind();
            glDrawArrays(GL_TRIANGLES, 0, 3);
            source->unbind();
            GLFramebuffer::popFramebuffer();
            source = target.texture.get();
        }
    }
    return source;
}

bool GpuWobblyEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data)
{
    const auto it = m_windows.find(w);
    if (it == m_windows.end() || it->second->capturing || !it->second->state[0].texture) {
        return effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    }
    Wobble &wobble = *it->second;

    if (!capture(renderTarget, viewport, w, wobble)) {
        return false;
    }
    // The bound keeps its size while the window moves, so the textures are not reallocated per frame.
    // It is all repainted every frame, while pixels beyond it may be stale: the blur clamps at its
    // edge instead of reading further, which is far enough from the window not to show.
    const Rect backgroundRect = bounds(w).toAlignedRect();
    if (!wobble.window.texture || !backgroundRect.intersects(viewport.renderRect().toAlignedRect())) {
        return true;
    }
    GLTexture *background = blurBackground(renderTarget, viewport, deviceRegion, backgroundRect);
    if (!background) {
        return effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    }

    VertexArrayBinder vao(m_vao);
    ShaderBinder binder(m_compositeShader.get());
    GLShader *shader = binder.shader();
    shader->setUniform("state", 0);
    shader->setUniform("windowTexture", 1);
    shader->setUniform("background", 2);
    shader->setUniform("projection", viewport.projectionMatrix());
    shader->setUniform("scale", float(viewport.scale()));
    shader->setUniform("frameRect", toVector(w->frameGeometry()));
    shader->setUniform("expandedRect", toVector(w->expandedGeometry()));
    shader->setUniform("backgroundRect", toVector(RectF(backgroundRect)));
    shader->setUniform("grid", QVector2D(m_params.xTessellation, m_params.yTessellation));
    shader->setUniform("maxOffset", float(s_maxOffset));
    shader->setUniform("opacity", float(data.opacity() * m_opacity));
    shader->setUniform("colorMatrix", m_colorMatrix);
    shader->setUniform("noise", m_noise);
    // GPU Blur's noise grains grow with the screen DPI
    shader->setUniform("noiseScale", float(std::max(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0)));

    glActiveTexture(GL_TEXTURE2);
    background->bind();
    glActiveTexture(GL_TEXTURE1);
    wobble.window.texture->bind();
    glActiveTexture(GL_TEXTURE0);
    wobble.state[wobble.current].texture->bind();

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const int count = m_params.xTessellation * m_params.yTessellation * 6;
    if (deviceRegion == Region::infinite()) {
        glDrawArrays(GL_TRIANGLES, 0, count);
    } else {
        const Region clipRegion = viewport.transform().map(deviceRegion, renderTarget.transformedSize());
        const int height = GLFramebuffer::currentFramebuffer()->size().height();
        glEnable(GL_SCISSOR_TEST);
        for (const Rect &rect : clipRegion.rects()) {
            glScissor(rect.x(), height - (rect.y() + rect.height()), rect.width(), rect.height());
            glDrawArrays(GL_TRIANGLES, 0, count);
        }
        glDisable(GL_SCISSOR_TEST);
    }
    glDisable(GL_BLEND);

    for (const GLenum unit : {GL_TEXTURE2, GL_TEXTURE1, GL_TEXTURE0}) {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return true;
}

void GpuWobblyEffect::postPaintScreen()
{
    if (!m_updateRegion.isEmpty()) {
        effects->addRepaint(m_updateRegion);
        m_updateRegion = Region();
    }

    effects->postPaintScreen();
}

} // namespace KWin

#include "moc_gpuwobbly.cpp"
