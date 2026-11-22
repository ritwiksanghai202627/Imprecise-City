#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "sky.glslh"

uniform int face;   // 0..5 = +X, -X, +Y, -Y, +Z, -Z (OpenGL's cube map face order)

in vec2 faceUV;
out vec4 fragColour;

// The direction a cube map face texel looks at (OpenGL cube map convention).
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
