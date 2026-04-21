#version 330

in vec2 vTexCoord;
uniform sampler2D uScene;
out vec4 fragColor;

void main() {
    fragColor = texture(uScene, vTexCoord);
}
