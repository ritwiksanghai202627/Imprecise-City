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
uniform float rainAmount;       // 0..1 rain streaks
uniform float snowAmount;       // 0..1 snow flakes
uniform float precipTime;       // seconds, drives the fall

in vec2 uv;
out vec4 fragColour;

// Leaves normal brightness untouched (below 0.8) and rolls the bright values off smoothly towards 1,
// so HDR highlights do not clip to flat white and the daytime look is unchanged.
vec3 ToneMap(vec3 x) {
	vec3 over = max(x - 0.8, vec3(0.0));
	return min(x, vec3(0.8)) + 0.2 * (1.0 - exp(-over / 0.2));
}

float CellHash(vec2 id) {
	return fract(sin(dot(id, vec2(127.1, 311.7))) * 43758.5453);
}

// Screen-space rain: thin slanted streaks in three layers (near = long and fast, far = short and faint).
float RainLayer(vec2 uv, float aspect, float density, float t) {
	vec2 p = vec2(uv.x * aspect * density, uv.y * density * 0.08);
	p.y += t * density * 0.10;
	p.x += p.y * 0.12;                         // a slight slant: the wind
	vec2  id = floor(p);
	vec2  f  = fract(p);
	float h  = CellHash(id);
	float x  = 0.15 + 0.7 * fract(h * 7.31);   // where the streak sits in its cell
	float streak = smoothstep(0.05, 0.0, abs(f.x - x)) * smoothstep(0.0, 0.1, f.y) * (1.0 - smoothstep(0.1, 0.55, f.y));
	return step(0.60, h) * streak;
}

// Screen-space snow: soft flakes in three layers (big and soft = near, small and faint = far).
float SnowLayer(vec2 uv, float aspect, float density, float t) {
	vec2 p = vec2(uv.x * aspect, uv.y) * density;
	p.y += t * density * 0.09;
	vec2  id = floor(p);
	vec2  f  = fract(p);
	float h  = CellHash(id);
	vec2  c  = vec2(0.5 + 0.28 * sin(t * (0.5 + h) + h * 30.0), 0.25 + 0.5 * fract(h * 31.0));
	float r  = mix(0.07, 0.17, fract(h * 53.0));
	return step(0.78, h) * smoothstep(r, r * 0.35, length(f - c));
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

	if (rainAmount > 0.01 || snowAmount > 0.01) {
		float aspect = float(textureSize(sceneTex, 0).x) / float(textureSize(sceneTex, 0).y);
		if (rainAmount > 0.01) {
			float r = RainLayer(uv, aspect, 70.0, precipTime) + RainLayer(uv, aspect, 120.0, precipTime) * 0.8 + RainLayer(uv, aspect, 190.0, precipTime) * 0.6;
			colour += vec3(0.55, 0.62, 0.72) * r * 0.55 * rainAmount;
		}
		if (snowAmount > 0.01) {
			float f = SnowLayer(uv, aspect, 14.0, precipTime) + SnowLayer(uv, aspect, 28.0, precipTime) * 0.7 + SnowLayer(uv, aspect, 52.0, precipTime) * 0.5;
			colour += vec3(0.92, 0.95, 1.0) * f * 0.55 * snowAmount;
		}
	}

	if (lensAmount > 0.0) {
		float n = fract(sin(dot(gl_FragCoord.xy + grainSeed * 91.7, vec2(12.9898, 78.233))) * 43758.5453);
		colour += (n - 0.5) * 0.022;
	}

	fragColour = vec4(colour * (1.0 - fade), 1.0);
}
