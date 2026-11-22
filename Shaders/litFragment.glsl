#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

uniform vec4 baseColour;

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	vec3  N      = normalize(IN.normal);
	vec3  colour = ApplyDirectional(baseColour.rgb, N, ShadowFactor(IN.worldPos, N));
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), baseColour.a);
}
