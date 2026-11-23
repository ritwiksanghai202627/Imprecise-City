#version 430 core

layout(binding = 7) uniform sampler2D fontTex;   // glyph coverage in the red channel (see DebugOverlay.h)
uniform float useTexture;                         // 1 = text (coverage from the font atlas), 0 = flat panel

in Vertex {
	vec2 texCoord;
	vec4 colour;
} IN;

out vec4 fragColour;

void main(void) {
	float coverage = useTexture > 0.5 ? texture(fontTex, IN.texCoord).r : 1.0;
	fragColour = vec4(IN.colour.rgb, IN.colour.a * coverage);
}
