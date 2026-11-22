#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "instance.glslh"

out Vertex {
	vec3 worldPos;
	vec3 normal;
	vec3 local;        // position on the building in metres from its corner (x across, y up, z deep)
} OUT;

flat out vec4 sizeStyle;    // identical for every vertex of an instance, so not interpolated
flat out vec3 faceNormal;   // the unrotated box face normal: tells the fragment shader which wall it is on

void main(void) {
	Instance inst  = instances[gl_InstanceID];
	vec4 worldPos  = vec4(InstanceToWorld(inst, position), 1.0);

	OUT.worldPos = worldPos.xyz;
	OUT.normal   = InstanceRotate(inst, normal);
	OUT.local    = (position + vec3(0.5, 0.0, 0.5)) * inst.sizeStyle.xyz;
	sizeStyle    = inst.sizeStyle;
	faceNormal   = normal;

	gl_Position = projMatrix * viewMatrix * worldPos;
}
