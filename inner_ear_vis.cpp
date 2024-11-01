// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "inner_ear_vis.h"

#include <iostream>
#include <QPlatformSurfaceEvent>
#include <QPainter>
#include <QFile>
#include <rhi/qshader.h>
#include <assimp/Importer.hpp>

#include "assimp/postprocess.h"
#include "assimp/scene.h"
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"
#include "optional"
#include "util.h"
#include "vendor/easing/easing.h"

RhiWindow::RhiWindow(QRhi::Implementation graphicsApi)
    : m_graphicsApi(graphicsApi), m_camera(Camera()) {
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

void RhiWindow::exposeEvent(QExposeEvent *) {
    if (isExposed() && !m_initialized) {
        init();
        resizeSwapChain();
        m_initialized = true;
    }

    const QSize surfaceSize = m_hasSwapChain ? m_sc->surfacePixelSize() : QSize();

    if ((!isExposed() || (m_hasSwapChain && surfaceSize.isEmpty())) && m_initialized && !m_notExposed)
        m_notExposed = true;

    if (isExposed() && m_initialized && m_notExposed && !surfaceSize.isEmpty()) {
        m_notExposed = false;
        m_newlyExposed = true;
    }

    if (isExposed() && !surfaceSize.isEmpty())
        render();
}

bool RhiWindow::event(QEvent *e) {
    switch (e->type()) {
        case QEvent::UpdateRequest:
            render();
            break;

        case QEvent::PlatformSurface:
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
        params.enableDebugLayer = true;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &params));
    } else if (m_graphicsApi == QRhi::D3D12) {
        QRhiD3D12InitParams params;
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

    m_sc.reset(m_rhi->newSwapChain());
    m_ds.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil,
                                      QSize(),
                                      1,
                                      QRhiRenderBuffer::UsedWithSwapChainOnly));
    m_sc->setWindow(this);
    m_sc->setDepthStencil(m_ds.get());
    m_rp.reset(m_sc->newCompatibleRenderPassDescriptor());
    m_sc->setRenderPassDescriptor(m_rp.get());

    customInit();
}

void RhiWindow::resizeSwapChain() {
    m_hasSwapChain = m_sc->createOrResize();

    const QSize outputSize = m_sc->currentPixelSize();
    QMatrix4x4 projection;
    projection.perspective(45.0f, static_cast<float>(outputSize.width()) / outputSize.height(), 0.1f, 1000.0f);
    m_projection = projection;
}

void RhiWindow::releaseSwapChain() {
    if (m_hasSwapChain) {
        m_hasSwapChain = false;
        m_sc->destroy();
    }
}

void RhiWindow::render() {
    if (!m_hasSwapChain || m_notExposed)
        return;

    if (m_sc->currentPixelSize() != m_sc->surfacePixelSize() || m_newlyExposed) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
        m_newlyExposed = false;
    }

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
    m_rhi->endFrame(m_sc.get());
    requestUpdate();
}

static QShader getShader(const QString &name) {
    QFile f(name);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());

    return QShader();
}


AppWindow::AppWindow(QRhi::Implementation graphicsApi)
    : RhiWindow(graphicsApi) {
}

void AppWindow::customInit() {
    m_timer.start();

    const QSize outputSize = m_sc->currentPixelSize();
    auto projection = QMatrix4x4();
    projection.perspective(45.0f, outputSize.width() / (float) outputSize.height(), 0.01f, 1000.0f);
    m_projection = projection;

    auto modelRotation = QMatrix4x4();
    modelRotation.rotate(m_rotationAngles.y(), -1, 0, 0);
    modelRotation.rotate(m_rotationAngles.x(), 0, 1, 0);
    m_modelRotation = modelRotation;

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
        m_initialUpdates->uploadTexture(texture, image);
        materialIndexToTexture[i] = texture;
        stbi_image_free(data);
    }

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();

    // uniform buffers
    constexpr quint32 UBUF_SIZE = 64 + 64 + 4;
    m_normalUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
    m_normalUbuf->create();
    m_greyedOutUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
    m_greyedOutUbuf->create();

    m_rayUniformBuffer.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    m_rayUniformBuffer->create();

    // entity initialization
    m_entities.reserve(8);
    for (int j = 0; j < scene->mNumMeshes; ++j) {
        const auto mesh = scene->mMeshes[j];

        assert(mesh->HasPositions());
        assert(mesh->HasNormals());
        assert(mesh->HasTextureCoords(0));

        m_entities.emplace_back(*mesh, materialIndexToTexture[mesh->mMaterialIndex], m_sampler.get(), *m_rhi,
                                m_initialUpdates, m_normalUbuf.get(), m_greyedOutUbuf.get());
    }

    // entity rendering setup
    m_colorPipeline.reset(m_rhi->newGraphicsPipeline());
    m_colorPipeline->setDepthTest(true);
    m_colorPipeline->setDepthWrite(true);
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

    // ray rendering setup
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
}

