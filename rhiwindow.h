// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef WINDOW_H
#define WINDOW_H

#include <QWindow>
#include <QOffscreenSurface>
#include <rhi/qrhi.h>

#include "Camera.h"
#include "Entity.h"
#include "assimp/texture.h"
#include "vendor/easing/easing.h"

struct SelectionTween {
    QVector3D startValueEye;
    QVector3D endValueEye;
    QVector3D startValueCenter;
    QVector3D endValueCenter;
    float durationSeconds = 0.0f;
    float timerSeconds = 0.0f;
    bool playing = false;

    easing_functions easingFunction = EaseInCubic;
};

class RhiWindow : public QWindow
{
public:
    RhiWindow(QRhi::Implementation graphicsApi);
    QString graphicsApiName() const;
    void releaseSwapChain();

protected:
    virtual void customInit() = 0;
    virtual void customRender() = 0;

    // destruction order matters to a certain degree: the fallbackSurface must
    // outlive the rhi, the rhi must outlive all other resources.  The resources
    // need no special order when destroying.
#if QT_CONFIG(opengl)
    std::unique_ptr<QOffscreenSurface> m_fallbackSurface;
#endif
    std::unique_ptr<QRhi> m_rhi;
//! [swapchain-data]
    std::unique_ptr<QRhiSwapChain> m_sc;
    std::unique_ptr<QRhiRenderBuffer> m_ds;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;
//! [swapchain-data]
    bool m_hasSwapChain = false;
    QMatrix4x4 m_viewProjection;

    virtual void handleMouseMove(QMouseEvent *event) = 0;
    virtual void handleMouseButtonPress(QMouseEvent *event) = 0;
    virtual void handleMouseButtonRelease(QMouseEvent *event) = 0;
    virtual void handleWheel(QWheelEvent *event) = 0;

    QPoint m_lastMousePos;
    bool m_rotating = false;
    bool m_pressing_down = false;
    QVector2D m_rotationAngles = QVector2D(0, 0);

    QElapsedTimer m_timer;
    qint64 m_lastElapsedMillis;
    float m_deltaTime = 0;

    QMatrix4x4 m_projection;
    QMatrix4x4 m_modelRotation;

    Camera m_camera;

private:
    void init();
    void resizeSwapChain();
    void render();

    void exposeEvent(QExposeEvent *) override;
    bool event(QEvent *) override;

    QRhi::Implementation m_graphicsApi;
    bool m_initialized = false;
    bool m_notExposed = false;
    bool m_newlyExposed = false;
};

class HelloWindow : public RhiWindow
{
public:
    HelloWindow(QRhi::Implementation graphicsApi);

    void customInit() override;
    void customRender() override;

    void handleMouseMove(QMouseEvent *event) override;
    void handleMouseButtonPress(QMouseEvent *event) override;
    void handleMouseButtonRelease(QMouseEvent *event) override;
    void handleWheel(QWheelEvent *event) override;
private:
    // void ensureFullscreenTexture(const QSize &pixelSize, QRhiResourceUpdateBatch *u);

    std::unique_ptr<QRhiBuffer> m_normalUbuf;
    std::unique_ptr<QRhiBuffer> m_greyedOutUbuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiGraphicsPipeline> m_colorPipeline;

    std::unique_ptr<QRhiGraphicsPipeline> m_fullscreenQuadPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_fullscreenQuadSrb;

    std::unique_ptr<QRhiGraphicsPipeline> m_rayPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_raySrb;
    std::unique_ptr<QRhiBuffer> m_rayVertexBuffer;
    std::unique_ptr<QRhiBuffer> m_rayUniformBuffer;
    float* pendingUpdates = nullptr;

    std::vector<Entity> m_entities;
    int m_selectedEntity = -1;

    QRhiResourceUpdateBatch *m_initialUpdates = nullptr;

    float m_rotation = 0;
    float m_opacity = 1.0f;
    int m_opacityDir = -1;

    SelectionTween m_selectionTween;
};

#endif
