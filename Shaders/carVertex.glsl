#version 430 core
#include "attributes.glslh"
#include "frame.glslh"

uniform mat4 modelMatrix;

out Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
	vec3 localPos;   // position on the car body, so the fragment shader knows where the lights are
} OUT;

void main(void) {
	vec4 worldPos = modelMatrix * vec4(position, 1.0);

	OUT.worldPos = worldPos.xyz;
	OUT.normal   = normalize(mat3(modelMatrix) * normal);
	OUT.texCoord = texCoord;
	OUT.localPos = position;

	gl_Position = projMatrix * viewMatrix * worldPos;
}
