#version 430 core
#include "attributes.glslh"
#include "frame.glslh"

uniform mat4 modelMatrix;

// Depth-only pass: just place each vertex as the sun sees it.
void main(void) {
	gl_Position = shadowViewProj * modelMatrix * vec4(position, 1.0);
}
