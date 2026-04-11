#include "slideio/viewer/ui/ViewportWidget.h"
#include "slideio/viewer/ui/ViewportController.h"

#include "slideio/viewer/core/CoordinateSystem.h"
#include "slideio/viewer/core/ISlideSource.h"
#include "slideio/viewer/core/ITileCache.h"
#include "slideio/viewer/core/TileData.h"
#include "slideio/viewer/core/TileKey.h"
#include "slideio/viewer/core/TilePyramid.h"
#include "slideio/viewer/core/Types.h"
#include "slideio/viewer/infra/LruTileCache.h"
#include "slideio/viewer/infra/SlideIOAdapter.h"
#include "slideio/viewer/infra/SlideIOAdapterPool.h"
#include "slideio/viewer/infra/TileLoadScheduler.h"

#include <QMetaObject>
#include <QMouseEvent>
#include <QTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QWheelEvent>

#include <mutex>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{

const char* kTileVertexShaderSource = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec4 uScreenRect;
uniform vec2 uViewportSize;

out vec2 vTexCoord;

void main()
{
    float screenX = uScreenRect.x + aPos.x * uScreenRect.z;
    float screenY = uScreenRect.y + aPos.y * uScreenRect.w;

    float ndcX = (screenX / uViewportSize.x) * 2.0 - 1.0;
    float ndcY = 1.0 - (screenY / uViewportSize.y) * 2.0;

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);

    vTexCoord = vec2(aPos.x, aPos.y);
}
)glsl";

const char* kTileFragmentShaderSource = R"glsl(
#version 330 core

in vec2 vTexCoord;

uniform sampler2D uTileTexture;
uniform float uAlpha;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(uTileTexture, vTexCoord);
    fragColor = vec4(texel.rgb, texel.a * uAlpha);
}
)glsl";

constexpr int kMaxTextureUploadsPerFrame = 8;
constexpr double kZoomInFactor = 1.25;
constexpr double kZoomOutFactor = 1.0 / 1.25;
constexpr double kPanPixels = 20.0;
constexpr double kWheelZoomFactor = 1.1;

} // anonymous namespace

namespace slideio::viewer::ui
{

struct ViewportWidget::Impl
{
    // OpenGL resources
    QOpenGLFunctions_3_3_Core* gl = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> tileShader;
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    // Texture management
    std::unordered_map<core::TileKey, GLuint> textures;
    std::vector<core::TileKey> pendingUploads;
    std::mutex pendingUploadsMutex;

    // Slide management
    std::shared_ptr<infra::SlideIOAdapterPool> adapterPool;
    std::shared_ptr<core::ITileCache> tileCache;
    std::shared_ptr<infra::TileLoadScheduler> scheduler;
    std::shared_ptr<core::TilePyramid> pyramid;
    std::unique_ptr<ViewportController> controller;
    core::SlideInfo slideInfo;
    bool slideOpen = false;

    // Mouse interaction
    bool isPanning = false;
    QPoint lastMousePos;

    // GL state
    bool glInitialized = false;

    void createQuadGeometry()
    {
        float vertices[] = {
            // Position (unit quad, two triangles)
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
        };

        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);

        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

        gl->glBindVertexArray(0);
        gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GLuint uploadTileTexture(const core::TileData& tile)
    {
        GLuint texId = 0;
        gl->glGenTextures(1, &texId);
        gl->glBindTexture(GL_TEXTURE_2D, texId);

        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        GLenum format = GL_RGB;
        GLenum internalFormat = GL_RGB8;
        int channels = tile.numChannels();

        if (channels == 1) {
            format = GL_RED;
            internalFormat = GL_R8;
            // Swizzle R -> RGB so the shader's texel.rgb reads grayscale
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        } else if (channels == 2) {
            format = GL_RG;
            internalFormat = GL_RG8;
        } else if (channels == 3) {
            format = GL_RGB;
            internalFormat = GL_RGB8;
        } else if (channels == 4) {
            format = GL_RGBA;
            internalFormat = GL_RGBA8;
        }

        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
                         tile.width(), tile.height(), 0, format, GL_UNSIGNED_BYTE,
                         tile.buffer().data());

