#version 450
struct PointLight { vec4 positionRadius; vec4 colorIntensity; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirectionAmbient;
    vec4 lightColorIntensity;
    PointLight points[4];
} frame;
layout(set=0,binding=1) uniform sampler2D baseTexture;
layout(push_constant) uniform Draw {
    mat4 model;
    vec4 baseColor;
    vec4 emissionMetallic;
    vec4 parameters;
} draw;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 vertexColor;
layout(location=0) out vec4 outputColor;
const float PI = 3.14159265;
vec3 safeNormalize(vec3 value, vec3 fallback) {
    float magnitude = dot(value,value);
    return magnitude > 0.00000001 ? value*inversesqrt(magnitude) : fallback;
}
vec3 illumination(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 albedo) {
    float nl = max(dot(n,l),0);
    if (draw.parameters.y < 2.5) return albedo * radiance * nl;
    vec3 h = safeNormalize(v+l,n);
    float nv = max(dot(n,v),0.001);
    float nh = max(dot(n,h),0);
    float vh = max(dot(v,h),0);
    float roughness = max(draw.parameters.x,0.05);
    float a2 = pow(roughness,4);
    float denominator = nh*nh*(a2-1)+1;
    float distribution = a2 / max(PI*denominator*denominator,0.0001);
    float k = pow(roughness+1,2)/8;
    float geometry = (nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
    float metallic = draw.emissionMetallic.w;
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
    if (model >= 2 && draw.parameters.z < 0.5) {
        vec3 n = safeNormalize(worldNormal,vec3(0,1,0));
        if (!gl_FrontFacing) n = -n;
        vec3 v = safeNormalize(frame.cameraPosition.xyz-worldPosition,n);
        vec3 lightDirection = -frame.lightDirectionAmbient.xyz;
        lit = base.rgb * frame.lightDirectionAmbient.w;
        if (dot(lightDirection,lightDirection) > 0.00001)
            lit += illumination(n,v,normalize(lightDirection),
                frame.lightColorIntensity.rgb*frame.lightColorIntensity.w,base.rgb);
        for (int i=0;i<4;++i) {
            float radius = frame.points[i].positionRadius.w;
            vec3 delta = frame.points[i].positionRadius.xyz-worldPosition;
            float distance = length(delta);
            if (radius > 0 && distance > 0.0001 && distance < radius) {
                float falloff = pow(1-distance/radius,2);
                lit += illumination(n,v,delta/distance,
                    frame.points[i].colorIntensity.rgb*frame.points[i].colorIntensity.w*falloff,base.rgb);
            }
        }
    }
    outputColor = vec4(lit + draw.emissionMetallic.xyz,base.a);
}
