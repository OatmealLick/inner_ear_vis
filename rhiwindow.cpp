// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rhiwindow.h"

#include <iostream>
#include <QPlatformSurfaceEvent>
#include <QPainter>
#include <QFile>
#include <rhi/qshader.h>
#include <assimp/Importer.hpp>

#include "assimp/postprocess.h"
#include "assimp/scene.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "optional"

//! [rhiwindow-ctor]
RhiWindow::RhiWindow(QRhi::Implementation graphicsApi)
    : m_graphicsApi(graphicsApi) {
    switch (graphicsApi) {
        case QRhi::OpenGLES2:
            setSurfaceType(OpenGLSurface);
            break;
        case QRhi::D3D11:
        case QRhi::D3D12:
            setSurfaceType(Direct3DSurface);
            break;
        case QRhi::Metal:
            setSurfaceType(MetalSurface);
            break;
        default:
            break;
    }
}

//! [rhiwindow-ctor]

QString RhiWindow::graphicsApiName() const {
    switch (m_graphicsApi) {
        case QRhi::Null:
            return QLatin1String("Null (no output)");
        case QRhi::OpenGLES2:
            return QLatin1String("OpenGL");
        case QRhi::Vulkan:
            return QLatin1String("Vulkan");
        case QRhi::D3D11:
            return QLatin1String("Direct3D 11");
        case QRhi::D3D12:
            return QLatin1String("Direct3D 12");
        case QRhi::Metal:
            return QLatin1String("Metal");
    }
    return QString();
}

//! [expose]
void RhiWindow::exposeEvent(QExposeEvent *) {
    // initialize and start rendering when the window becomes usable for graphics purposes
    if (isExposed() && !m_initialized) {
        init();
        resizeSwapChain();
        m_initialized = true;
    }

    const QSize surfaceSize = m_hasSwapChain ? m_sc->surfacePixelSize() : QSize();

    // stop pushing frames when not exposed (or size is 0)
    if ((!isExposed() || (m_hasSwapChain && surfaceSize.isEmpty())) && m_initialized && !m_notExposed)
        m_notExposed = true;

    // Continue when exposed again and the surface has a valid size. Note that
    // surfaceSize can be (0, 0) even though size() reports a valid one, hence
    // trusting surfacePixelSize() and not QWindow.
    if (isExposed() && m_initialized && m_notExposed && !surfaceSize.isEmpty()) {
        m_notExposed = false;
        m_newlyExposed = true;
    }

    // always render a frame on exposeEvent() (when exposed) in order to update
    // immediately on window resize.
    if (isExposed() && !surfaceSize.isEmpty())
        render();
}

//! [expose]

//! [event]
bool RhiWindow::event(QEvent *e) {
    switch (e->type()) {
        case QEvent::UpdateRequest:
            render();
            break;

        case QEvent::PlatformSurface:
            // this is the proper time to tear down the swapchain (while the native window and surface are still around)
            if (static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                releaseSwapChain();
            break;

        case QEvent::MouseMove:
            handleMouseMove(static_cast<QMouseEvent *>(e));
            break;
        case QEvent::MouseButtonPress:
            handleMouseButtonPress(static_cast<QMouseEvent *>(e));
            break;
        case QEvent::MouseButtonRelease:
            handleMouseButtonRelease(static_cast<QMouseEvent *>(e));
            break;
        case QEvent::Wheel:
            handleWheel(static_cast<QWheelEvent *>(e));
            break;
        default:
            break;
    }

    return QWindow::event(e);
}

//! [event]

//! [rhi-init]
void RhiWindow::init() {
    if (m_graphicsApi == QRhi::Null) {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
    }

#if QT_CONFIG(opengl)
    if (m_graphicsApi == QRhi::OpenGLES2) {
        m_fallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = m_fallbackSurface.get();
        params.window = this;
        m_rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
    }
#endif

#ifdef Q_OS_WIN
    if (m_graphicsApi == QRhi::D3D11) {
        QRhiD3D11InitParams params;
        // Enable the debug layer, if available. This is optional
        // and should be avoided in production builds.
        params.enableDebugLayer = true;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &params));
    } else if (m_graphicsApi == QRhi::D3D12) {
        QRhiD3D12InitParams params;
        // Enable the debug layer, if available. This is optional
        // and should be avoided in production builds.
        params.enableDebugLayer = true;
        m_rhi.reset(QRhi::create(QRhi::D3D12, &params));
    }
