#version 430 core
#include "attributes.glslh"
#include "frame.glslh"

uniform mat4 modelMatrix;

out Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} OUT;

void main(void) {
	vec4 worldPos = modelMatrix * vec4(position, 1.0);

	OUT.worldPos = worldPos.xyz;
	// mat3(modelMatrix) is only a correct normal transform for uniform scale and for
	// axis-aligned boxes (every face normal stays on one axis). Revisit if we ever
	// non-uniformly scale a curved mesh.
	OUT.normal   = normalize(mat3(modelMatrix) * normal);
	OUT.texCoord = texCoord;

	gl_Position = projMatrix * viewMatrix * worldPos;
}
