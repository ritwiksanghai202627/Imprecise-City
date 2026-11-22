#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "skinning.glslh"

uniform mat4 modelMatrix;

// Same as shadowVertex.glsl, but the vertex is animated first, so the shadow walks too.
void main(void) {
	gl_Position = shadowViewProj * modelMatrix * (SkinMatrix() * vec4(position, 1.0));
}
