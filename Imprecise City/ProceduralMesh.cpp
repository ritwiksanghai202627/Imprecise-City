#include "ProceduralMesh.h"

#include <algorithm>
#include <cassert>
#include <cmath>

ProceduralMesh::ProceduralMesh(const std::vector<Vector3>& positions,
                               const std::vector<Vector2>& texCoords,
                               const std::vector<unsigned int>& triangleIndices,
                               const std::vector<Vector3>* normalsIn) {
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

	if (normalsIn) {
		assert(normalsIn->size() == positions.size());
		normals = new Vector3[numVertices];
		std::copy(normalsIn->begin(), normalsIn->end(), normals);
	}
	else {
		GenerateNormals();   // must come before BufferData, which uploads whichever arrays exist
	}
	BufferData();
}

namespace {
	const float kPi = 3.14159265358979f;

	// sign(x) * |x|^e : the building block of a superellipsoid.
	float SignedPow(float x, float e) {
		return std::copysign(std::pow(std::fabs(x), e), x);
	}
}

ProceduralMesh* BuildRoundedBox(const Vector3& radii, float e, int stacks, int slices) {
	std::vector<Vector3>      positions, normals;
	std::vector<Vector2>      uvs;
	std::vector<unsigned int> indices;

	// v runs bottom to top, u runs round the middle (x towards z).
	for (int i = 0; i <= stacks; ++i) {
		const float v  = -kPi * 0.5f + kPi * i / stacks;
		const float cv = std::cos(v), sv = std::sin(v);
		for (int j = 0; j <= slices; ++j) {
			const float u  = -kPi + 2.0f * kPi * j / slices;
			const float cu = std::cos(u), su = std::sin(u);

			positions.push_back(Vector3(
				radii.x * SignedPow(cv, e) * SignedPow(cu, e),
				radii.y * SignedPow(sv, e),
				radii.z * SignedPow(cv, e) * SignedPow(su, e)));

			// Analytic normal: same expression with exponent (2 - e), divided by the radii.
			Vector3 n(
				SignedPow(cv, 2.0f - e) * SignedPow(cu, 2.0f - e) / radii.x,
				SignedPow(sv, 2.0f - e) / radii.y,
				SignedPow(cv, 2.0f - e) * SignedPow(su, 2.0f - e) / radii.z);
			const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			normals.push_back(Vector3(n.x / len, n.y / len, n.z / len));

			uvs.push_back(Vector2((float)j / slices, (float)i / stacks));
		}
	}

	// Counter-clockwise seen from outside (checked in the unit test for this function).
	const int stride = slices + 1;
	for (int i = 0; i < stacks; ++i) {
		for (int j = 0; j < slices; ++j) {
			const unsigned a = i * stride + j, b = a + stride, c = a + 1, d = b + 1;
			indices.push_back(a); indices.push_back(b); indices.push_back(c);
			indices.push_back(c); indices.push_back(b); indices.push_back(d);
		}
	}
	return new ProceduralMesh(positions, uvs, indices, &normals);
}
