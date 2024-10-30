//
// Created by lick on 10/30/2024.
//

#include "Camera.h"

#include <qmatrix4x4.h>

QMatrix4x4 Camera::view() const {
    QMatrix4x4 viewMatrix;
    viewMatrix.lookAt(
        m_eye, m_center, m_up
    );
    return viewMatrix;
}

void Camera::setLookAt(const QVector3D eye, const QVector3D center, const QVector3D up) {
    m_eye = eye;
    m_center = center;
    m_up = up;
}

void Camera::zoom(const float scaler) {
    const auto dir = (m_center - m_eye).normalized();
    const auto newEye = m_eye + (dir * scaler * 0.003f);
    setLookAt(newEye, m_center, m_up);
}
