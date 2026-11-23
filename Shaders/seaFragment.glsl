#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"
#include "water.glslh"

layout(binding = 5) uniform samplerCube envMap;   // sky baked by the renderer (same unit the car uses)

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	float dist = length(IN.worldPos - cameraPos.xyz);

	vec2  fp       = fwidth(IN.worldPos.xz);
	float footprint = max(fp.x, fp.y);

	vec3 V = normalize(cameraPos.xyz - IN.worldPos);
	fragColour = vec4(ShadeWater(IN.worldPos, V, footprint, dist, envMap), 1.0);
}
