#include "ProceduralMesh.h"

#include <algorithm>
#include <cassert>

ProceduralMesh::ProceduralMesh(const std::vector<Vector3>& positions,
                               const std::vector<Vector2>& texCoords,
                               const std::vector<unsigned int>& triangleIndices) {
	assert(positions.size() == texCoords.size());

	numVertices = static_cast<GLuint>(positions.size());
	numIndices  = static_cast<GLuint>(triangleIndices.size());
	type        = GL_TRIANGLES;

	vertices      = new Vector3[numVertices];
	textureCoords = new Vector2[numVertices];
	indices       = new unsigned int[numIndices];

	std::copy(positions.begin(), positions.end(), vertices);
	std::copy(texCoords.begin(), texCoords.end(), textureCoords);
	std::copy(triangleIndices.begin(), triangleIndices.end(), indices);

	GenerateNormals();   // must come before BufferData, which uploads whichever arrays exist
	BufferData();
}
