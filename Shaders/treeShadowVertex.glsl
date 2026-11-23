#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "instance.glslh"
#include "wind.glslh"

// Depth-only pass for trees: same placement and sway as treeVertex.glsl.
void main(void) {
	Instance inst = instances[gl_InstanceID];
	vec3 world    = InstanceToWorld(inst, position);
	world        += WindOffset(world, position.y, inst.sizeStyle.y);
	gl_Position   = shadowViewProj * vec4(world, 1.0);
}
