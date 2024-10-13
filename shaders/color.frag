#version 440

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float opacity;
};

void main()
{
    vec3 light_dir = vec3(0.0, 1.0, 0.0);
    vec3 light_color = vec3(1.0, 1.0, 1.0);
    float diff = max(dot(light_dir, v_normal), 0.0);
    vec3 diffuse = light_color * diff;

    vec3 ambient = vec3(0.2, 0.2, 0.2);
    vec3 result = (ambient + diffuse) * v_color;
    fragColor = vec4(result, 1.0);
}
