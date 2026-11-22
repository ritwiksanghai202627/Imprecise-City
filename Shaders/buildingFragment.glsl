#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec3 local;
} IN;

flat in vec4 sizeStyle;
flat in vec3 faceNormal;

out vec4 fragColour;

float Hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Everything about a building's look is procedural: wall colour, window grid, which windows are
// lit at night, neon shop fronts. No textures, no extra geometry.
void main(void) {
	vec3  N     = normalize(IN.normal);
	float style = sizeStyle.w;
	float night = environment.x;

	vec3 albedo   = vec3(0.5);
	vec3 emissive = vec3(0.0);

	if (style > 10.5) {
		// Lamp head: glows when the street lights are on.
		albedo   = vec3(0.7);
		emissive = vec3(1.0, 0.78, 0.45) * 2.5 * smoothstep(0.2, 0.5, night);
	}
	else if (style > 9.5) {
		albedo = vec3(0.22, 0.23, 0.25);   // lamp pole
	}
	else {
		float seed = style;
		vec3  wall = lerp(vec3(0.58, 0.56, 0.52), vec3(0.34, 0.36, 0.42), fract(seed * 7.0));
		wall       = lerp(wall, vec3(0.50, 0.27, 0.21), step(0.82, fract(seed * 13.0)));   // some brick buildings

		if (abs(faceNormal.y) > 0.5) {
			albedo = wall * 0.55;   // roof
		}
		else {
			// Window grid on the wall: one cell = 3.2 m wide, 3.6 m tall (one storey).
			vec2 fc       = abs(faceNormal.x) > 0.5 ? IN.local.zy : IN.local.xy;
			vec2 cellSize = vec2(3.2, 3.6);
			vec2 id       = floor(fc / cellSize);
			vec2 f        = fract(fc / cellSize);
			bool shopFront = id.y < 1.0;   // ground floor: big glass

			float glass = step(0.18, f.x) * step(f.x, 0.82)
			            * step(shopFront ? 0.10 : 0.22, f.y) * step(f.y, shopFront ? 0.85 : 0.78);

			float h   = Hash(id + vec2(seed * 91.0, seed * 37.0 + (abs(faceNormal.x) > 0.5 ? 5.0 : 0.0)));
			float lit = shopFront ? 1.0 : step(0.45, h);   // about 55% of windows are lit

			vec3 glassColour = lerp(vec3(0.04, 0.07, 0.11), fogColour.rgb * 0.6, 0.35);   // dark glass, tinted by the sky
			albedo = lerp(wall, glassColour, glass);

			vec3 warm = lerp(vec3(1.0, 0.78, 0.45), vec3(0.6, 0.8, 1.0), step(0.85, h));
			vec3 neon = h < 0.33 ? vec3(1.0, 0.15, 0.55) : (h < 0.66 ? vec3(0.1, 0.8, 1.0) : warm);
			emissive  = (shopFront ? neon * 1.6 : warm) * glass * lit * night * 1.4;
		}
	}

	float shadow = ShadowFactor(IN.worldPos, N);
	vec3  colour = ApplyLighting(albedo, N, IN.worldPos, shadow) + emissive;
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
