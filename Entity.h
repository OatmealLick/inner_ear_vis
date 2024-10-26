#ifndef ENTITY_H
#define ENTITY_H
#include <memory>
#include <rhi/qrhi.h>

#include "assimp/mesh.h"


class Entity {
public:
    Entity(const aiMesh &mesh, QRhiTexture* texture, QRhiSampler* sampler, QRhi& m_rhi, QRhiResourceUpdateBatch *m_initialUpdates, QRhiBuffer* ubuf);

    unsigned int GetNumVertices() const;

    std::unique_ptr<QRhiShaderResourceBindings> m_colorTriSrb;
    std::unique_ptr<QRhiBuffer> m_vbuf;
private:
    unsigned int m_numVertices;
};


#endif //ENTITY_H
