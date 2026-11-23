#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "instance.glslh"

void main(void) {
	gl_Position = shadowViewProj * vec4(InstanceToWorld(instances[gl_InstanceID], position), 1.0);
}
