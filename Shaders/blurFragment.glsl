#version 430 core

// Bloom step 2: separable Gaussian blur (run once horizontally, once vertically, twice over).
// Nine taps per pass instead of an 81-tap 2D kernel.
layout(binding = 0) uniform sampler2D sourceTex;
uniform vec2 direction;   // one texel step: (1/width, 0) for horizontal, (0, 1/height) for vertical

in vec2 uv;
out vec4 fragColour;

void main(void) {
	const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

	vec3 sum = texture(sourceTex, uv).rgb * weights[0];
	for (int i = 1; i < 5; ++i) {
		sum += texture(sourceTex, uv + direction * float(i)).rgb * weights[i];
		sum += texture(sourceTex, uv - direction * float(i)).rgb * weights[i];
	}
	fragColour = vec4(sum, 1.0);
}
