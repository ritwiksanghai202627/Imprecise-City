#version 430 core
#include "attributes.glslh"
#include "frame.glslh"
#include "skinning.glslh"

uniform mat4 modelMatrix;

out Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} OUT;

void main(void) {
	mat4 skin     = SkinMatrix();
	vec4 worldPos = modelMatrix * (skin * vec4(position, 1.0));

	OUT.worldPos = worldPos.xyz;
	// Skin the normal with the bone rotation, then place it. Fine while bones and the model
	// matrix use uniform scale, which is true for every character we use.
	OUT.normal   = normalize(mat3(modelMatrix) * (mat3(skin) * normal));
	OUT.texCoord = texCoord;

	gl_Position = projMatrix * viewMatrix * worldPos;
}
