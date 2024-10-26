#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 tex_coords;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_tex_coords;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float opacity;
};

void main()
{
    v_color = vec3(tex_coords.x, tex_coords.y, 0.0);
    v_normal = normal;
    v_tex_coords = tex_coords;
    gl_Position = mvp * vec4(position, 1.0);
}
