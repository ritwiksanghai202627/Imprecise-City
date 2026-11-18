#version 330 core
uniform sampler2D diffuseTex;
uniform sampler2D patternTex; //For Random Challenge: A Splatter of Fact!
uniform sampler2D rockTex; //For Random Challenge: A Splatter of Fact!
uniform sampler2D sandTex; //For Random Challenge: A Splatter of Fact!

in Vertex {
	vec2 texCoord;
} IN;

out vec4 fragColour;

void main(void) {
	//For Random Challenge: That's where I draw the line!
	/*int frequency = 4;
	vec2 cell = (IN.texCoord * frequency) % 1;
	bool isLine = (cell.x <= 0.01 || cell.x >= 0.99) || (cell.y <= 0.01 || cell.y >= 0.99);
	float line = isLine ? 1.0 : 0.0;

	if (line == 0) discard;
	fragColour = vec4(0.7, 0.1, 0.7, 1.0);*/

	//For Random Challenge: A Splatter of Fact!
	/*vec4 grass = texture(diffuseTex, IN.texCoord);
	vec4 sand = texture(sandTex, IN.texCoord);
	vec4 rock = texture(rockTex, IN.texCoord);

	vec2 patternUV = IN.texCoord / 16.0f;
	vec4 pattern = texture(patternTex, patternUV);

	float weight = pattern.r + pattern.g + pattern.b;

	fragColour = ((grass * pattern.r) + (rock * pattern.g) + (sand * pattern.b)) / weight;*/

	//Original
	fragColour = texture(diffuseTex, IN.texCoord) /** vec4(0.25, 0.0, 0.35, 1.0)*/;
}