#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "instance.glslh"
#include "wind.glslh"

out Vertex {
	vec3 worldPos;
	vec3 normal;
	vec3 local;        // position in the tree's unit space: y = 0 at the ground, 1 at the top
} OUT;

flat out float style;   // 0..1 pine seed, 2..3 broadleaf seed

// One draw call per patch of trees: gl_InstanceID picks the tree, the unit tree mesh is scaled
// (crown width, height), turned and moved to the tree's position, then bent by the wind.
void main(void) {
	Instance inst = instances[gl_InstanceID];
	vec3 world    = InstanceToWorld(inst, position);
	world        += WindOffset(world, position.y, inst.sizeStyle.y);

	OUT.worldPos = world;
	OUT.normal   = InstanceRotate(inst, normal);
	OUT.local    = position;
	style        = inst.sizeStyle.w;

	gl_Position = projMatrix * viewMatrix * vec4(world, 1.0);
}