#endif

#if !QT_NO_METAL
    if (m_graphicsApi == QRhi::Metal) {
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
    }
#endif

    if (!m_rhi)
        qFatal("Failed to create RHI backend");
    //! [rhi-init]

    //! [swapchain-init]
    m_sc.reset(m_rhi->newSwapChain());
    m_ds.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil,
                                      QSize(), // no need to set the size here, due to UsedWithSwapChainOnly
                                      1,
                                      QRhiRenderBuffer::UsedWithSwapChainOnly));
    m_sc->setWindow(this);
    m_sc->setDepthStencil(m_ds.get());
    m_rp.reset(m_sc->newCompatibleRenderPassDescriptor());
    m_sc->setRenderPassDescriptor(m_rp.get());
    //! [swapchain-init]

    customInit();
}

//! [swapchain-resize]
void RhiWindow::resizeSwapChain() {
    m_hasSwapChain = m_sc->createOrResize(); // also handles m_ds

    const QSize outputSize = m_sc->currentPixelSize();
    m_viewProjection = m_rhi->clipSpaceCorrMatrix();
    m_viewProjection.perspective(45.0f, outputSize.width() / (float) outputSize.height(), 0.01f, 1000.0f);
    m_viewProjection.lookAt(
        QVector3D(0, 0, m_zoom),
        QVector3D(0, 0, 0),
        QVector3D(0, 1, 0)
    );
    m_viewProjection.rotate(m_rotationAngles.y(), -1, 0, 0);
    m_viewProjection.rotate(m_rotationAngles.x(), 0, 1, 0);
}

//! [swapchain-resize]

void RhiWindow::releaseSwapChain() {
    if (m_hasSwapChain) {
        m_hasSwapChain = false;
        m_sc->destroy();
    }
}

//! [render-precheck]
void RhiWindow::render() {
    if (!m_hasSwapChain || m_notExposed)
        return;
    //! [render-precheck]

    //! [render-resize]
    // If the window got resized or newly exposed, resize the swapchain. (the
    // newly-exposed case is not actually required by some platforms, but is
    // here for robustness and portability)
    //
    // This (exposeEvent + the logic here) is the only safe way to perform
    // resize handling. Note the usage of the RHI's surfacePixelSize(), and
    // never QWindow::size(). (the two may or may not be the same under the hood,
    // depending on the backend and platform)
    //
    if (m_sc->currentPixelSize() != m_sc->surfacePixelSize() || m_newlyExposed) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
        m_newlyExposed = false;
    }
    //! [render-resize]

    //! [beginframe]
    QRhi::FrameOpResult result = m_rhi->beginFrame(m_sc.get());
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
        result = m_rhi->beginFrame(m_sc.get());
    }
    if (result != QRhi::FrameOpSuccess) {
        qWarning("beginFrame failed with %d, will retry", result);
        requestUpdate();
        return;
    }

    customRender();
    //! [beginframe]

    //! [request-update]
    m_rhi->endFrame(m_sc.get());

    // Always request the next frame via requestUpdate(). On some platforms this is backed
    // by a platform-specific solution, e.g. CVDisplayLink on macOS, which is potentially
    // more efficient than a timer, queued metacalls, etc.
    requestUpdate();
}

static QShader getShader(const QString &name) {
    QFile f(name);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());

    return QShader();
}

//! [getshader]

