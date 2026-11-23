#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"
#include "sky.glslh"

layout(binding = 0) uniform sampler2D grassTex;
layout(binding = 1) uniform sampler2D sandTex;
layout(binding = 2) uniform sampler2D rockTex;

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	vec3  N     = normalize(IN.normal);
	float slope = 1.0 - N.y;          // 0 = flat ground, towards 1 = cliff

	vec2 xz = IN.worldPos.xz;

	// Two samples at unrelated scales hide the repeat of a single tiling texture.
	vec3 grass = 0.6 * texture(grassTex, IN.texCoord).rgb
	           + 0.4 * texture(grassTex, IN.texCoord * 0.173 + vec2(0.37, 0.11)).rgb;

	// Slow colour variation, so a big field is not one flat green.
	grass *= 0.85 + 0.30 * ValueNoise(xz * 0.012);

	// One shared wobble field. The beach border follows a wobbly height instead of a clean
	// contour line, which is what made the sand look like a bathtub ring painted on the hill.
	float wobble = (ValueNoise(xz * 0.035) - 0.5) * 7.0;

	// Texture choice comes from height and steepness, so no splat map is needed.
	float sandAmount = 1.0 - smoothstep(2.0, 10.0, IN.worldPos.y + wobble);
	float rockAmount = smoothstep(0.20, 0.40, slope + wobble * 0.02);

	// Texture lookups inside an 'if' need explicit derivatives, otherwise mip selection is
	// undefined where neighbouring pixels take different branches (visible as seams).
	vec2 dx = dFdx(IN.texCoord);
	vec2 dy = dFdy(IN.texCoord);

	// Most pixels are pure grass: only fetch sand / rock where they actually contribute.
	vec3 albedo = grass;
	if (sandAmount > 0.001) {
		vec3 sand = textureGrad(sandTex, IN.texCoord, dx, dy).rgb;

		// Wet sand just above the waterline: darker, so the beach darkens into the sea instead
		// of stopping at a bright edge.
		float wet = 1.0 - smoothstep(0.0, 2.2, IN.worldPos.y + wobble * 0.15);
		sand *= lerp(1.0, 0.55, wet);

		albedo = lerp(albedo, sand, sandAmount);
	}
	if (rockAmount > 0.001) {
		albedo = lerp(albedo, textureGrad(rockTex, IN.texCoord, dx, dy).rgb, rockAmount);
	}

	// Foam along the waterline. The sea is opaque and hides the terrain below y = 0, so the visible
	// seam is exactly y = 0; a noisy white fringe just above it sits on that seam and hides it.
	// Two counter-moving noise layers so it does not look like static.
	float shore = 1.0 - smoothstep(0.05, 1.0, IN.worldPos.y + wobble * 0.2);
	if (shore > 0.001) {
		float foamNoise = ValueNoise(xz * 1.6 + vec2(timeInfo.x * 0.30, 0.0))
		                * ValueNoise(xz * 2.7 - vec2(0.0, timeInfo.x * 0.22));
		float foam = shore * smoothstep(0.30, 0.55, foamNoise);
		albedo = lerp(albedo, vec3(0.95, 0.97, 0.98), foam);
	}

	vec3  colour = ApplyLighting(albedo, N, IN.worldPos,
	                             ShadowFactor(IN.worldPos, N) * CloudShadow(IN.worldPos));
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