        gl->glBindTexture(GL_TEXTURE_2D, 0);
        return texId;
    }

    void deleteTexture(const core::TileKey& key)
    {
        auto it = textures.find(key);
        if (it != textures.end()) {
            gl->glDeleteTextures(1, &it->second);
            textures.erase(it);
        }
    }

    void clearAllTextures()
    {
        for (auto& pair : textures) {
            gl->glDeleteTextures(1, &pair.second);
        }
        textures.clear();
        pendingUploads.clear();
    }

    // Returns true if there are still pending uploads remaining
    bool uploadPendingTextures()
    {
        if (!tileCache) {
            return false;
        }

        // Swap pending list under lock to minimize lock hold time
        std::vector<core::TileKey> toUpload;
        {
            std::lock_guard<std::mutex> lock(pendingUploadsMutex);
            toUpload.swap(pendingUploads);
        }

        if (toUpload.empty()) {
            return false;
        }

        int actualUploads = 0;
        size_t processed = 0;
        for (processed = 0; processed < toUpload.size(); ++processed) {
            if (actualUploads >= kMaxTextureUploadsPerFrame) {
                break;
            }
            const auto& key = toUpload[processed];
            if (textures.find(key) != textures.end()) {
                continue; // Already uploaded
            }
            auto tileData = tileCache->lookup(key);
            if (tileData && !tileData->isEmpty() && !tileData->isError()) {
                GLuint texId = uploadTileTexture(*tileData);
                textures[key] = texId;
                ++actualUploads;
            }
        }

        // Put unprocessed tiles back
        if (processed < toUpload.size()) {
            std::lock_guard<std::mutex> lock(pendingUploadsMutex);
            pendingUploads.insert(pendingUploads.end(),
                toUpload.begin() + static_cast<ptrdiff_t>(processed), toUpload.end());
            return true; // Still have pending
        }
        return false;
    }

    core::TileKey findFallbackTile(const core::TileKey& key) const
    {
        if (!pyramid || !tileCache) {
            return core::TileKey();
        }

        int level = key.level();
        int col = key.column();
        int row = key.row();

        for (int fallbackLevel = level + 1; fallbackLevel < pyramid->numLevels(); ++fallbackLevel) {
            const auto& currentLevelInfo = pyramid->levelInfo(level);
            const auto& fallbackLevelInfo = pyramid->levelInfo(fallbackLevel);

            if (currentLevelInfo.tileWidth <= 0 || currentLevelInfo.tileHeight <= 0 ||
                fallbackLevelInfo.tileWidth <= 0 || fallbackLevelInfo.tileHeight <= 0) {
                continue;
            }

            double scaleRatio = fallbackLevelInfo.scale / currentLevelInfo.scale;
            int fallbackCol = static_cast<int>(std::floor(col * scaleRatio));
            int fallbackRow = static_cast<int>(std::floor(row * scaleRatio));

            fallbackCol = std::max(0, std::min(fallbackCol, fallbackLevelInfo.tilesX - 1));
            fallbackRow = std::max(0, std::min(fallbackRow, fallbackLevelInfo.tilesY - 1));

            core::TileKey fallbackKey(fallbackLevel, fallbackCol, fallbackRow);

            if (textures.find(fallbackKey) != textures.end()) {
                return fallbackKey;
            }
        }

        return core::TileKey();
    }
};

ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

ViewportWidget::~ViewportWidget()
{
    makeCurrent();
    if (m_impl->glInitialized) {
        m_impl->clearAllTextures();
        if (m_impl->quadVAO) {
            m_impl->gl->glDeleteVertexArrays(1, &m_impl->quadVAO);
        }
        if (m_impl->quadVBO) {
            m_impl->gl->glDeleteBuffers(1, &m_impl->quadVBO);
        }
    }
    doneCurrent();
}

