#version 430 core
#include "attributes.glslh"

// Draws a full-screen quad into one face of the environment cube map.
out vec2 faceUV;

void main(void) {
	faceUV      = position.xy;   // -1..1 across the face
	gl_Position = vec4(position.xy, 0.0, 1.0);
}
