#version 330

layout(location = 0) in vec3 aPosition;

uniform mat4 uLightMVP;

void main() {
    gl_Position = uLightMVP * vec4(aPosition, 1.0);
}
