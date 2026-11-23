#version 430 core
#include "attributes.glslh"

out vec2 uv;

void main(void) {
	uv          = position.xy * 0.5 + 0.5;
	gl_Position = vec4(position.xy, 0.0, 1.0);
}