HelloWindow::HelloWindow(QRhi::Implementation graphicsApi)
    : RhiWindow(graphicsApi) {
}

//! [ensure-texture]
// void HelloWindow::ensureFullscreenTexture(const QSize &pixelSize, QRhiResourceUpdateBatch *u) {
//     if (m_texture && m_texture->pixelSize() == pixelSize)
//         return;
//
//     if (!m_texture)
//         m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, pixelSize));
//     else
//         m_texture->setPixelSize(pixelSize);
//
//     m_texture->create();
//
//     QImage image(pixelSize, QImage::Format_RGBA8888_Premultiplied);
//     image.setDevicePixelRatio(devicePixelRatio());
//     //! [ensure-texture]
//     QPainter painter(&image);
//     painter.fillRect(QRectF(QPointF(0, 0), size()), QColor::fromRgbF(0.4f, 0.7f, 0.0f, 1.0f));
//     painter.setPen(Qt::transparent);
//     painter.setBrush({QGradient(QGradient::DeepBlue)});
//     painter.drawRoundedRect(QRectF(QPointF(20, 20), size() - QSize(40, 40)), 16, 16);
//     painter.setPen(Qt::black);
//     QFont font;
//     font.setPixelSize(0.05 * qMin(width(), height()));
//     painter.setFont(font);
//     painter.drawText(QRectF(QPointF(60, 60), size() - QSize(120, 120)), 0,
//                      QLatin1String(
//                          "Rendering with QRhi to a resizable QWindow.\nThe 3D API is %1.\nUse the command-line options to choose a different API.")
//                      .arg(graphicsApiName()));
//     painter.end();
//
//     if (m_rhi->isYUpInNDC())
//         image = image.mirrored();
//
//     //! [ensure-texture-2]
//     u->uploadTexture(m_texture.get(), image);
//     //! [ensure-texture-2]
// }

