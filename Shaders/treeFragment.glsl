#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec3 local;
} IN;

flat in float style;

out vec4 fragColour;

// Trees are untextured: trunk or leaves is decided from the position in the unit tree, and the
// colour from the species and a per-tree seed, so no two neighbours look identical.
void main(void) {
	vec3 N = normalize(IN.normal);

	bool  trunk = IN.local.y < 0.46 && length(IN.local.xz) < 0.065;
	bool  broad = style >= 2.0;
	float seed  = fract(style);

	vec3 albedo;
	if (trunk) {
		albedo = vec3(0.20, 0.13, 0.08);
	}
	else {
		vec3 darker  = broad ? vec3(0.13, 0.26, 0.06) : vec3(0.05, 0.13, 0.07);
		vec3 lighter = broad ? vec3(0.24, 0.34, 0.07) : vec3(0.09, 0.21, 0.09);
		albedo = lerp(darker, lighter, seed) * (0.7 + 0.6 * IN.local.y);   // sunlit tops, darker insides
	}

	float dist   = length(IN.worldPos - cameraPos.xyz);
	float shadow = ShadowFactor(IN.worldPos, N);
	vec3  colour = ApplyLighting(albedo, N, IN.worldPos, shadow);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
