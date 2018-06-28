#include "main.h"
#include "../opengl/functions.h"

namespace three {
namespace renderer {
    namespace multipass {
        Main::Main()
        {
            const GLchar vertex_shader[] = R"(
#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 face_normal_vector;
layout(location = 2) in vec3 vertex_normal_vector;
layout(location = 3) in vec4 vertex_color;
layout(location = 0) uniform mat4 model_mat;
layout(location = 1) uniform mat4 view_mat;
layout(location = 2) uniform mat4 projection_mat;
layout(location = 3) uniform float smoothness;
out vec4 frag_object_color;
out vec3 frag_smooth_normal_vector;
out vec3 frag_vertex_normal_vector;
out vec3 frag_light_direction;
out vec4 frag_position;
void main(void)
{
    vec4 model_position = model_mat * vec4(position, 1.0f);
    gl_Position = projection_mat * view_mat * model_position;
    vec3 light_position = vec3(0.0f, 1.0f, 1.0f);
    frag_light_direction = light_position - model_position.xyz;
    frag_object_color = vertex_color;
    frag_smooth_normal_vector = smoothness * vertex_normal_vector 
        + (1.0 - smoothness) * face_normal_vector;
    frag_vertex_normal_vector = vertex_normal_vector;
    frag_position = view_mat * model_position;
}
)";

            const GLchar fragment_shader[] = R"(
#version 450
uniform sampler2D depth_map;
in vec4 frag_object_color;
in vec3 frag_light_direction;
in vec3 frag_smooth_normal_vector;
in vec3 frag_vertex_normal_vector;
in vec4 frag_position;
out vec4 frag_color;

const float pi = 3.14159;  
const float distance_threshold = 0.0001; // 距離が離れすぎている場合は無視

void main(){
    vec2 texcoord = gl_FragCoord.xy / 640;
    vec3 center = vec3(texcoord, texture(depth_map, texcoord).x);
    float radius = 0.0001;
    float ao = 1.0;
    float depth = 0.0;
    for(int n = 0;n < 6;n++){
        for(int i = 0;i < 6;i++){
            float ratio = float(i) / 6.0;
            vec2 s1 = center.xy + vec2(radius * cos(pi * ratio), radius * sin(pi * ratio));
            vec3 p1 = vec3(s1, texture(depth_map, s1).x);
            if(p1.z >= center.z){
                continue;
            }
            if(p1.z + distance_threshold < center.z){
                continue;
            }
            vec2 s2 = center.xy + vec2(3.0 * radius * cos(pi * ratio + pi), 3.0 * radius * sin(pi * ratio + pi));
            vec3 p2 = vec3(s2, texture(depth_map, s2).x);
            if(p2.z >= center.z){
                continue;
            }
            if(p2.z + distance_threshold < center.z){
                continue;
            }
            float rad1 = atan(radius / (center.z - p1.z));
            float rad2 = atan(radius / (center.z - p2.z));
            ao -= clamp((rad1 + rad2) / pi, 0.0, 1.0) / 72.0;

            depth += center.z;
        }
        for(int i = 0;i < 6;i++){
            float ratio = float(i) / 6.0;
            vec2 s1 = center.xy + vec2(3.0 * radius * cos(pi * ratio),32.0 * radius * sin(pi * ratio));
            vec3 p1 = vec3(s1, texture(depth_map, s1).x);
            if(p1.z >= center.z){
                continue;
            }
            if(p1.z + distance_threshold < center.z){
                continue;
            }
            vec2 s2 = center.xy + vec2(radius * cos(pi * ratio + pi), radius * sin(pi * ratio + pi));
            vec3 p2 = vec3(s2, texture(depth_map, s2).x);
            if(p2.z >= center.z){
                continue;
            }
            if(p2.z + distance_threshold < center.z){
                continue;
            }
            float rad1 = atan(radius / (center.z - p1.z));
            float rad2 = atan(radius / (center.z - p2.z));
            ao -= clamp((rad1 + rad2) / pi, 0.0, 1.0) / 72.0;

            depth += center.z;
        }
        radius += 0.0001;
    }
    ao = clamp(ao, 0.0, 1.0);
    depth = clamp(depth / 6.0, 0.0, 1.0);

    vec3 unit_smooth_normal_vector = normalize(frag_smooth_normal_vector);
    vec3 unit_light_direction = normalize(frag_light_direction);
    vec3 unit_eye_direction = normalize(-frag_position.xyz);
    vec3 unit_reflection = normalize(2.0 * (dot(unit_light_direction, unit_smooth_normal_vector)) * unit_smooth_normal_vector - unit_light_direction);
    float is_frontface = step(0.0, dot(unit_reflection, unit_smooth_normal_vector));
    float light_distance = length(frag_light_direction);
    float attenuation = clamp(1.0 / (1.0 + 0.1 * light_distance + 0.2 * light_distance * light_distance), 0.0f, 1.0f);
    vec3 eye_direction = -frag_position.xyz;
    float diffuse = dot(unit_smooth_normal_vector, unit_light_direction);
    float specular = clamp(dot(unit_reflection, unit_eye_direction), 0.0f, 1.0f) * is_frontface;
    specular = pow(specular, 2.0);
    // frag_color = vec4((attenuation) * object_color.xyz, 1.0);
    vec3 attenuation_color = attenuation * frag_object_color.rgb;
    vec3 diffuse_color = diffuse * frag_object_color.rgb;
    vec3 specular_color = vec3(specular);
    vec3 ambient_color = frag_object_color.rgb;
    frag_color = vec4(clamp(diffuse_color + ambient_color * 0.5, 0.0, 1.0), 1.0);
    frag_color = vec4(vec3(attenuation), 1.0);
    vec3 composite_color = ambient_color * 0.1 + diffuse_color
        + specular_color * 0.5;

    vec3 top = 0.5 * diffuse_color;
    vec3 bottom = 0.5 * ambient_color;
    vec3 screen = 1.0 - (1.0 - top) * (1.0 - bottom);

    frag_color = vec4(screen * ao + specular_color * 0.08, 1.0);
    frag_color = vec4(vec3(attenuation) * ao, 1.0);

    // frag_color = vec4(vec3(texture(depth_map, texcoord).x - 0.996923) / (1.0 - 0.996923), 1.0);

    // frag_color = vec4(vec3(ao), 1.0);

    if(gl_FragCoord.x > 320){
        frag_color = vec4(vec3(attenuation), 1.0);
    }

}
)";

            _program = opengl::create_program(vertex_shader, fragment_shader);
        }

        void Main::use()
        {
            glUseProgram(_program);
        }
        void Main::uniform_matrix(GLuint location, const GLfloat* matrix)
        {
            glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
        }
        void Main::uniform_float(GLuint location, const GLfloat value)
        {
            glUniform1f(location, value);
        }
    }
}
}