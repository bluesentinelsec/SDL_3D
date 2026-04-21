#version 330

in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;
uniform int uHasTexture;
uniform vec4 uTint;

out vec4 fragColor;

void main() {
    vec2 uv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);
    vec4 texel = (uHasTexture != 0) ? texture(uTexture, uv) : vec4(1.0);
    fragColor = texel * vColor * uTint;
    if (fragColor.a <= 0.0) discard;
}
