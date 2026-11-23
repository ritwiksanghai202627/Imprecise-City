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

	// Where the view ray hits the y = 0 plane. Computed unconditionally so the derivatives below
	// stay in uniform control flow: the horizon line cuts straight through a row of pixels.
	// t = 0 for directions above the horizon, where no sea is painted.
	float t      = (d.y < -0.0001 && cameraPos.y > 0.0) ? -cameraPos.y / d.y : 0.0;
	vec3  seaPos = cameraPos.xyz + d * t;
	float footprint = max(fwidth(seaPos.x), fwidth(seaPos.z));

	vec3 col = SkyColour(d, 0.0);

	// The 8000 m sea quad ends long before the true horizon. Paint the rest of the ocean into the
	// sky itself: the real quad (drawn afterwards, at closer depth) covers the near part, and both
	// paths call ShadeWater with the same distance, so they join without a line. Fog fades the
	// painted sea into the horizon just as it does the real one.
	if (t > 0.0) {
		col = ShadeWater(seaPos, -d, footprint, t, envMap);
	}

	fragColour = vec4(col, 1.0);
}