//! [render-init-1]
void HelloWindow::customInit() {
    m_timer.start();

    m_initialUpdates = m_rhi->nextResourceUpdateBatch();

    Assimp::Importer importer;
    const auto scene = importer.ReadFile(
        "../resources/inner_ear.fbx",
        aiProcess_Triangulate
    );

    if (!scene) {
        std::cerr << "Error importing model: " << importer.GetErrorString() << std::endl;
        exit(1);
    }

    std::unordered_map<unsigned int, QRhiTexture *> materialIndexToTexture;

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        const aiMaterial *material = scene->mMaterials[i];

        if (material->GetTextureCount(aiTextureType_DIFFUSE) <= 0) {
            std::cout << "Texture count 0 or less. Skipping..." << std::endl;
            continue;
        }

        aiString str;
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &str) != AI_SUCCESS) {
            std::cerr << "Error loading texture: " << str.C_Str() << std::endl;
            exit(1);
        }

        const aiTexture *a_texture = scene->GetEmbeddedTexture(str.C_Str());

        if (a_texture == nullptr) {
            std::cerr << "Error loading texture: " << str.C_Str() << std::endl;
            exit(1);
        }

        std::cout << "Texture path: " << str.C_Str() << std::endl;

        int width, height, channels;
        unsigned char *data = stbi_load_from_memory(
            reinterpret_cast<unsigned char *>(a_texture->pcData),
            a_texture->mWidth,
            &width,
            &height,
            &channels,
            0
        );

        if (!data) {
            std::cerr << "Error loading texture: " << str.C_Str() << ". Exiting..." << std::endl;
            exit(1);
        }


        const auto texture = m_rhi->newTexture(QRhiTexture::RGBA8, QSize(width, height));
        texture->create();

        const auto rgbaData = new uchar[width * height * 4];

        for (int x = 0; x < width * height; ++x) {
            rgbaData[4 * x + 0] = data[3 * x + 0]; // R
            rgbaData[4 * x + 1] = data[3 * x + 1]; // G
            rgbaData[4 * x + 2] = data[3 * x + 2]; // B
            rgbaData[4 * x + 3] = 255; // A (fully opaque)
        }

        QImage image(rgbaData, width, height, QImage::Format_RGBA8888);
        // image.setDevicePixelRatio(devicePixelRatio());
        // image.data_ptr() = data;
        m_initialUpdates->uploadTexture(texture, image);
        materialIndexToTexture[i] = texture;
        stbi_image_free(data);

        // todo
        // delete[] rgbaData;
    }


    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();

    static const quint32 UBUF_SIZE = 68;
    m_opaqueUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
    m_opaqueUbuf->create();
    m_greyedOutUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
    m_greyedOutUbuf->create();

    m_rayUniformBuffer.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    m_rayUniformBuffer->create();

    m_entities.reserve(10);
    for (int j = 0; j < scene->mNumMeshes; ++j) {
        const auto mesh = scene->mMeshes[j];

        assert(mesh->HasPositions());
        assert(mesh->HasNormals());
        assert(mesh->HasTextureCoords(0));

        m_entities.emplace_back(*mesh, materialIndexToTexture[mesh->mMaterialIndex], m_sampler.get(), *m_rhi,
                                m_initialUpdates, m_opaqueUbuf.get(), m_greyedOutUbuf.get());
    }

    //! [render-init-1]

    // ensureFullscreenTexture(m_sc->surfacePixelSize(), m_initialUpdates);

    m_colorPipeline.reset(m_rhi->newGraphicsPipeline());
    m_colorPipeline->setDepthTest(true);
    m_colorPipeline->setDepthWrite(true);
    // Blend factors default to One, OneOneMinusSrcAlpha, which is convenient.
    QRhiGraphicsPipeline::TargetBlend premulAlphaBlend;
    premulAlphaBlend.enable = true;
    m_colorPipeline->setTargetBlends({premulAlphaBlend});
    m_colorPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, getShader(QLatin1String(":/shaders/color.vert.qsb"))},
        {QRhiShaderStage::Fragment, getShader(QLatin1String(":/shaders/color.frag.qsb"))}
    });
    QRhiVertexInputLayout inputLayout;
    constexpr auto positionSize = 3;
    constexpr auto normalSize = 3;
    constexpr auto textureCoords = 2;
    constexpr auto stride = positionSize + normalSize + textureCoords;
    inputLayout.setBindings({
        {stride * sizeof(float)}
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},
        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
    });
    m_colorPipeline->setVertexInputLayout(inputLayout);
    m_colorPipeline->setRenderPassDescriptor(m_rp.get());
    m_colorPipeline->create();

    constexpr float rayInitialData[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, -1.0f,
    };
    m_rayVertexBuffer.reset(m_rhi->newBuffer(
            QRhiBuffer::Dynamic,
            QRhiBuffer::VertexBuffer,
            2 * 3 * sizeof(float))
    );
    m_rayVertexBuffer->create();
    m_initialUpdates->updateDynamicBuffer(m_rayVertexBuffer.get(), 0, 2 * 3 * sizeof(float), rayInitialData);

    m_rayPipeline.reset(m_rhi->newGraphicsPipeline());
    m_rayPipeline->setDepthTest(true);
    m_rayPipeline->setDepthWrite(true);
    m_rayPipeline->setTargetBlends({premulAlphaBlend});
    m_rayPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, getShader(QLatin1String(":/shaders/ray.vert.qsb"))},
        {QRhiShaderStage::Fragment, getShader(QLatin1String(":/shaders/ray.frag.qsb"))}
    });
    QRhiVertexInputLayout rayInputLayout;
    rayInputLayout.setBindings({
        {3 * sizeof(float)}
    });
    rayInputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},
    });
    m_rayPipeline->setVertexInputLayout(rayInputLayout);
    m_rayPipeline->setRenderPassDescriptor(m_rp.get());
    m_rayPipeline->setTopology(QRhiGraphicsPipeline::LineStrip);
    m_raySrb.reset(m_rhi->newShaderResourceBindings());
    static constexpr QRhiShaderResourceBinding::StageFlags visibility =
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;

    m_raySrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, visibility, m_rayUniformBuffer.get()),
    });
    m_raySrb->create();
    m_rayPipeline->setShaderResourceBindings(m_raySrb.get());
    m_rayPipeline->create();


    // m_fullscreenQuadSrb.reset(m_rhi->newShaderResourceBindings());
    // m_fullscreenQuadSrb->setBindings({
    //     QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
    //                                               m_texture.get(), m_sampler.get())
    // });
    // m_fullscreenQuadSrb->create();
    //
    // m_fullscreenQuadPipeline.reset(m_rhi->newGraphicsPipeline());
    // m_fullscreenQuadPipeline->setShaderStages({
    //     {QRhiShaderStage::Vertex, getShader(QLatin1String(":/shaders/quad.vert.qsb"))},
    //     {QRhiShaderStage::Fragment, getShader(QLatin1String(":/shaders/quad.frag.qsb"))}
    // });
    // m_fullscreenQuadPipeline->setVertexInputLayout({});
    // m_fullscreenQuadPipeline->setShaderResourceBindings(m_fullscreenQuadSrb.get());
    // m_fullscreenQuadPipeline->setRenderPassDescriptor(m_rp.get());
    // m_fullscreenQuadPipeline->create();
}

