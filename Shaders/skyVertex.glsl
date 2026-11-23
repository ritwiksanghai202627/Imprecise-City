#version 430 core
#include "attributes.glslh"

uniform mat4 inverseProjView;

out vec3 viewDir;

void main(void) {
	vec4 p      = inverseProjView * vec4(position.xy, 1.0, 1.0);
	viewDir     = p.xyz / p.w;
	gl_Position = vec4(position.xy, 1.0, 1.0);   // z = w, so depth is exactly 1.0 (the far plane)
}