void ViewportWidget::openSlide(const std::string& filePath)
{
    closeSlide();

    try {
        m_impl->adapterPool = std::make_shared<infra::SlideIOAdapterPool>(filePath, 4);

        auto adapterLoan = m_impl->adapterPool->acquire();
        m_impl->slideInfo = adapterLoan->slideInfo();
        auto levels = adapterLoan->levels();

        m_impl->tileCache = std::make_shared<infra::LruTileCache>();

        m_impl->pyramid = std::make_shared<core::TilePyramid>(
            m_impl->slideInfo.width, m_impl->slideInfo.height, levels);

        m_impl->scheduler = std::make_shared<infra::TileLoadScheduler>(
            m_impl->adapterPool, m_impl->tileCache);

        m_impl->scheduler->setOnTileLoaded([this](const core::TileKey& key) {
            {
                std::lock_guard<std::mutex> lock(m_impl->pendingUploadsMutex);
                m_impl->pendingUploads.push_back(key);
            }
            // Marshal update() to the UI thread
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        });

        // Create controller WITHOUT a scheduler initially to prevent
        // premature tile requests during setup
        m_impl->controller = std::make_unique<ViewportController>(
            m_impl->pyramid, m_impl->tileCache, nullptr);

        m_impl->controller->setSlide(m_impl->slideInfo, *m_impl->pyramid);

        if (width() > 0 && height() > 0) {
            m_impl->controller->resize(width(), height());
            m_impl->controller->fitToSlide();
        }

        // Now connect the scheduler and request tiles once
        m_impl->controller->setScheduler(m_impl->scheduler);
        m_impl->controller->requestVisibleTiles();

        m_impl->slideOpen = true;

        spdlog::info("ViewportWidget::openSlide: slide {}x{}, {} levels, {} channels, magnification={}",
                     m_impl->slideInfo.width, m_impl->slideInfo.height,
                     m_impl->slideInfo.numZoomLevels, m_impl->slideInfo.numChannels,
                     m_impl->slideInfo.magnification);
        spdlog::info("ViewportWidget::openSlide: viewport {}x{}, scale={}, center=({},{})",
                     m_impl->controller->viewport().screenWidth(),
                     m_impl->controller->viewport().screenHeight(),
                     m_impl->controller->viewport().scale(),
                     m_impl->controller->viewport().centerX(),
                     m_impl->controller->viewport().centerY());
        auto keys = m_impl->controller->visibleTileKeys();
        spdlog::info("ViewportWidget::openSlide: {} visible tiles requested", keys.size());

        emit slideOpened(filePath);
        emit viewportChanged();
        update();
    } catch (const std::exception& ex) {
        spdlog::error("ViewportWidget::openSlide: exception: {}", ex.what());
        closeSlide();
    } catch (...) {
        spdlog::error("ViewportWidget::openSlide: unknown exception");
        closeSlide();
    }
}

void ViewportWidget::closeSlide()
{
    if (m_impl->scheduler) {
        m_impl->scheduler->stop();
    }

    m_impl->controller.reset();
    m_impl->scheduler.reset();
    m_impl->pyramid.reset();
    m_impl->tileCache.reset();
    m_impl->adapterPool.reset();

    if (m_impl->glInitialized) {
        makeCurrent();
        m_impl->clearAllTextures();
        doneCurrent();
    }

    m_impl->slideOpen = false;
    emit slideClosed();
    update();
}

bool ViewportWidget::isSlideOpen() const
{
    return m_impl->slideOpen;
}

ViewportController* ViewportWidget::controller() const
{
    return m_impl->controller.get();
}

