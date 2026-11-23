#version 430 core

// Final post-processing pass: HDR scene + bloom -> tone mapping -> vignette and colour grade ->
// fade to black (used between camera shots) -> the screen.
layout(binding = 0) uniform sampler2D sceneTex;
layout(binding = 1) uniform sampler2D bloomTex;

uniform float bloomAmount;      // 0 = bloom off
uniform float vignetteAmount;   // 0 = vignette and colour grade off
uniform float fade;             // 0 = normal, 1 = black
uniform float lensAmount;       // 0 = off; chromatic aberration at the edges + a little film grain
uniform float grainSeed;        // changes every frame so the grain moves

in vec2 uv;
out vec4 fragColour;

// Leaves normal brightness untouched (below 0.8) and rolls the bright values off smoothly towards 1,
// so HDR highlights do not clip to flat white and the daytime look is unchanged.
vec3 ToneMap(vec3 x) {
	vec3 over = max(x - 0.8, vec3(0.0));
	return min(x, vec3(0.8)) + 0.2 * (1.0 - exp(-over / 0.2));
}

void main(void) {
	vec3 scene;
	if (lensAmount > 0.0) {
		// Red and blue are sampled slightly apart, more so towards the corners (like a real lens).
		vec2 c   = uv - 0.5;
		vec2 off = c * dot(c, c) * 0.02;
		scene = vec3(texture(sceneTex, uv + off).r, texture(sceneTex, uv).g, texture(sceneTex, uv - off).b);
	}
	else {
		scene = texture(sceneTex, uv).rgb;
	}
	vec3 colour = scene + texture(bloomTex, uv).rgb * bloomAmount;
	colour = ToneMap(colour);

	if (vignetteAmount > 0.0) {
		float d = length(uv - 0.5) * 1.35;
		colour *= 1.0 - vignetteAmount * smoothstep(0.40, 1.00, d);

		// Colour grade: a touch more saturation and contrast.
		float luma = dot(colour, vec3(0.299, 0.587, 0.114));
		colour = mix(vec3(luma), colour, 1.10);
		colour = (colour - 0.5) * 1.05 + 0.5;
	}

	if (lensAmount > 0.0) {
		float n = fract(sin(dot(gl_FragCoord.xy + grainSeed * 91.7, vec2(12.9898, 78.233))) * 43758.5453);
		colour += (n - 0.5) * 0.022;
	}

	fragColour = vec4(colour * (1.0 - fade), 1.0);
}
