#pragma once

#include "../nclgl/Mesh.h"
#include <vector>

/*
A Mesh built from CPU-side arrays (terrain chunks, road ribbon, later buildings).

Mesh keeps its vertex data in protected raw arrays that its destructor frees, so
this subclass fills those arrays (allocated with new[]) and then uploads them to
the GPU through the base class's BufferData().
*/
class ProceduralMesh : public Mesh {
public:
	ProceduralMesh(const std::vector<Vector3>& positions,
	               const std::vector<Vector2>& texCoords,
	               const std::vector<unsigned int>& triangleIndices,
	               const std::vector<Vector3>* normalsIn = nullptr);

	void DrawInstanced(int instanceCount) const;
};

ProceduralMesh* BuildUnitBox();

ProceduralMesh* BuildRoundedBox(const Vector3& radii, float roundness, int stacks, int slices);