void ViewportWidget::fitToSlide()
{
    if (m_impl->controller) {
        m_impl->controller->fitToSlide();
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::setActualPixels()
{
    if (m_impl->controller) {
        m_impl->controller->setActualPixels();
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::zoomIn()
{
    if (m_impl->controller) {
        double cx = width() / 2.0;
        double cy = height() / 2.0;
        m_impl->controller->zoomToPoint(cx, cy, kZoomInFactor);
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::zoomOut()
{
    if (m_impl->controller) {
        double cx = width() / 2.0;
        double cy = height() / 2.0;
        m_impl->controller->zoomToPoint(cx, cy, kZoomOutFactor);
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::panByPixels(double dx, double dy)
{
    if (m_impl->controller) {
        m_impl->controller->pan(dx, dy);
        emit viewportChanged();
        update();
    }
}

void ViewportWidget::initializeGL()
{
    m_impl->gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(QOpenGLContext::currentContext());
    if (!m_impl->gl) {
        return;
    }
    m_impl->gl->initializeOpenGLFunctions();

    m_impl->gl->glClearColor(0.251f, 0.251f, 0.251f, 1.0f); // #404040

    m_impl->gl->glEnable(GL_BLEND);
    m_impl->gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_impl->tileShader = std::make_unique<QOpenGLShaderProgram>();
    bool vsOk = m_impl->tileShader->addShaderFromSourceCode(QOpenGLShader::Vertex, kTileVertexShaderSource);
    if (!vsOk) {
        spdlog::error("ViewportWidget: vertex shader compilation failed: {}",
                      m_impl->tileShader->log().toStdString());
    }
    bool fsOk = m_impl->tileShader->addShaderFromSourceCode(QOpenGLShader::Fragment, kTileFragmentShaderSource);
    if (!fsOk) {
        spdlog::error("ViewportWidget: fragment shader compilation failed: {}",
                      m_impl->tileShader->log().toStdString());
    }
    bool linkOk = m_impl->tileShader->link();
    if (!linkOk) {
        spdlog::error("ViewportWidget: shader program link failed: {}",
                      m_impl->tileShader->log().toStdString());
    }
    spdlog::info("ViewportWidget: shader compiled: vs={} fs={} link={}", vsOk, fsOk, linkOk);

    m_impl->createQuadGeometry();
    spdlog::info("ViewportWidget: GL initialized, VAO={} VBO={}", m_impl->quadVAO, m_impl->quadVBO);

    m_impl->glInitialized = true;
}

void ViewportWidget::resizeGL(int w, int h)
{
    if (m_impl->gl) {
        m_impl->gl->glViewport(0, 0, w, h);
    }

    if (m_impl->controller) {
        m_impl->controller->resize(w, h);
        emit viewportChanged();
    }
}

void ViewportWidget::paintGL()
{
    if (!m_impl->gl || !m_impl->glInitialized) {
        return;
    }

    m_impl->gl->glClear(GL_COLOR_BUFFER_BIT);

    if (!m_impl->controller || !m_impl->slideOpen) {
        return;
    }

    bool morePending = m_impl->uploadPendingTextures();

    auto visibleKeys = m_impl->controller->visibleTileKeys();
    const auto& viewport = m_impl->controller->viewport();

    static int paintCount = 0;
    if (++paintCount <= 5 || paintCount % 100 == 0) {
        spdlog::info("paintGL[{}]: {} visible tiles, {} textures uploaded, scale={:.4f}, viewport={}x{}",
                     paintCount, visibleKeys.size(), m_impl->textures.size(),
                     viewport.scale(), viewport.screenWidth(), viewport.screenHeight());
    }

    if (visibleKeys.empty()) {
        return;
    }

    m_impl->tileShader->bind();
    m_impl->tileShader->setUniformValue("uViewportSize",
        static_cast<float>(width()), static_cast<float>(height()));

    m_impl->gl->glBindVertexArray(m_impl->quadVAO);
    m_impl->gl->glActiveTexture(GL_TEXTURE0);
    m_impl->tileShader->setUniformValue("uTileTexture", 0);

    core::CoordinateSystem coordSystem(*m_impl->pyramid);

    int tilesRendered = 0;
    int tilesSkipped = 0;
    for (const auto& key : visibleKeys) {
        auto screenRect = coordSystem.tileScreenRect(key, viewport);

        GLuint texId = 0;
        float alpha = 1.0f;

        auto texIt = m_impl->textures.find(key);
        if (texIt != m_impl->textures.end()) {
            texId = texIt->second;
        } else {
            // Not in texture map yet — check if it's in the tile cache and upload directly
            auto tileData = m_impl->tileCache->lookup(key);
            if (tileData && !tileData->isEmpty() && !tileData->isError()) {
                texId = m_impl->uploadTileTexture(*tileData);
                m_impl->textures[key] = texId;
            } else {
                if (paintCount <= 5) {
                    spdlog::info("  SKIP tile {} - cached={} empty={} error={}", key.toString(),
                                 tileData != nullptr,
                                 tileData ? tileData->isEmpty() : true,
                                 tileData ? tileData->isError() : false);
                }
                ++tilesSkipped;
                continue;
            }
        }

        if (paintCount <= 3) {
            spdlog::info("  tile {} -> screenRect({:.1f},{:.1f},{:.1f},{:.1f}) texId={} alpha={:.1f}",
                         key.toString(), screenRect.x, screenRect.y,
                         screenRect.width, screenRect.height, texId, alpha);
        }

        m_impl->tileShader->setUniformValue("uScreenRect",
            static_cast<float>(screenRect.x),
            static_cast<float>(screenRect.y),
            static_cast<float>(screenRect.width),
            static_cast<float>(screenRect.height));
        m_impl->tileShader->setUniformValue("uAlpha", alpha);

        m_impl->gl->glBindTexture(GL_TEXTURE_2D, texId);
        m_impl->gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        ++tilesRendered;
    }

    if (paintCount <= 5 || paintCount % 100 == 0) {
        spdlog::info("paintGL[{}]: rendered={} skipped={}", paintCount, tilesRendered, tilesSkipped);
    }

    m_impl->gl->glBindVertexArray(0);
    m_impl->gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_impl->tileShader->release();

    // If tiles were skipped (not yet loaded), schedule repaints until all are rendered
    if (tilesSkipped > 0 || morePending) {
        // Use a short timer to allow tile workers to make progress
        QTimer::singleShot(16, this, [this]() { update(); });
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_impl->isPanning = true;
        m_impl->lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QOpenGLWidget::mousePressEvent(event);
    }
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_impl->isPanning) {
        m_impl->isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QOpenGLWidget::mouseReleaseEvent(event);
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_impl->isPanning && m_impl->controller) {
        QPoint delta = event->pos() - m_impl->lastMousePos;
        m_impl->controller->pan(static_cast<double>(delta.x()),
                                static_cast<double>(delta.y()));
        m_impl->lastMousePos = event->pos();
        emit viewportChanged();
        update();
        event->accept();
    }

    if (m_impl->controller) {
        double slideX = 0.0;
        double slideY = 0.0;
        m_impl->controller->screenToSlide(
            static_cast<double>(event->pos().x()),
            static_cast<double>(event->pos().y()),
            slideX, slideY);
        emit cursorMoved(slideX, slideY);
    }

    if (!event->isAccepted()) {
        QOpenGLWidget::mouseMoveEvent(event);
    }
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_impl->controller) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    double angleDelta = event->angleDelta().y();
    if (std::abs(angleDelta) < 1.0) {
        event->accept();
        return;
    }

    double steps = angleDelta / 120.0;
    double factor = std::pow(kWheelZoomFactor, steps);

    auto pos = event->position();
    m_impl->controller->zoomToPoint(pos.x(), pos.y(), factor);
    emit viewportChanged();
    update();
    event->accept();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_impl->controller) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Left:
        m_impl->controller->pan(kPanPixels, 0.0);
        break;
    case Qt::Key_Right:
        m_impl->controller->pan(-kPanPixels, 0.0);
        break;
    case Qt::Key_Up:
        m_impl->controller->pan(0.0, kPanPixels);
        break;
    case Qt::Key_Down:
        m_impl->controller->pan(0.0, -kPanPixels);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        return; // zoomIn already emits and updates
    case Qt::Key_Minus:
        zoomOut();
        return; // zoomOut already emits and updates
    default:
        handled = false;
        break;
    }

    if (handled) {
        emit viewportChanged();
        update();
        event->accept();
    } else {
        QOpenGLWidget::keyPressEvent(event);
    }
}

} // namespace slideio::viewer::ui
