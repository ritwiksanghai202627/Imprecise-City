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
	// positions and texCoords must have the same length; triangleIndices is a
	// triangle list (3 indices per triangle, counter-clockwise when viewed from the front).
	// If 'normalsIn' is null the normals are generated from the triangles; pass your own
	// when the mesh is one tile of a bigger surface, so tile borders shade seamlessly.
	ProceduralMesh(const std::vector<Vector3>& positions,
	               const std::vector<Vector2>& texCoords,
	               const std::vector<unsigned int>& triangleIndices,
	               const std::vector<Vector3>* normalsIn = nullptr);
};

// A smooth "rounded box" (superellipsoid) with the given half-extents, centred on the origin.
// roundness: 1 = ellipsoid, towards 0 = sharper box. Normals are analytic, so it reflects cleanly.
ProceduralMesh* BuildRoundedBox(const Vector3& radii, float roundness, int stacks, int slices);
