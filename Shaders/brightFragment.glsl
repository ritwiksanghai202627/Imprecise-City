#version 430 core

// Bloom step 1: shrink the HDR scene to a quarter of the resolution and keep only what is brighter
// than a normal surface can be (lit windows, lamps, neon, the sun). Surfaces in daylight stay below
// the threshold, so by day almost nothing blooms; at night the lights do.
layout(binding = 0) uniform sampler2D sceneTex;
uniform vec2 texelSize;   // size of one texel of the HDR scene

in vec2 uv;
out vec4 fragColour;

void main(void) {
	// Four bilinear taps = a 4x4 box average: cheap downsampling that does not shimmer.
	vec3 c = texture(sceneTex, uv + texelSize * vec2(-1.0, -1.0)).rgb
	       + texture(sceneTex, uv + texelSize * vec2( 1.0, -1.0)).rgb
	       + texture(sceneTex, uv + texelSize * vec2(-1.0,  1.0)).rgb
	       + texture(sceneTex, uv + texelSize * vec2( 1.0,  1.0)).rgb;
	c *= 0.25;

	float peak = max(c.r, max(c.g, c.b));
	fragColour = vec4(c * smoothstep(0.9, 1.5, peak), 1.0);
}
