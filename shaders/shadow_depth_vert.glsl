#version 330

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uLightMVP;
uniform mat4 uModel;
uniform vec3 uLightDir;
uniform float uNormalBias;

void main() {
    /* Apply normal offset bias in the shadow caster vertex shader.
     * This is how Unity prevents shadow acne: inset the geometry along
     * the vertex normal before projecting into light space. The offset
     * scales with the angle between the normal and light direction. */
    vec3 worldPos = (uModel * vec4(aPosition, 1.0)).xyz;
    vec3 worldNormal = normalize(mat3(uModel) * aNormal);
    float NdotL = dot(worldNormal, -uLightDir);
    float bias = uNormalBias * sqrt(1.0 - NdotL * NdotL); /* sin(angle) */
    worldPos += worldNormal * bias;
    gl_Position = uLightMVP * vec4(aPosition + aNormal * bias, 1.0);
}