//! [render-1]
void HelloWindow::customRender() {
    auto nowElapsed = m_timer.elapsed();
    m_deltaTime = (nowElapsed - m_lastElapsedMillis) / 1000.f;
    m_lastElapsedMillis = nowElapsed;

    QRhiResourceUpdateBatch *resourceUpdates = m_rhi->nextResourceUpdateBatch();

    if (m_initialUpdates) {
        resourceUpdates->merge(m_initialUpdates);
        m_initialUpdates->release();
        m_initialUpdates = nullptr;
    }
    //! [render-1]

    //! [render-rotation]
    QMatrix4x4 modelViewProjection = m_viewProjection;

    if (pendingUpdates != nullptr) {
        resourceUpdates->updateDynamicBuffer(m_rayVertexBuffer.get(), 0, 2 * 3 * sizeof(float), pendingUpdates);
        pendingUpdates = nullptr;
    }
    resourceUpdates->updateDynamicBuffer(m_rayUniformBuffer.get(), 0, 64, m_viewProjection.constData());

    auto normalRenderingMode = RenderingMode::Normal;
    auto greyedOutRenderingMode = RenderingMode::GreyedOut;
    resourceUpdates->updateDynamicBuffer(m_opaqueUbuf.get(), 0, 64, modelViewProjection.constData());
    resourceUpdates->updateDynamicBuffer(m_opaqueUbuf.get(), 64, 4, &normalRenderingMode);

    resourceUpdates->updateDynamicBuffer(m_greyedOutUbuf.get(), 0, 64, modelViewProjection.constData());
    resourceUpdates->updateDynamicBuffer(m_greyedOutUbuf.get(), 64, 4, &greyedOutRenderingMode);

    QRhiCommandBuffer *cb = m_sc->currentFrameCommandBuffer();
    const QSize outputSizeInPixels = m_sc->currentPixelSize();

    // (re)create the texture with a size matching the output surface size, when necessary.
    // ensureFullscreenTexture(outputSizeInPixels, resourceUpdates);

    cb->beginPass(m_sc->currentFrameRenderTarget(), Qt::black, {1.0f, 0}, resourceUpdates);

    // cb->setGraphicsPipeline(m_fullscreenQuadPipeline.get());
    cb->setViewport({0, 0, float(outputSizeInPixels.width()), float(outputSizeInPixels.height())});
    // cb->setShaderResources();
    // cb->draw(6);

    for (const auto &entity: m_entities) {
        if (entity.m_renderingMode == RenderingMode::GreyedOut) {
            m_colorPipeline->setShaderResourceBindings(entity.m_greyedOutSrb.get());
        } else {
            m_colorPipeline->setShaderResourceBindings(entity.m_defaultSrb.get());
        }
        m_colorPipeline->create();
        cb->setGraphicsPipeline(m_colorPipeline.get());
        cb->setShaderResources();

        const QRhiCommandBuffer::VertexInput vbufBinding(entity.m_vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(entity.GetNumVertices());
    }

    cb->setGraphicsPipeline(m_rayPipeline.get());
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput rayVbufBinding(m_rayVertexBuffer.get(), 0);
    cb->setVertexInput(0, 1, &rayVbufBinding);
    cb->draw(2);

    cb->endPass();
    //! [render-pass-record]
}

void HelloWindow::handleMouseMove(QMouseEvent *event) {
    if (m_rotating) {
        const auto mousePos = event->pos();
        const auto offset = mousePos - m_lastMousePos;
        m_rotationAngles += QVector2D(offset) * m_deltaTime * 20;
        const QSize outputSize = m_sc->currentPixelSize();
        m_viewProjection = m_rhi->clipSpaceCorrMatrix();
        m_viewProjection.perspective(45.0f, outputSize.width() / (float) outputSize.height(), 0.01f, 1000.0f);
        m_viewProjection.lookAt(
            QVector3D(0, 0, m_zoom),
            QVector3D(0, 0, 0),
            QVector3D(0, 1, 0)
        );
        m_viewProjection.rotate(m_rotationAngles.y(), -1, 0, 0);
        m_viewProjection.rotate(m_rotationAngles.x(), 0, 1, 0);
    }

    m_lastMousePos = event->pos();
}

void HelloWindow::handleMouseButtonPress(QMouseEvent *event) {
    m_rotating = true;
}

void HelloWindow::handleMouseButtonRelease(QMouseEvent *event) {
    m_rotating = false;

    if (event->button() == Qt::LeftButton) {
        return;
    }

    const auto screenPosition = event->position();
    std::cout << "Screen pos in pixels: " << screenPosition.x() << ", " << screenPosition.y() << std::endl;
    const float ndcX = (2.0f * screenPosition.x()) / width() - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenPosition.y()) / height();
    std::cout << "NDC: " << ndcX << ", " << ndcY << std::endl;
    std::cout << std::endl;

    const QVector4D nearPoint(ndcX, ndcY, -1.0f, 1.0f);
    const QVector4D farPoint(ndcX, ndcY, 1.0f, 1.0f);
    const QMatrix4x4 inverseVP = m_viewProjection.inverted();

    QVector4D nearWorld = inverseVP * nearPoint;
    QVector4D farWorld = inverseVP * farPoint;

    nearWorld /= nearWorld.w();
    farWorld /= farWorld.w();

    const QVector3D rayOrigin(nearWorld);
    const QVector3D rayEnd(farWorld);
    const QVector3D rayDir = (rayEnd - rayOrigin).normalized();

    pendingUpdates = new float[]{
        rayOrigin.x(), rayOrigin.y(), rayOrigin.z(),
        rayEnd.x(), rayEnd.y(), rayEnd.z()
    };

    // collide with entities
    int closestEntity = -1;
    float closestDistance = std::numeric_limits<float>::max();
    for (int entityIndex = 0; entityIndex < m_entities.size(); ++entityIndex) {
        const auto &entity = m_entities[entityIndex];
        assert(entity.m_vertices.size() % 3 == 0);
        for (int i = 0; i < entity.m_vertices.size() / 3; ++i) {
            const auto v0 = QVector3D(QVector4D(entity.m_vertices[3 * i], 1.0f));
            const auto v1 = QVector3D(QVector4D(entity.m_vertices[3 * i + 1], 1.0f));
            const auto v2 = QVector3D(QVector4D(entity.m_vertices[3 * i + 2], 1.0f));

            const auto result = doesRayIntersectTriangle(rayOrigin, rayDir, v0, v1, v2);
            if (result.has_value()) {
                if (result.value() < closestDistance) {
                    closestDistance = result.value();
                    closestEntity = entityIndex;
                    std::cout << "Closest entity index: " << closestEntity << " with value: " << closestDistance <<
                            std::endl;
                }
            }
        }
    }
    if (closestEntity != -1) {
        for (int i = 0; i < m_entities.size(); ++i) {
            if (i == closestEntity) {
                m_selectedEntity = closestEntity;
                m_entities[i].m_renderingMode = RenderingMode::Normal;
            } else {
                m_entities[i].m_renderingMode = RenderingMode::GreyedOut;
            }
        }
        m_zoom = 1.0f;

        const auto newEye = m_entities[m_selectedEntity].m_centroid + QVector3D(0, 0, m_zoom);
        const QSize outputSize = m_sc->currentPixelSize();
        m_viewProjection = m_rhi->clipSpaceCorrMatrix();
        m_viewProjection.perspective(45.0f, outputSize.width() / (float) outputSize.height(), 0.01f, 1000.0f);
        m_viewProjection.lookAt(
            newEye,
            m_entities[m_selectedEntity].m_centroid,
            QVector3D(0, 1, 0)
        );
        m_viewProjection.rotate(m_rotationAngles.y(), -1, 0, 0);
        m_viewProjection.rotate(m_rotationAngles.x(), 0, 1, 0);

    } else {
        m_selectedEntity = -1;
    }
}

