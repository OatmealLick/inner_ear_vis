#ifndef ENTITY_H
#define ENTITY_H
#include <memory>
#include <rhi/qrhi.h>

#include "assimp/mesh.h"
#include <cstdint>

enum class RenderingMode : int {
    Normal = 0,
    GreyedOut = 1
};

class Entity {
public:
    Entity(const aiMesh &mesh, QRhiTexture* texture, QRhiSampler* sampler, QRhi& rhi, QRhiResourceUpdateBatch *initialUpdates, QRhiBuffer* ubuf, QRhiBuffer* greyedOutUbuf);

    unsigned int GetNumVertices() const;

    std::unique_ptr<QRhiShaderResourceBindings> m_defaultSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_greyedOutSrb;
    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::vector<QVector3D> m_vertices;
    QVector3D m_centroid;
    float m_opacity = 1.0f;
    RenderingMode m_renderingMode = RenderingMode::Normal;
private:
    unsigned int m_numVertices;
};


#endif //ENTITY_H
