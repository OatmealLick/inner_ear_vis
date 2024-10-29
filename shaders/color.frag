#version 440

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_tex_coords;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 model_rotation;
    mat4 view_projection;
    int rendering_mode;
};

layout(binding = 1) uniform sampler2D diffuse_texture;

void main()
{
    vec3 light_dir = vec3(0.0, 1.0, 0.0);
    vec3 light_color = vec3(1.0, 1.0, 1.0);
    float diff = max(dot(light_dir, v_normal), 0.0);
    vec3 diffuse = light_color * diff;
    vec3 ambient = vec3(0.4, 0.4, 0.4);

    if (rendering_mode == 0) {
        // one mesh doesn't have UV coordinates / texture, a small hack :)
        vec3 diff_color = vec3(0.9, 0.8, 0.9);
        if (v_tex_coords.x > 0.001) {
            diff_color = texture(diffuse_texture, v_tex_coords).xyz;
        }

        vec3 result = (ambient + diffuse) * diff_color;
        fragColor = vec4(result, 1.0);
    } else {
        vec3 diff_color = vec3(0.4, 0.4, 0.4);
        vec3 result = (ambient + diffuse) * diff_color;
        fragColor = vec4(result, 1.0);
    }
}
