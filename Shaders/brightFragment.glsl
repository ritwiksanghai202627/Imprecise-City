#version 430 core

layout(binding = 0) uniform sampler2D sceneTex;
uniform vec2 texelSize;   // size of one texel of the HDR scene

in vec2 uv;
out vec4 fragColour;

void main(void) {
	vec3 c = texture(sceneTex, uv + texelSize * vec2(-1.0, -1.0)).rgb
	       + texture(sceneTex, uv + texelSize * vec2( 1.0, -1.0)).rgb
	       + texture(sceneTex, uv + texelSize * vec2(-1.0,  1.0)).rgb
	       + texture(sceneTex, uv + texelSize * vec2( 1.0,  1.0)).rgb;
	c *= 0.25;

	float peak = max(c.r, max(c.g, c.b));
	fragColour = vec4(c * smoothstep(0.9, 1.5, peak), 1.0);
}
