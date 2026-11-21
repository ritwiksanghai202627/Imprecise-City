#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	vec3 N = vec3(0.0, 1.0, 0.0);                        // flat sea for now, waves come later
	vec3 V = normalize(cameraPos.xyz - IN.worldPos);

	// Fresnel: looking along the surface reflects the sky, looking down shows the deep colour.
	float fresnel = pow(1.0 - saturate(V.y), 4.0);
	vec3  deep    = vec3(0.02, 0.12, 0.22);
	vec3  colour  = lerp(deep, fogColour.rgb, 0.1 + 0.9 * fresnel);

	// Sun glint
	vec3 R = reflect(-V, N);
	colour += sunColour.rgb * sunDir.w * pow(saturate(dot(R, sunDir.xyz)), 256.0);

	float dist = length(IN.worldPos - cameraPos.xyz);
	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
