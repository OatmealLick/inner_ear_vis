//
// Created by lick on 10/16/2024.
//

#include "Entity.h"

#include <iostream>

#include "assimp/material.h"

Entity::Entity(const aiMesh &mesh, QRhiTexture* texture, QRhiSampler* sampler, QRhi& rhi, QRhiResourceUpdateBatch *initialUpdates, QRhiBuffer* ubuf, QRhiBuffer* greyedOutUbuf) {

    constexpr auto positionSize = 3;
    constexpr auto normalSize = 3;
    constexpr auto textureCoords = 2;
    constexpr auto stride = positionSize + normalSize + textureCoords;

    m_numVertices = mesh.mNumVertices;

    auto *vertexData = new float[mesh.mNumVertices * stride];

    m_vertices.reserve(m_numVertices);

    for (int i = 0; i < mesh.mNumVertices; ++i) {
        const auto v = mesh.mVertices[i];
        const auto n = mesh.mNormals[i];
        const auto t = mesh.mTextureCoords[0][i];

        // poor man scaling :)
        float x = v.x / 1000.0f;
        float y = v.y / 1000.0f;
        float z = v.z / 1000.0f;

        vertexData[stride * i] = x;
        vertexData[stride * i + 1] = y;
        vertexData[stride * i + 2] = z;

        vertexData[stride * i + 3] = n.x;
        vertexData[stride * i + 4] = n.y;
        vertexData[stride * i + 5] = n.z;

        vertexData[stride * i + 6] = t.x;
        vertexData[stride * i + 7] = 1.0f - t.y;  // flipping the y coordinate for pipeline to handle properly

        // also copy vertex positions for later use, eg. raycasting
        m_vertices.emplace_back(x, y, z);

        // computing centroid for zooming on selection
        m_centroid += QVector3D(x, y, z);
    }

    m_vbuf.reset(rhi.newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                  mesh.mNumVertices * stride * sizeof(float)));
    m_vbuf->create();
    initialUpdates->uploadStaticBuffer(m_vbuf.get(), vertexData);

    static constexpr QRhiShaderResourceBinding::StageFlags visibility =
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;

    m_defaultSrb.reset(rhi.newShaderResourceBindings());
    m_defaultSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, visibility, ubuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  texture, sampler)
    });
    m_defaultSrb->create();
    m_greyedOutSrb.reset(rhi.newShaderResourceBindings());
    m_greyedOutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, visibility, greyedOutUbuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  texture, sampler)
    });
    m_greyedOutSrb->create();

    m_centroid /= static_cast<float>(mesh.mNumVertices);
}

unsigned int Entity::GetNumVertices() const {
    return m_numVertices;
}