void AppWindow::customRender() {
    const auto nowElapsed = m_timer.elapsed();
    m_deltaTime = (nowElapsed - m_lastElapsedMillis) / 1000.f;
    m_lastElapsedMillis = nowElapsed;

    if (m_selectionTween.playing) {
        m_selectionTween.timerSeconds += m_deltaTime;
        auto ratio = m_selectionTween.timerSeconds / m_selectionTween.durationSeconds;

        if (ratio >= 1.0) {
            ratio = 1.0;
            m_selectionTween.playing = false;
        }

        const auto tweenedRatio = getEasingFunction(m_selectionTween.easingFunction)(ratio);
        const auto tweenedEye = lerp(m_selectionTween.startValueEye, m_selectionTween.endValueEye, tweenedRatio);
        const auto tweenedCenter = lerp(m_selectionTween.startValueCenter, m_selectionTween.endValueCenter,
                                        tweenedRatio);
        m_camera.setLookAt(tweenedEye, tweenedCenter, QVector3D(0, 1, 0));
    }

    QRhiResourceUpdateBatch *resourceUpdates = m_rhi->nextResourceUpdateBatch();

    if (m_initialUpdates) {
        resourceUpdates->merge(m_initialUpdates);
        m_initialUpdates->release();
        m_initialUpdates = nullptr;
    }

    const auto viewProjection = m_rhi->clipSpaceCorrMatrix() * m_projection * m_camera.view();
    m_viewProjection = viewProjection * m_modelRotation;

    if (pendingUpdates != nullptr) {
        resourceUpdates->updateDynamicBuffer(m_rayVertexBuffer.get(), 0, 2 * 3 * sizeof(float), pendingUpdates);
        delete[] pendingUpdates;
        pendingUpdates = nullptr;
    }
    resourceUpdates->updateDynamicBuffer(m_rayUniformBuffer.get(), 0, 64, m_viewProjection.constData());

    constexpr auto normalRenderingMode = RenderingMode::Normal;
    constexpr auto greyedOutRenderingMode = RenderingMode::GreyedOut;
    resourceUpdates->updateDynamicBuffer(m_normalUbuf.get(), 0, 64, m_modelRotation.constData());
    resourceUpdates->updateDynamicBuffer(m_normalUbuf.get(), 64, 64, viewProjection.constData());
    resourceUpdates->updateDynamicBuffer(m_normalUbuf.get(), 128, 4, &normalRenderingMode);

    resourceUpdates->updateDynamicBuffer(m_greyedOutUbuf.get(), 0, 64, m_modelRotation.constData());
    resourceUpdates->updateDynamicBuffer(m_greyedOutUbuf.get(), 64, 64, viewProjection.constData());
    resourceUpdates->updateDynamicBuffer(m_greyedOutUbuf.get(), 128, 4, &greyedOutRenderingMode);

    QRhiCommandBuffer *cb = m_sc->currentFrameCommandBuffer();
    const QSize outputSizeInPixels = m_sc->currentPixelSize();

    cb->beginPass(m_sc->currentFrameRenderTarget(), Qt::black, {1.0f, 0}, resourceUpdates);
    cb->setViewport({0, 0, float(outputSizeInPixels.width()), float(outputSizeInPixels.height())});

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

    if (m_drawRays) {
        cb->setGraphicsPipeline(m_rayPipeline.get());
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput rayVbufBinding(m_rayVertexBuffer.get(), 0);
        cb->setVertexInput(0, 1, &rayVbufBinding);
        cb->draw(2);
    }

    cb->endPass();
}

void AppWindow::handleMouseMove(QMouseEvent *event) {
    if (m_pressing_down) {
        m_rotating = true;

        const auto mousePos = event->pos();
        const auto offset = mousePos - m_lastMousePos;
        m_rotationAngles += QVector2D(offset) * m_deltaTime * 20;

        QMatrix4x4 modelRotation;
        modelRotation.rotate(m_rotationAngles.y(), -1, 0, 0);
        modelRotation.rotate(m_rotationAngles.x(), 0, 1, 0);
        m_modelRotation = modelRotation;
    }

    m_lastMousePos = event->pos();
}

void AppWindow::handleMouseButtonPress(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_pressing_down = true;
    }
}

void AppWindow::handleMouseButtonRelease(QMouseEvent *event) {
    m_pressing_down = false;

    if (event->button() == Qt::LeftButton) {
        if (m_rotating) {
            m_rotating = false;
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
                const auto v0 = entity.m_vertices[3 * i];
                const auto v1 = entity.m_vertices[3 * i + 1];
                const auto v2 = entity.m_vertices[3 * i + 2];

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

            constexpr QVector3D cameraDirView(0, 0, -1.0);
            const auto centroidWorld = m_modelRotation.map(m_entities[m_selectedEntity].m_centroid);
            const auto newEye = centroidWorld - cameraDirView;

            m_selectionTween = SelectionTween{
                m_camera.eye(),
                newEye,
                m_camera.center(),
                centroidWorld,
                0.2f,
                0.0f,
                true,
                EaseOutCubic
            };
        } else {
            m_selectedEntity = -1;
        }
    } else if (event->button() == Qt::RightButton) {
        if (m_selectedEntity != -1) {
            for (auto &entity: m_entities) {
                entity.m_renderingMode = RenderingMode::Normal;
            }
            m_selectionTween = SelectionTween{
                m_camera.eye(),
                QVector3D(0, 0, 2.5),
                m_camera.center(),
                QVector3D(0, 0, 0),
                0.2f,
                0.0f,
                true,
                EaseOutCubic
            };
            m_selectedEntity = -1;
        }
    }
}

void AppWindow::handleWheel(QWheelEvent *event) {
    if (m_selectedEntity == -1) {
        m_camera.zoom(event->angleDelta().y());
    }
}
