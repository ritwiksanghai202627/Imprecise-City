#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"
#include "sky.glslh"

uniform vec4 baseColour;

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	vec3  N      = normalize(IN.normal);
	float dist   = length(IN.worldPos - cameraPos.xyz);
	vec3  colour = ApplyLighting(baseColour.rgb, N, IN.worldPos,
	                             ShadowFactor(IN.worldPos, N) * CloudShadow(IN.worldPos, dist));

	fragColour = vec4(ApplyFog(colour, dist), baseColour.a);
}
