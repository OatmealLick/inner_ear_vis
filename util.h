#ifndef UTIL_H
#define UTIL_H
#include <qvectornd.h>


inline float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}


inline QVector3D lerp(const QVector3D a, const QVector3D b, const float t) {
    return {
        lerp(a.x(), b.x(), t),
        lerp(a.y(), b.y(), t),
        lerp(a.z(), b.z(), t)
    };
}

// returns the distance from the ray origin if intersects
inline std::optional<float> doesRayIntersectTriangle(const QVector3D rayOriginWorld,
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

#endif //UTIL_H
