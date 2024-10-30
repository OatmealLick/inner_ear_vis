#ifndef CAMERA_H
#define CAMERA_H
#include <qvectornd.h>


class Camera {
public:
    Camera(): m_zoom(2.5), m_eye(QVector3D(0, 0, 2.5)), m_center(QVector3D(0, 0, 0)), m_up(QVector3D(0, 1, 0)) {}
    QMatrix4x4 view() const;
    void setLookAt(QVector3D eye, QVector3D center, QVector3D up);
    void zoom(float scaler);

    QVector3D eye() const {
        return m_eye;
    }

    QVector3D center() const {
        return m_center;
    }

    QVector3D up() const {
        return m_up;
    }

private:
    float m_zoom;
    QVector3D m_eye;
    QVector3D m_center;
    QVector3D m_up;
};



#endif //CAMERA_H
