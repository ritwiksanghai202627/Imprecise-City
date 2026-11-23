#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "sky.glslh"

uniform int face;

in vec2 faceUV;
out vec4 fragColour;

vec3 CubeDirection(int f, vec2 uv) {
	float u = uv.x;
	float v = uv.y;
	if (f == 0) return vec3( 1.0, -v, -u);
	if (f == 1) return vec3(-1.0, -v,  u);
	if (f == 2) return vec3(  u, 1.0,  v);
	if (f == 3) return vec3(  u, -1.0, -v);
	if (f == 4) return vec3(  u, -v, 1.0);
	return vec3(-u, -v, -1.0);
}

void main(void) {
	fragColour = vec4(SkyColour(normalize(CubeDirection(face, faceUV)), 1.0), 1.0);
}
