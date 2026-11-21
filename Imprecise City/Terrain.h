#pragma once

#include "ProceduralMesh.h"
#include "RoadPath.h"

#include <memory>
#include <vector>

/*
Procedural island heightfield, levelled around the road.

The grid is gridSize x gridSize vertices, cellSize world units apart, centred on the origin.
Heights come from noise shaped by a radial falloff (so it is an island), then the road
carves a level corridor into it. Texturing happens in the fragment shader from height and
slope, so the mesh only needs positions and texture coordinates.
*/
class Terrain {
public:
	Terrain(const RoadPath& road, int gridSize = 257, float cellSize = 8.0f);

	Mesh* GetMesh() const { return mesh.get(); }

	// Height of the (carved) terrain at a world position; -30 outside the grid.
	float HeightAt(float x, float z) const;

	// The natural island before any carving. The road samples this to choose its own height.
	static float IslandHeight(float x, float z);

private:
	void CarveRoad(const RoadPath& road);
	void BuildMesh();

	int   gridSize;
	float cellSize;
	float halfExtent;
	std::vector<float> heights;
	std::unique_ptr<ProceduralMesh> mesh;
};
