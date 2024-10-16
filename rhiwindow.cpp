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
    m_viewProjection.translate(0, 0, 0);
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

//! [request-update]

static float vertexData[] = {
    // Y up (note clipSpaceCorrMatrix in m_viewProjection), CCW
    0.0f, 0.5f, 1.0f, 0.0f, 0.0f,
    -0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
    0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
};

static float vertexData2[] = {
    // Y up (note clipSpaceCorrMatrix in m_viewProjection), CCW
    0.0f, 0.5f, 0.0f, //1.0f, 1.0f, 0.0f,
    -0.5f, -0.5f, 0.0f, //0.0f, 1.0f, 0.0f,
    0.5f, -0.5f, 0.0f, //0.0f, 0.0f, 1.0f,

    0.5f, 0.5f, 0.0f,
    -0.5f, -0.5f, 0.0f, //0.0f, 1.0f, 0.0f,
    0.5f, -0.5f, 0.0f, //0.0f, 0.0f, 1.0f,
};

static unsigned int totalNumVertices = 0;

// static float *vertexDataBunny = new float[14904 * 3];

//! [getshader]
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
void HelloWindow::ensureFullscreenTexture(const QSize &pixelSize, QRhiResourceUpdateBatch *u) {
    if (m_texture && m_texture->pixelSize() == pixelSize)
        return;

    if (!m_texture)
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, pixelSize));
    else
        m_texture->setPixelSize(pixelSize);

    m_texture->create();

    QImage image(pixelSize, QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio());
    //! [ensure-texture]
    QPainter painter(&image);
    painter.fillRect(QRectF(QPointF(0, 0), size()), QColor::fromRgbF(0.4f, 0.7f, 0.0f, 1.0f));
    painter.setPen(Qt::transparent);
    painter.setBrush({QGradient(QGradient::DeepBlue)});
    painter.drawRoundedRect(QRectF(QPointF(20, 20), size() - QSize(40, 40)), 16, 16);
    painter.setPen(Qt::black);
    QFont font;
    font.setPixelSize(0.05 * qMin(width(), height()));
    painter.setFont(font);
    painter.drawText(QRectF(QPointF(60, 60), size() - QSize(120, 120)), 0,
                     QLatin1String(
                         "Rendering with QRhi to a resizable QWindow.\nThe 3D API is %1.\nUse the command-line options to choose a different API.")
                     .arg(graphicsApiName()));
    painter.end();

    if (m_rhi->isYUpInNDC())
        image = image.mirrored();

    //! [ensure-texture-2]
    u->uploadTexture(m_texture.get(), image);
    //! [ensure-texture-2]
}

unsigned int HelloWindow::loadTexture(const aiTexture *texture) {
    return 0;
}

