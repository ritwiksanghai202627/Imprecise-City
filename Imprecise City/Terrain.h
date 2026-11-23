#pragma once

#include "ProceduralMesh.h"
#include "RoadPath.h"

#include <memory>
#include <vector>

/*
Procedural island heightfield, levelled around the road, split into square chunks.

The grid is gridSize x gridSize vertices, cellSize world units apart, centred on the origin.
Heights come from noise shaped by a radial falloff (so it is an island), then the road
carves a level corridor into it.

Why chunks: the scene graph can only cull what it can test. One giant terrain mesh is
always "visible", so it is cut into kCellsPerChunk-sized tiles. Each tile gets a mesh whose
vertices are relative to the tile centre, plus a bounding sphere, so a tile that is off
screen is never sent to the GPU. Texturing happens in the fragment shader from height and
slope, so a tile only needs positions, normals and texture coordinates.
*/
struct TerrainPlateau {
	float x = 0.0f, z = 0.0f, y = 0.0f;
	float inner = 0.0f, outer = 0.0f;   // outer <= inner means "no plateau"
};

class Terrain {
public:
	static const int kCellsPerChunk = 32;

	struct Chunk {
		std::unique_ptr<ProceduralMesh> mesh;
		Vector3 centre;   // world-space centre of the tile (the mesh is built around this point)
		float   radius;   // bounding sphere radius around 'centre'
	};

	Terrain(const RoadPath& road, const TerrainPlateau& plateau = TerrainPlateau(), int gridSize = 257, float cellSize = 8.0f);

	const std::vector<Chunk>& GetChunks() const { return chunks; }

	float HeightAt(float x, float z) const;

	float GetHalfExtent() const { return halfExtent; }   // the grid spans -halfExtent .. +halfExtent on x and z

	static float IslandHeight(float x, float z);

private:
	void    CarveRoad(const RoadPath& road);
	void    BuildChunks();
	Vector3 NormalAt(int ix, int iz) const;   // from the whole grid, so it ignores chunk borders

	int   gridSize;
	float cellSize;
	float halfExtent;
	std::vector<float> heights;
	std::vector<Chunk> chunks;
};
