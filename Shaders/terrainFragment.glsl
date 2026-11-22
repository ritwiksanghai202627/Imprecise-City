#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

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

	// Two samples at unrelated scales hide the repeat of a single tiling texture.
	vec3 grass = 0.6 * texture(grassTex, IN.texCoord).rgb
	           + 0.4 * texture(grassTex, IN.texCoord * 0.173 + vec2(0.37, 0.11)).rgb;

	// Texture choice comes from height and steepness, so no splat map is needed.
	float sandAmount = 1.0 - smoothstep(2.0, 10.0, IN.worldPos.y);
	float rockAmount = smoothstep(0.20, 0.40, slope);

	// Texture lookups inside an 'if' need explicit derivatives, otherwise mip selection is
	// undefined where neighbouring pixels take different branches (visible as seams).
	vec2 dx = dFdx(IN.texCoord);
	vec2 dy = dFdy(IN.texCoord);

	// Most pixels are pure grass: only fetch sand / rock where they actually contribute.
	vec3 albedo = grass;
	if (sandAmount > 0.001) {
		albedo = lerp(albedo, textureGrad(sandTex, IN.texCoord, dx, dy).rgb, sandAmount);
	}
	if (rockAmount > 0.001) {
		albedo = lerp(albedo, textureGrad(rockTex, IN.texCoord, dx, dy).rgb, rockAmount);
	}

	vec3  colour = ApplyDirectional(albedo, N, ShadowFactor(IN.worldPos, N));
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
