#version 430 core
#include "attributes.glslh"

// Full-screen quad pinned to the far plane. The view direction of each pixel comes from
// un-projecting the pixel with the camera's rotation (no translation: the sky never gets closer).
uniform mat4 inverseProjView;

out vec3 viewDir;

void main(void) {
	vec4 p      = inverseProjView * vec4(position.xy, 1.0, 1.0);
	viewDir     = p.xyz / p.w;
	gl_Position = vec4(position.xy, 1.0, 1.0);   // z = w, so depth is exactly 1.0 (the far plane)
}
