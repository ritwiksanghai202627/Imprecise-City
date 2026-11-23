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

	// Seasons (timeInfo: y = wetness, z = snow cover, w = autumn colour): broadleaf trees turn rust and drop their
	// leaves in winter (darker, thinner colour), every tree gets snow on its upper surfaces and is darker when wet.
	if (!trunk) {
		if (broad) {
			vec3 rust = lerp(vec3(0.62, 0.30, 0.05), vec3(0.62, 0.12, 0.06), seed) * (0.7 + 0.6 * IN.local.y);
			albedo = lerp(albedo, rust, timeInfo.w);
			albedo = lerp(albedo, vec3(0.20, 0.15, 0.11), timeInfo.z * 0.8);
		}
		albedo = lerp(albedo, vec3(0.90, 0.93, 0.98), timeInfo.z * saturate(N.y * 1.6 - 0.2));
	}
	albedo *= 1.0 - 0.25 * timeInfo.y;

	float dist   = length(IN.worldPos - cameraPos.xyz);
	float shadow = ShadowFactor(IN.worldPos, N);
	vec3  colour = ApplyLighting(albedo, N, IN.worldPos, shadow);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
