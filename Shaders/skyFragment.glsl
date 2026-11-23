#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "lighting.glslh"
#include "water.glslh"
#include "sky.glslh"

layout(binding = 5) uniform samplerCube envMap;

in vec3 viewDir;
out vec4 fragColour;

void main(void) {
	vec3 d = normalize(viewDir);

	// Ray hit on the y = 0 plane, computed unconditionally so the fwidth below stays in uniform flow.
	float t      = (d.y < -0.0001 && cameraPos.y > 0.0) ? -cameraPos.y / d.y : 0.0;
	vec3  seaPos = cameraPos.xyz + d * t;
	float footprint = max(fwidth(seaPos.x), fwidth(seaPos.z));

	vec3 col = SkyColour(d, 0.0);

	// Continue the sea past the finite quad; the real quad covers the near part.
	if (t > 0.0) {
		col = ShadeWater(seaPos, -d, footprint, t, envMap);
	}

	fragColour = vec4(col, 1.0);
}
