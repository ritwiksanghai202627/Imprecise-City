#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "skinning.glslh"

uniform mat4 modelMatrix;

void main(void) {
	gl_Position = shadowViewProj * modelMatrix * (SkinMatrix() * vec4(position, 1.0));
}
