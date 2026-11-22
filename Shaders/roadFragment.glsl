#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;   // x = 0..1 across the road, y = distance along it in 8 m units
} IN;

out vec4 fragColour;

void main(void) {
	float u = IN.texCoord.x;
	float v = IN.texCoord.y;

	// Painted lines, all procedural: solid edge lines and a dashed centre line.
	float edgeLine   = step(abs(u - 0.06), 0.012) + step(abs(u - 0.94), 0.012);
	float centreLine = step(abs(u - 0.50), 0.008) * step(fract(v), 0.5);

	vec3 albedo = vec3(0.06);
	albedo = lerp(albedo, vec3(0.80), saturate(edgeLine));
	albedo = lerp(albedo, vec3(0.80, 0.65, 0.15), centreLine);

	vec3  N      = normalize(IN.normal);
	vec3  colour = ApplyDirectional(albedo, N, ShadowFactor(IN.worldPos, N));
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
