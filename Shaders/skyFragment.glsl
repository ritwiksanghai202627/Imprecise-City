#version 430 core
#include "frame.glslh"
#include "support.glslh"
#include "sky.glslh"

in vec3 viewDir;
out vec4 fragColour;

void main(void) {
	fragColour = vec4(SkyColour(normalize(viewDir)), 1.0);
}