//! [render-init-1]
void HelloWindow::customInit() {
    m_timer.start();

    m_initialUpdates = m_rhi->nextResourceUpdateBatch();

    // QFile modelFile(":/bunny.obj");
    // if (!modelFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    //     std::cerr << "Failed to open model file!" << std::endl;
    //     exit(1);
    // }
    //
    // QByteArray objData = modelFile.readAll();
    // modelFile.close();

    Assimp::Importer importer;
    const auto scene = importer.ReadFile(
        "../resources/inner_ear.fbx",
        aiProcess_Triangulate
    );

    if (!scene) {
        std::cerr << "Error importing model: " << importer.GetErrorString() << std::endl;
        exit(1);
    }

    constexpr auto positionSize = 3;
    constexpr auto normalSize = 3;
    constexpr auto textureCoords = 2;
    constexpr auto stride = positionSize + normalSize + textureCoords;

    totalNumVertices = 0;
    for (int i = 0; i < scene->mNumMeshes; ++i) {
        const auto mesh = scene->mMeshes[i];
        totalNumVertices += mesh->mNumVertices;
    }

    auto *vertexData = new float[totalNumVertices * stride];
    unsigned int offset = 0;
    for (int j = 0; j < scene->mNumMeshes; ++j) {
        const auto mesh = scene->mMeshes[j];

        assert(mesh->HasTextureCoords(0));

        for (int i = 0; i < mesh->mNumVertices; ++i) {
            const auto v = mesh->mVertices[i];
            const auto n = mesh->mNormals[i];
            const auto t = mesh->mTextureCoords[0][i];

            vertexData[offset + stride * i] = v.x / 1000.0f;
            vertexData[offset + stride * i + 1] = v.y / 1000.0f;
            vertexData[offset + stride * i + 2] = v.z / 1000.0f;

            vertexData[offset + stride * i + 3] = n.x;
            vertexData[offset + stride * i + 4] = n.y;
            vertexData[offset + stride * i + 5] = n.z;

            vertexData[offset + stride * i + 6] = t.x;
            vertexData[offset + stride * i + 7] = t.y;
        }

        offset += mesh->mNumVertices * stride;
    }


    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                  totalNumVertices * stride * sizeof(float)));
    m_vbuf->create();
    m_initialUpdates->uploadStaticBuffer(m_vbuf.get(), vertexData);

    std::vector<QRhiTexture *> textures;
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
            rgbaData[4 * x + 0] = data[3 * x + 0];  // R
            rgbaData[4 * x + 1] = data[3 * x + 1];  // G
            rgbaData[4 * x + 2] = data[3 * x + 2];  // B
            rgbaData[4 * x + 3] = 255;              // A (fully opaque)
        }

        QImage image(rgbaData, width, height, QImage::Format_RGBA8888);
        // image.setDevicePixelRatio(devicePixelRatio());
        // image.data_ptr() = data;
        m_initialUpdates->uploadTexture(texture, image);

        textures.push_back(texture);
        // todo
        // stbi_image_free(data);
    }

    static const quint32 UBUF_SIZE = 68;
    m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
    m_ubuf->create();
    //! [render-init-1]

    ensureFullscreenTexture(m_sc->surfacePixelSize(), m_initialUpdates);

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();

    //! [render-init-2]
    m_colorTriSrb.reset(m_rhi->newShaderResourceBindings());
    static constexpr QRhiShaderResourceBinding::StageFlags visibility =
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;

    // todo if rendered together, could be on the same texture
    m_colorTriSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, visibility, m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  textures.at(5), m_sampler.get())
    });
    m_colorTriSrb->create();

    m_colorPipeline.reset(m_rhi->newGraphicsPipeline());
    // Enable depth testing; not quite needed for a simple triangle, but we
    // have a depth-stencil buffer so why not.
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
    inputLayout.setBindings({
        {stride * sizeof(float)}
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},
        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
    });
    m_colorPipeline->setVertexInputLayout(inputLayout);
    m_colorPipeline->setShaderResourceBindings(m_colorTriSrb.get());
    m_colorPipeline->setRenderPassDescriptor(m_rp.get());
    m_colorPipeline->create();
    //! [render-init-2]

    m_fullscreenQuadSrb.reset(m_rhi->newShaderResourceBindings());
    m_fullscreenQuadSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                                                  m_texture.get(), m_sampler.get())
    });
    m_fullscreenQuadSrb->create();

    m_fullscreenQuadPipeline.reset(m_rhi->newGraphicsPipeline());
    m_fullscreenQuadPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, getShader(QLatin1String(":/shaders/quad.vert.qsb"))},
        {QRhiShaderStage::Fragment, getShader(QLatin1String(":/shaders/quad.frag.qsb"))}
    });
    m_fullscreenQuadPipeline->setVertexInputLayout({});
    m_fullscreenQuadPipeline->setShaderResourceBindings(m_fullscreenQuadSrb.get());
    m_fullscreenQuadPipeline->setRenderPassDescriptor(m_rp.get());
    m_fullscreenQuadPipeline->create();
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

    // modelViewProjection.scale(100);
    constexpr auto yOffset = -0.1f;
    modelViewProjection.translate(0, yOffset, m_zoom);
    modelViewProjection.rotate(m_rotationAngles.y(), 1, 0, 0);
    modelViewProjection.rotate(m_rotationAngles.x(), 0, 1, 0);
    // modelViewProjection.lookAt(
    //     QVector3D(0, 0, -4 + m_rotation),
    //     QVector3D(0, 0, 0),
    //     QVector3D(0, 1, 0)
    // );

    // modelViewProjection.rotate(m_rotation, 0, 1, 0);
    resourceUpdates->updateDynamicBuffer(m_ubuf.get(), 0, 64, modelViewProjection.constData());
    //! [render-rotation]

    //! [render-opacity]
    // m_opacity += m_opacityDir * 0.005f;
    // if (m_opacity < 0.0f || m_opacity > 1.0f) {
    //     m_opacityDir *= -1;
    //     m_opacity = qBound(0.0f, m_opacity, 1.0f);
    // }
    resourceUpdates->updateDynamicBuffer(m_ubuf.get(), 64, 4, &m_opacity);
    //! [render-opacity]

    //! [render-cb]
    QRhiCommandBuffer *cb = m_sc->currentFrameCommandBuffer();
    const QSize outputSizeInPixels = m_sc->currentPixelSize();
    //! [render-cb]

    // (re)create the texture with a size matching the output surface size, when necessary.
    ensureFullscreenTexture(outputSizeInPixels, resourceUpdates);

    //! [render-pass]
    cb->beginPass(m_sc->currentFrameRenderTarget(), Qt::black, {1.0f, 0}, resourceUpdates);
    //! [render-pass]

    // cb->setGraphicsPipeline(m_fullscreenQuadPipeline.get());
    cb->setViewport({0, 0, float(outputSizeInPixels.width()), float(outputSizeInPixels.height())});
    // cb->setShaderResources();
    // // todo
    const auto numVertices = totalNumVertices;
    // cb->draw(numVertices);
    // cb->draw(6);

    //! [render-pass-record]
    cb->setGraphicsPipeline(m_colorPipeline.get());
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(numVertices);
    // cb->draw(6);

    cb->endPass();
    //! [render-pass-record]
}

void HelloWindow::handleMouseMove(QMouseEvent *event) {
    if (m_rotating) {
        auto mousePos = event->pos();
        auto offset = mousePos - m_lastMousePos;
        m_rotationAngles += QVector2D(offset) * m_deltaTime * 20;
    }

    m_lastMousePos = event->pos();
}

void HelloWindow::handleMouseButtonPress(QMouseEvent *event) {
    m_rotating = true;
}

void HelloWindow::handleMouseButtonRelease(QMouseEvent *event) {
    m_rotating = false;
}

void HelloWindow::handleWheel(QWheelEvent *event) {
    m_zoom += event->angleDelta().y() * 0.005f;
}
