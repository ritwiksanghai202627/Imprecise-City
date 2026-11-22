#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"

layout(binding = 0) uniform sampler2D diffuseTex;   // the renderer binds a 1x1 white texture when a model has none
uniform vec4 baseColour;                            // glTF baseColorFactor of the current sub-mesh

in Vertex {
	vec3 worldPos;
	vec3 normal;
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	vec3 N      = normalize(IN.normal);
	vec3 albedo = texture(diffuseTex, IN.texCoord).rgb * baseColour.rgb;

	vec3  colour = ApplyDirectional(albedo, N, ShadowFactor(IN.worldPos, N));
	float dist   = length(IN.worldPos - cameraPos.xyz);

	fragColour = vec4(ApplyFog(colour, dist), 1.0);
}
