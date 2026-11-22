#version 450
layout(location = 0) out vec3 faceColor;
#ifdef ECHO_CUBE_INTERACTIVE
layout(push_constant) uniform Rotation { vec2 angles; } rotation;
#endif

const vec3 points[8] = vec3[](
    vec3(-1, -1, -1), vec3(1, -1, -1), vec3(1, 1, -1), vec3(-1, 1, -1),
    vec3(-1, -1, 1),  vec3(1, -1, 1),  vec3(1, 1, 1),  vec3(-1, 1, 1)
);
const int indices[36] = int[](
    0, 4, 7, 0, 7, 3,  0, 1, 5, 0, 5, 4,  0, 3, 2, 0, 2, 1,
    1, 2, 6, 1, 6, 5,  3, 7, 6, 3, 6, 2,  4, 5, 6, 4, 6, 7
);
const vec3 colors[6] = vec3[](
#ifdef ECHO_CUBE_INTERACTIVE
    vec3(0.65, 0.25, 0.85), vec3(0.15, 0.80, 0.35), vec3(0.95, 0.18, 0.55),
#else
    vec3(0.10, 0.15, 0.22), vec3(0.12, 0.12, 0.20), vec3(0.09, 0.11, 0.17),
#endif
    vec3(0.95, 0.28, 0.12), vec3(1.00, 0.72, 0.20), vec3(0.18, 0.65, 0.95)
);

void main()
{
    vec3 p = points[indices[gl_VertexIndex]];
#ifdef ECHO_CUBE_INTERACTIVE
    float cy = cos(rotation.angles.x), sy = sin(rotation.angles.x);
    float cx = cos(rotation.angles.y), sx = sin(rotation.angles.y);
    p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
#endif
    float x = 0.707107 * (p.x - p.z);
    float y = 0.816497 * p.y - 0.408248 * (p.x + p.z);
    float w = 4.0 - 0.577350 * (p.x + p.y + p.z);
    gl_Position = vec4(1.35 * x, -1.35 * y, 0.5 * w, w);
    faceColor = colors[gl_VertexIndex / 6];
}
