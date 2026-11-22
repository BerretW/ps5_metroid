#version 450
struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
    vec4 attenuation;
    vec4 directionCone;
};
layout(set=0,binding=0,std140) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirectionAmbient;
    vec4 lightColorIntensity;
    vec4 ambientColor;
    vec4 lightScreenToWorld;
    PointLight points[4];
    vec4 shadowInfo;
    vec4 shadowSegments[32];
} frame;
layout(set=0,binding=1) uniform sampler2D baseTexture;
layout(set=0,binding=2) uniform sampler2D normalTexture;
layout(set=0,binding=3) uniform sampler2D metallicRoughnessTexture;
layout(push_constant) uniform Draw {
    mat4 model;
    vec4 baseColor;
    vec4 emissionMetallic;
    vec4 parameters;
    vec4 uvTransform;
} draw;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 vertexColor;
layout(location=4) in vec4 worldTangent;
layout(location=0) out vec4 outputColor;
const float PI = 3.14159265;
vec3 safeNormalize(vec3 value, vec3 fallback) {
    float magnitude = dot(value,value);
    return magnitude > 0.00000001 ? value*inversesqrt(magnitude) : fallback;
}
float cross2(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}
bool blockedByWorld(vec2 lightPosition, vec2 surfacePosition) {
    vec2 ray = surfacePosition - lightPosition;
    for (int index = 0; index < int(frame.shadowInfo.x); ++index) {
        vec4 edge = frame.shadowSegments[index];
        vec2 segment = edge.zw - edge.xy;
        float denominator = cross2(ray, segment);
        if (abs(denominator) < 0.0001) continue;
        vec2 offset = edge.xy - lightPosition;
        float rayDistance = cross2(offset, segment) / denominator;
        float edgeDistance = cross2(offset, ray) / denominator;
        if (rayDistance > 0.001 && rayDistance < 0.999 &&
            edgeDistance >= 0.0 && edgeDistance <= 1.0) return true;
    }
    return false;
}
vec3 illumination(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 albedo, float metallic, float roughness) {
    float nl = max(dot(n,l),0);
    if (draw.parameters.y < 2.5) return albedo * radiance * nl;
    vec3 h = safeNormalize(v+l,n);
    float nv = max(dot(n,v),0.001);
    float nh = max(dot(n,h),0);
    float vh = max(dot(v,h),0);
    roughness = max(roughness,0.05);
    float a2 = pow(roughness,4);
    float denominator = nh*nh*(a2-1)+1;
    float distribution = a2 / max(PI*denominator*denominator,0.0001);
    float k = pow(roughness+1,2)/8;
    float geometry = (nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
    vec3 f0 = mix(vec3(0.04),albedo,metallic);
    vec3 fresnel = f0+(1-f0)*pow(1-vh,5);
    vec3 specular = distribution*geometry*fresnel / max(4*nv*nl,0.001);
    return ((1-fresnel)*(1-metallic)*albedo/PI + specular) * radiance * nl;
}
void main() {
    int model = int(draw.parameters.y+0.5);
    vec4 base = draw.baseColor * vertexColor;
    if (model != 1) base *= texture(baseTexture,texcoord);
    vec3 lit = base.rgb;
    bool canvasWorld = mod(draw.parameters.z, 2.0) > 0.5 &&
                       mod(floor(draw.parameters.z / 4.0), 2.0) > 0.5;
    if (canvasWorld) {
        lit = base.rgb * frame.ambientColor.rgb * frame.lightDirectionAmbient.w;
        vec3 n = safeNormalize(worldNormal, vec3(0,0,1));
        if (draw.parameters.w >= 0.0) {
            vec3 tangent = safeNormalize(worldTangent.xyz - n * dot(n,worldTangent.xyz),
                                         vec3(1,0,0));
            vec3 bitangent = cross(n,tangent) * worldTangent.w;
            vec3 sampled = texture(normalTexture,texcoord).xyz * 2.0 - 1.0;
            sampled.xy *= draw.parameters.w;
            n = safeNormalize(mat3(tangent,bitangent,n) * sampled,n);
        }
        vec3 directional = -frame.lightDirectionAmbient.xyz;
        float directionalLength = length(directional);
        if (frame.lightColorIntensity.w > 0.0 && directionalLength > 0.0001) {
            float diffuse = max(dot(n, directional / directionalLength), 0.0);
            lit += base.rgb * frame.lightColorIntensity.rgb *
                   frame.lightColorIntensity.w * diffuse;
        }
        vec2 canvasPosition = (worldPosition.xy + vec2(1.0)) * vec2(320.0, 180.0);
        for (int i=0;i<4;++i) {
            float radius = frame.points[i].positionRadius.w;
            vec2 screenDelta = frame.points[i].positionRadius.xy-canvasPosition;
            vec2 worldDelta = vec2(
                frame.lightScreenToWorld.x * screenDelta.x +
                    frame.lightScreenToWorld.y * screenDelta.y,
                frame.lightScreenToWorld.z * screenDelta.x +
                    frame.lightScreenToWorld.w * screenDelta.y);
            float distance = length(worldDelta);
            if (radius > 0.0 && distance < radius) {
                float falloff = pow(1.0-distance/radius,2.0);
                vec3 attenuationTerms = frame.points[i].attenuation.xyz;
                float normalizedDistance = distance / radius;
                float attenuation = dot(attenuationTerms, attenuationTerms) > 0.000001
                    ? 1.0 / max(attenuationTerms.x + attenuationTerms.y * normalizedDistance +
                                attenuationTerms.z * normalizedDistance * normalizedDistance, 0.0001)
                    : 1.0;
                vec3 toLight = vec3(screenDelta, frame.points[i].positionRadius.z);
                float diffuse = max(dot(n, safeNormalize(toLight, n)), 0.0);
                float spot = 1.0;
                float coneCosine = frame.points[i].directionCone.w;
                if (coneCosine > 0.0 && distance > 0.0001) {
                    float alignment = dot(normalize(-worldDelta),
                                          safeNormalize(frame.points[i].directionCone.xyz, vec3(1,0,0)).xy);
                    spot = smoothstep(coneCosine, min(coneCosine + 0.08, 1.0), alignment);
                }
                bool shadowed = frame.points[i].attenuation.w > 0.5 &&
                                blockedByWorld(frame.points[i].positionRadius.xy,
                                               canvasPosition);
                if (!shadowed) {
                    lit += base.rgb * frame.points[i].colorIntensity.rgb *
                           frame.points[i].colorIntensity.w * falloff * attenuation * spot *
                           diffuse;
                }
            }
        }
    } else if (model >= 2 && mod(draw.parameters.z, 2.0) < 0.5) {
        float metallic = draw.emissionMetallic.w;
        float roughness = draw.parameters.x;
        if (draw.parameters.z >= 2.0) {
            vec4 pbr = texture(metallicRoughnessTexture,texcoord);
            roughness *= pbr.g;
            metallic *= pbr.b;
        }
        vec3 n = safeNormalize(worldNormal,vec3(0,1,0));
        if (draw.parameters.w >= 0.0) {
            vec3 tangent = safeNormalize(worldTangent.xyz - n * dot(n,worldTangent.xyz),vec3(1,0,0));
            vec3 bitangent = cross(n,tangent) * worldTangent.w;
            vec3 sampled = texture(normalTexture,texcoord).xyz * 2.0 - 1.0;
            sampled.xy *= draw.parameters.w;
            n = safeNormalize(mat3(tangent,bitangent,n) * sampled,n);
        }
        if (!gl_FrontFacing) n = -n;
        vec3 v = safeNormalize(frame.cameraPosition.xyz-worldPosition,n);
        vec3 lightDirection = -frame.lightDirectionAmbient.xyz;
        lit = base.rgb * frame.lightDirectionAmbient.w;
        if (dot(lightDirection,lightDirection) > 0.00001)
            lit += illumination(n,v,normalize(lightDirection),
                frame.lightColorIntensity.rgb*frame.lightColorIntensity.w,base.rgb,metallic,roughness);
        for (int i=0;i<4;++i) {
            float radius = frame.points[i].positionRadius.w;
            vec3 delta = frame.points[i].positionRadius.xyz-worldPosition;
            float distance = length(delta);
            if (radius > 0 && distance > 0.0001 && distance < radius) {
                float falloff = pow(1-distance/radius,2);
                vec3 attenuationTerms = frame.points[i].attenuation.xyz;
                float attenuation = dot(attenuationTerms, attenuationTerms) > 0.000001
                    ? 1.0 / max(attenuationTerms.x + attenuationTerms.y * (distance / radius) +
                                attenuationTerms.z * pow(distance / radius, 2.0), 0.0001)
                    : 1.0;
                float spot = 1.0;
                float coneCosine = frame.points[i].directionCone.w;
                if (coneCosine > 0.0) {
                    float alignment = dot(normalize(-delta),
                                          safeNormalize(frame.points[i].directionCone.xyz, vec3(0,0,1)));
                    spot = smoothstep(coneCosine, min(coneCosine + 0.08, 1.0), alignment);
                }
                lit += illumination(n,v,delta/distance,
                    frame.points[i].colorIntensity.rgb*frame.points[i].colorIntensity.w*
                    falloff*attenuation*spot,base.rgb,metallic,roughness);
            }
        }
    }
    vec3 emission = draw.emissionMetallic.xyz;
    if (canvasWorld && mod(floor(draw.parameters.z / 2.0), 2.0) > 0.5)
        emission *= texture(metallicRoughnessTexture,texcoord).rgb;
    vec3 result = lit + emission;
#ifdef ECHO_OUTPUT_SRGB
    result = clamp(result, 0.0, 1.0);
    result = mix(12.92 * result, 1.055 * pow(result, vec3(1.0 / 2.4)) - 0.055,
                 greaterThan(result, vec3(0.0031308)));
#endif
    outputColor = vec4(result,base.a);
}
