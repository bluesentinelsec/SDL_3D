in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;
uniform int uHasTexture;
uniform vec4 uTint;
uniform int uOverlayEffect;
uniform float uOverlayEffectProgress;
uniform float uOverlayEffectSeed;
uniform float uOverlayEffectColumns;

out vec4 fragColor;

float overlayHash(float n)
{
    return fract(sin(n) * 43758.5453123);
}

void main()
{
    vec2 uv = vTexCoord;
    if (uOverlayEffect == 1)
    {
        float columns = max(uOverlayEffectColumns, 1.0);
        float column = floor(clamp(uv.x, 0.0, 0.999999) * columns);
        float rnd = overlayHash(column + uOverlayEffectSeed);
        float delay = rnd * 0.45;
        float melt = clamp((uOverlayEffectProgress - delay) / 0.55, 0.0, 1.0);
        float wobble = (rnd - 0.5) * 0.025 * melt;
        float yShift = melt * (1.05 + rnd * 0.6);
        uv = vec2(uv.x + wobble, uv.y - yShift);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            discard;
    }

    vec4 texel = (uHasTexture != 0) ? texture(uTexture, uv) : vec4(1.0);
    fragColor = texel * vColor * uTint;
    if (fragColor.a <= 0.0)
        discard;
}
