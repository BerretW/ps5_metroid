#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 color;
struct PointLight { vec4 positionRadius; vec4 colorIntensity; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirectionAmbient;
    vec4 lightColorIntensity;
    PointLight points[4];
} frame;
layout(push_constant) uniform Draw {
    mat4 model;
    vec4 baseColor;
    vec4 emissionMetallic;
    vec4 parameters;
} draw;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 texcoord;
layout(location=3) out vec4 vertexColor;
void main() {
    vec4 world = draw.model * vec4(position,1);
    bool overlay = draw.parameters.z > 0.5;
    gl_Position = overlay ? world : frame.viewProjection * world;
    worldPosition = world.xyz;
    worldNormal = overlay ? normal : transpose(inverse(mat3(draw.model))) * normal;
    texcoord = uv;
    vertexColor = color;
}
