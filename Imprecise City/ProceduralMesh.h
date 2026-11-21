#pragma once

#include "../nclgl/Mesh.h"
#include <vector>

/*
A Mesh built from CPU-side arrays (terrain, road ribbon, later buildings).

Mesh keeps its vertex data in protected raw arrays that its destructor frees, so
this subclass fills those arrays (allocated with new[]) and then uploads them to
the GPU through the base class's BufferData().
*/
class ProceduralMesh : public Mesh {
public:
	// positions and texCoords must have the same length; triangleIndices is a
	// triangle list (3 indices per triangle, counter-clockwise when viewed from the front).
	ProceduralMesh(const std::vector<Vector3>& positions,
	               const std::vector<Vector2>& texCoords,
	               const std::vector<unsigned int>& triangleIndices);
};
