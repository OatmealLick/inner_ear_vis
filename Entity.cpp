//
// Created by lick on 10/16/2024.
//

#include "Entity.h"

#include <iostream>

#include "assimp/material.h"

Entity::Entity(const aiMesh &mesh, QRhiTexture* texture, QRhiSampler* sampler, QRhi& m_rhi, QRhiResourceUpdateBatch *m_initialUpdates, QRhiBuffer* ubuf) {

    constexpr auto positionSize = 3;
    constexpr auto normalSize = 3;
    constexpr auto textureCoords = 2;
    constexpr auto stride = positionSize + normalSize + textureCoords;

    m_numVertices = mesh.mNumVertices;

    auto *vertexData = new float[mesh.mNumVertices * stride];

    for (int i = 0; i < mesh.mNumVertices; ++i) {
        const auto v = mesh.mVertices[i];
        const auto n = mesh.mNormals[i];
        const auto t = mesh.mTextureCoords[0][i];

        std::cout << "T: " << t.x << ", " << t.y << std::endl;

        vertexData[stride * i] = v.x / 1000.0f;
        vertexData[stride * i + 1] = v.y / 1000.0f;
        vertexData[stride * i + 2] = v.z / 1000.0f;

        vertexData[stride * i + 3] = n.x;
        vertexData[stride * i + 4] = n.y;
        vertexData[stride * i + 5] = n.z;

        vertexData[stride * i + 6] = t.x; // why all zeroes
        vertexData[stride * i + 7] = t.y;
    }

    m_vbuf.reset(m_rhi.newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                  mesh.mNumVertices * stride * sizeof(float)));
    m_vbuf->create();
    m_initialUpdates->uploadStaticBuffer(m_vbuf.get(), vertexData);

    m_colorTriSrb.reset(m_rhi.newShaderResourceBindings());
    static constexpr QRhiShaderResourceBinding::StageFlags visibility =
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;

    m_colorTriSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, visibility, ubuf),
        // QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
        //                                           texture, sampler)
    });
    m_colorTriSrb->create();
}

unsigned int Entity::GetNumVertices() const {
    return m_numVertices;
}
