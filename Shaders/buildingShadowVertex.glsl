#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "instance.glslh"

// Depth-only pass for instanced city geometry: place each vertex as the sun sees it.
void main(void) {
	gl_Position = shadowViewProj * vec4(InstanceToWorld(instances[gl_InstanceID], position), 1.0);
}
