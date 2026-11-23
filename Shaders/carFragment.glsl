#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"
#include "sky.glslh"

layout(binding = 5) uniform samplerCube envMap;   // sky baked by the renderer
uniform vec4 baseColour;

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
	vec3 localPos;
} IN;

out vec4 fragColour;

// Car paint: diffuse base coat lit by the sun and shadows, plus a clear coat that reflects the
// environment cube map. The Fresnel term makes grazing angles (the car's edges) mirror-like
// while surfaces facing the camera mostly show the paint colour.
void main(void) {
	vec3 N = normalize(IN.normal);
	vec3 V = normalize(cameraPos.xyz - IN.worldPos);

	float dist   = length(IN.worldPos - cameraPos.xyz);
	float shadow = ShadowFactor(IN.worldPos, N) * CloudShadow(IN.worldPos, dist);
	vec3  paint  = ApplyLighting(baseColour.rgb, N, IN.worldPos, shadow);

	vec3  R       = reflect(-V, N);
	vec3  env     = textureLod(envMap, R, 1.0).rgb;
	float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(dot(N, V)), 5.0);
	vec3  tint    = lerp(vec3(1.0), baseColour.rgb, 0.35);   // metallic paint tints its reflection a little
	vec3  colour  = lerp(paint, env * tint, saturate(0.30 + 0.70 * fresnel));

	// Sun glint (Blinn-Phong), hidden when the sun is shadowed.
	vec3 H = normalize(V + sunDir.xyz);
	colour += sunColour.rgb * sunDir.w * pow(saturate(dot(N, H)), 160.0) * shadow;

	// Lamps: tail lights (red) at the rear, headlight lenses (warm white) at the front. The car is
	// 2.2 m half-length along z (front = -z). Masks pick two patches near the corners, low on the body.
	float sideMask   = smoothstep(0.35, 0.5, abs(IN.localPos.x)) * (1.0 - smoothstep(0.82, 0.98, abs(IN.localPos.x)));
	float heightMask = 1.0 - smoothstep(0.08, 0.22, abs(IN.localPos.y - 0.12));
	float lamp       = sideMask * heightMask;
	float rear       = smoothstep(1.55, 1.9,  IN.localPos.z);
	float front      = smoothstep(1.55, 1.9, -IN.localPos.z);
	float night      = environment.x;
	colour += vec3(1.0, 0.04, 0.02) * rear  * lamp * (0.8 + 3.0 * night);
	colour += vec3(1.0, 0.92, 0.75) * front * lamp * (0.3 + 3.0 * night);

	fragColour = vec4(ApplyFog(colour, dist), baseColour.a);
}
