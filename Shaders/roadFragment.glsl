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

	// Street-lamp pools on the asphalt, worked out from the road's own coordinates. The lamps stand
	// every LAMP_SPACING metres alternating sides (see City::Generate), so the three lamps that can
	// reach this pixel are known without any light list. Every lamp along the road therefore lights
	// the road, near or far: no lights popping on as you approach.
	const float LAMP_SPACING = 48.0;   // must match City::Generate
	const float LAMP_SIDE    = 6.0;    // lamp head's distance from the centre line
	const float LAMP_HEIGHT  = 7.2;    // lamp head above the road surface
	float lampsOn   = saturate((environment.x - 0.2) * 3.0);
	vec3  lampLight = vec3(0.0);
	if (lampsOn > 0.0) {
		float s           = v * 8.0;   // metres along the road (v is distance / 8 m)
		float centreIndex = floor(s / LAMP_SPACING + 0.5);
		for (int k = -1; k <= 1; ++k) {
			float index = centreIndex + float(k);
			if (index < 0.0) {
				continue;
			}
			float side = (mod(index, 2.0) < 0.5) ? 1.0 : -1.0;
			vec3  d    = vec3(s - index * LAMP_SPACING, LAMP_HEIGHT, (u - 0.5) * 10.0 - side * LAMP_SIDE);
			float dist = length(d);
			float fall = saturate(1.0 - dist / 45.0);
			lampLight += vec3(1.0, 0.72, 0.40) * 3.0 * (d.y / dist) * fall * fall;
		}
		lampLight *= lampsOn;
	}

	vec3  N      = normalize(IN.normal);
	vec3  colour = ApplyDirectional(albedo, N, ShadowFactor(IN.worldPos, N))
	             + albedo * lampLight
	             + ApplySpotLights(albedo, N, IN.worldPos);   // the car's headlights
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
