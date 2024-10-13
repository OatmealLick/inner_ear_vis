#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_normal;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float opacity;
};

void main()
{
    //v_color = normal;
    v_color = vec3(1.0, 1.0, 0.0);
    v_normal = normal;
    gl_Position = mvp * vec4(position, 1.0);
}
