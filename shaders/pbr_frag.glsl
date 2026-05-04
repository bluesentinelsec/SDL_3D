#version 330

#define MAX_LIGHTS 8
#define PI 3.14159265

struct Light {
    int type;       /* 0=directional, 1=point, 2=spot */
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float innerCutoff;
    float outerCutoff;
};

layout(std140) uniform SceneUBO {
    mat4 uViewProjection;
    vec3 uCameraPos;
    float _pad0;
    vec3 uAmbient;
    int uLightCount;
    Light uLights[MAX_LIGHTS];
    int uFogMode;
    float uFogStart;
    float uFogEnd;
    float uFogDensity;
    vec3 uFogColor;
    int uTonemapMode;
};

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;
uniform int uHasTexture;
uniform vec4 uTint;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;

out vec4 fragColor;

/* ---- PBR functions ---- */

float DistributionGGX(float NdotH, float r) {
    float a = r * r;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 0.0001);
}

float GeometrySchlickGGX(float NdotV, float r) {
    float k = (r + 1.0) * (r + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 0.0001);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/* ---- Main ---- */

void main() {
    vec2 uv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);
    vec4 texel = (uHasTexture != 0) ? texture(uTexture, uv) : vec4(1.0);
    vec3 albedo = texel.rgb * vColor.rgb * uTint.rgb;
    float alpha = texel.a * vColor.a * uTint.a;
    if (alpha <= 0.0) discard;

    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < uLightCount && i < MAX_LIGHTS; i++) {
        vec3 L;
        float attenuation = 1.0;

        if (uLights[i].type == 0) {
            L = normalize(-uLights[i].direction);
        } else {
            vec3 toLight = uLights[i].position - vWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            if (uLights[i].range > 0.0) {
                float r = dist / uLights[i].range;
                attenuation = exp(-2.8 * r * r);
            }
            if (uLights[i].type == 2) {
                float cosA = dot(-L, normalize(uLights[i].direction));
                float eps = uLights[i].innerCutoff - uLights[i].outerCutoff;
                attenuation *= clamp((cosA - uLights[i].outerCutoff) / max(eps, 0.0001), 0.0, 1.0);
            }
        }

        vec3 radiance = uLights[i].color * uLights[i].intensity * attenuation;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float NDF = DistributionGGX(NdotH, uRoughness);
        float G = GeometrySchlickGGX(NdotV, uRoughness) * GeometrySchlickGGX(NdotL, uRoughness);
        vec3 F = FresnelSchlick(HdotV, F0);

        vec3 spec = (NDF * G * F) / (4.0 * NdotV * NdotL + 0.0001);
        vec3 kD = (1.0 - F) * (1.0 - uMetallic);
        vec3 diff = kD * albedo / PI;

        Lo += (diff + spec) * radiance * NdotL;
    }

    vec3 color = uAmbient * albedo + Lo + uEmissive;

    /* Fog (per-fragment). */
    if (uFogMode > 0) {
        float dist = length(uCameraPos - vWorldPos);
        float fogFactor = 0.0;
        if (uFogMode == 1) fogFactor = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
        else if (uFogMode == 2) fogFactor = 1.0 - exp(-uFogDensity * dist);
        else if (uFogMode == 3) { float d = uFogDensity * dist; fogFactor = 1.0 - exp(-d * d); }
        color = mix(color, uFogColor, fogFactor);
    }

    /* Tonemapping. */
    if (uTonemapMode == 1) color = color / (1.0 + color);
    else if (uTonemapMode == 2) {
        float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        color = clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
    }

    /* sRGB gamma. */
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));

    fragColor = vec4(color, alpha);
}
