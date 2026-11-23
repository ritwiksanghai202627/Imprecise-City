#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "water.glslh"
#include "lighting.glslh"

layout(binding = 5) uniform samplerCube envMap;   // sky baked by the renderer (same unit the car uses)

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	float dist = length(IN.worldPos - cameraPos.xyz);

	// World-space size of this pixel on the water surface. Drives the wave and glitter fades.
	vec2  fp       = fwidth(IN.worldPos.xz);
	float footprint = max(fp.x, fp.y);

	// Wavy normal. Shading only: the sea is one large quad, so there is no geometry to displace.
	vec3 N = WaterNormal(IN.worldPos.xz, timeInfo.x, footprint);
	vec3 V = normalize(cameraPos.xyz - IN.worldPos);

	// Fresnel: looking along the surface mirrors the sky, looking down shows the deep colour.
	float fresnel = pow(1.0 - saturate(dot(N, V)), 5.0);
	vec3  deep    = vec3(0.02, 0.12, 0.22);

	// Real sky reflection from the environment cube map (clouds included). Blur it with distance:
	// distant water is smooth, and a sharp 128^2 cube map would shimmer on the horizon.
	vec3 R    = reflect(-V, N);
	float mip = 1.0 + 2.0 * saturate(dist / 1500.0);
	vec3 sky  = textureLod(envMap, R, mip).rgb;
	vec3 colour = lerp(deep, sky, saturate(0.08 + 0.92 * fresnel));

	// The cube map's sun disc is blurry at 128^2; a tight analytic glint restores the sparkle.
	float sun = saturate(dot(R, sunDir.xyz));
	if (sun > 0.10) {
		float glitter = WaterGlitter(IN.worldPos.xz, timeInfo.x, footprint);
		float glint   = pow(sun, 120.0) * 0.25 + pow(sun, 1200.0) * 4.0 * (0.35 + 0.9 * glitter);
		colour += sunColour.rgb * sunDir.w * glint;
	}

	// The quad ends about 4000 m out, slightly below the true horizon, and the sky behind it is
	// exactly fogColour. Saturating the water into fogColour before its own edge makes that strip
	// and the sea the same colour, so the quad's edge stops showing as a line.
	vec3 fogged = ApplyFog(colour, dist);
	fogged = lerp(fogged, fogColour.rgb, smoothstep(3000.0, 3800.0, dist));
	fragColour = vec4(fogged, 1.0);
}
