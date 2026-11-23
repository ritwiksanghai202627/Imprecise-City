#version 430 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 colour;

uniform vec2 screenSize;   // window size in pixels

out Vertex {
	vec2 texCoord;
	vec4 colour;
} OUT;

void main(void) {
	gl_Position  = vec4(position.x / screenSize.x * 2.0 - 1.0, 1.0 - position.y / screenSize.y * 2.0, 0.0, 1.0);
	OUT.texCoord = texCoord;
	OUT.colour   = colour;
}