void HelloWindow::handleWheel(QWheelEvent *event) {
    m_zoom -= event->angleDelta().y() * 0.005f;
    const QSize outputSize = m_sc->currentPixelSize();
    m_viewProjection = m_rhi->clipSpaceCorrMatrix();
    m_viewProjection.perspective(45.0f, outputSize.width() / (float) outputSize.height(), 0.01f, 1000.0f);
    m_viewProjection.lookAt(
        QVector3D(0, 0, m_zoom),
        QVector3D(0, 0, 0),
        QVector3D(0, 1, 0)
    );
    m_viewProjection.rotate(m_rotationAngles.y(), -1, 0, 0);
    m_viewProjection.rotate(m_rotationAngles.x(), 0, 1, 0);
}

void HelloWindow::lookAt(const QVector3D eye,
                         const QVector3D center,
                         const QVector3D up) {
    m_eye = eye;
    m_center = center;
    m_up = up;
}

std::optional<float> HelloWindow::doesRayIntersectTriangle(const QVector3D rayOriginWorld,
                                                           const QVector3D rayDirWorld,
                                                           const QVector3D v0,
                                                           const QVector3D v1,
                                                           const QVector3D v2) {
    constexpr float EPSILON = 1e-6;

    const QVector3D edge1 = v1 - v0;
    const QVector3D edge2 = v2 - v0;
    const QVector3D h = QVector3D::crossProduct(rayDirWorld, edge2);
    const float a = QVector3D::dotProduct(edge1, h);

    if (fabs(a) < EPSILON) {
        return std::nullopt;
    }

    const float f = 1.0 / a;
    const QVector3D s = rayOriginWorld - v0;
    const float u = f * QVector3D::dotProduct(s, h);
    if (u < 0.0 || u > 1.0) {
        return std::nullopt;
    }

    const QVector3D q = QVector3D::crossProduct(s, edge1);
    const float v = f * QVector3D::dotProduct(rayDirWorld, q);
    if (v < 0.0 || u + v > 1.0) {
        return std::nullopt;
    }

    const float t = f * QVector3D::dotProduct(edge2, q);
    if (t > EPSILON) {
        return t;
    }

    return std::nullopt;
}
