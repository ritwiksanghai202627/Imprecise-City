#include "Terrain.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace {
	const float kSeabedHeight = -30.0f;

	float Smoothstep(float edge0, float edge1, float x) {
		float t = (x - edge0) / (edge1 - edge0);
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		return t * t * (3.0f - 2.0f * t);
	}

	// Integer hash -> 0..1. Gives the same value for the same lattice point every run.
	float Hash2D(int x, int y) {
		unsigned int n = static_cast<unsigned int>(x) * 374761393u + static_cast<unsigned int>(y) * 668265263u;
		n = (n ^ (n >> 13)) * 1274126177u;
		n ^= (n >> 16);
		return (n & 0xFFFFu) / 65535.0f;
	}

	// Value noise: random values on a grid, smoothly interpolated between grid points.
	float ValueNoise(float x, float y) {
		const float fx = std::floor(x);
		const float fy = std::floor(y);
		const int   ix = static_cast<int>(fx);
		const int   iy = static_cast<int>(fy);

		float tx = x - fx;
		float ty = y - fy;
		tx = tx * tx * (3.0f - 2.0f * tx);   // smooth the interpolation so cell edges are not visible
		ty = ty * ty * (3.0f - 2.0f * ty);

		const float a = Hash2D(ix,     iy);
		const float b = Hash2D(ix + 1, iy);
		const float c = Hash2D(ix,     iy + 1);
		const float d = Hash2D(ix + 1, iy + 1);

		const float top    = a + (b - a) * tx;
		const float bottom = c + (d - c) * tx;
		return top + (bottom - top) * ty;
	}

	// Fractal noise: several octaves, each twice the frequency and half the weight. Result is 0..1.
	float Fbm(float x, float y, int octaves) {
		float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
		for (int i = 0; i < octaves; ++i) {
			sum += ValueNoise(x, y) * amplitude;
			total += amplitude;
			x *= 2.0f;
			y *= 2.0f;
			amplitude *= 0.5f;
		}
		return sum / total;
	}
}

float Terrain::IslandHeight(float x, float z) {
	const float islandRadius = 900.0f;

	// Distance from the centre, wobbled by low-frequency noise so the coastline is irregular.
	const float d      = std::sqrt(x * x + z * z);
	const float wobble = (Fbm(x * 0.002f + 11.3f, z * 0.002f + 7.1f, 3) - 0.5f) * 500.0f;

	// 1 inland, falling smoothly to 0 offshore.
	const float land = 1.0f - Smoothstep(0.55f * islandRadius, islandRadius, d + wobble);

	// Rolling hills; squaring the noise flattens the valleys and sharpens the peaks.
	const float hills      = Fbm(x * 0.0035f, z * 0.0035f, 5);
	const float landHeight = 8.0f + hills * hills * 130.0f;

	return kSeabedHeight + (landHeight - kSeabedHeight) * land;
}

Terrain::Terrain(const RoadPath& road, int gridSize, float cellSize)
	: gridSize(gridSize),
	  cellSize(cellSize),
	  halfExtent((gridSize - 1) * 0.5f * cellSize),
	  heights(static_cast<size_t>(gridSize) * gridSize) {

	for (int z = 0; z < gridSize; ++z) {
		for (int x = 0; x < gridSize; ++x) {
			heights[static_cast<size_t>(z) * gridSize + x] =
				IslandHeight(x * cellSize - halfExtent, z * cellSize - halfExtent);
		}
	}

	CarveRoad(road);
	BuildChunks();
}

void Terrain::CarveRoad(const RoadPath& road) {
	const float flatHalfWidth = 14.0f;   // fully level out to here (road half-width 5 plus a cell of margin)
	const float shoulderWidth = 48.0f;   // blends back to the natural terrain by here
	const int   radius = static_cast<int>(std::ceil(shoulderWidth / cellSize)) + 1;

	// For every grid vertex remember the closest road sample and that sample's height.
	// Splatting from each road point to the few vertices around it is far cheaper than
	// testing every vertex against every road point.
	const size_t count = heights.size();
	std::vector<float> nearest(count, std::numeric_limits<float>::max());
	std::vector<float> roadHeight(count, 0.0f);

	for (const Vector3& p : road.GetPoints()) {
		const int cx = static_cast<int>(std::floor((p.x + halfExtent) / cellSize));
		const int cz = static_cast<int>(std::floor((p.z + halfExtent) / cellSize));

		for (int dz = -radius; dz <= radius; ++dz) {
			const int iz = cz + dz;
			if (iz < 0 || iz >= gridSize) continue;

			for (int dx = -radius; dx <= radius; ++dx) {
				const int ix = cx + dx;
				if (ix < 0 || ix >= gridSize) continue;

				const float ox = ix * cellSize - halfExtent - p.x;
				const float oz = iz * cellSize - halfExtent - p.z;
				const float dist = std::sqrt(ox * ox + oz * oz);

				const size_t i = static_cast<size_t>(iz) * gridSize + ix;
				if (dist < nearest[i]) {
					nearest[i] = dist;
					roadHeight[i] = p.y;
				}
			}
		}
	}

	// Blend the terrain toward the road height: full strength inside flatHalfWidth,
	// fading to nothing at shoulderWidth.
	for (size_t i = 0; i < count; ++i) {
		if (nearest[i] < shoulderWidth) {
			const float w = 1.0f - Smoothstep(flatHalfWidth, shoulderWidth, nearest[i]);
			heights[i] += (roadHeight[i] - heights[i]) * w;
		}
	}
}

Vector3 Terrain::NormalAt(int ix, int iz) const {
	// Central differences on the full grid (clamped at the edges). Chunks call this with
	// global indices, so two tiles sharing a border vertex get the identical normal.
	auto h = [this](int x, int z) {
		x = std::min(std::max(x, 0), gridSize - 1);
		z = std::min(std::max(z, 0), gridSize - 1);
		return heights[static_cast<size_t>(z) * gridSize + x];
	};
	const Vector3 n(h(ix - 1, iz) - h(ix + 1, iz), 2.0f * cellSize, h(ix, iz - 1) - h(ix, iz + 1));
	return n * (1.0f / std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z));
}

void Terrain::BuildChunks() {
	assert((gridSize - 1) % kCellsPerChunk == 0);
	const int   cells          = kCellsPerChunk;
	const int   chunksPerSide  = (gridSize - 1) / cells;
	const int   vertsPerSide   = cells + 1;
	const float uvPerUnit      = 1.0f / 24.0f;   // one texture repeat every 24 world units

	// Vertices are stored relative to the chunk centre, so every chunk has the same index list.
	// Each grid cell is two triangles; with a = (x,z), b = (x+1,z), c = (x,z+1), d = (x+1,z+1),
	// (a,c,b) and (b,c,d) are counter-clockwise seen from above, so the front face points up.
	std::vector<unsigned int> indices;
	indices.reserve(static_cast<size_t>(cells) * cells * 6);
	for (int z = 0; z < cells; ++z) {
		for (int x = 0; x < cells; ++x) {
			const unsigned int a = z * vertsPerSide + x;
			const unsigned int b = a + 1;
			const unsigned int c = a + vertsPerSide;
			const unsigned int d = c + 1;
			indices.push_back(a); indices.push_back(c); indices.push_back(b);
			indices.push_back(b); indices.push_back(c); indices.push_back(d);
		}
	}

	std::vector<Vector3> positions, normals;
	std::vector<Vector2> texCoords;
	const size_t vertCount = static_cast<size_t>(vertsPerSide) * vertsPerSide;
	positions.reserve(vertCount); normals.reserve(vertCount); texCoords.reserve(vertCount);
	chunks.reserve(static_cast<size_t>(chunksPerSide) * chunksPerSide);

	for (int cz = 0; cz < chunksPerSide; ++cz) {
		for (int cx = 0; cx < chunksPerSide; ++cx) {
			const int x0 = cx * cells;
			const int z0 = cz * cells;

			float hMin = std::numeric_limits<float>::max();
			float hMax = std::numeric_limits<float>::lowest();
			for (int z = 0; z <= cells; ++z) {
				for (int x = 0; x <= cells; ++x) {
					const float h = heights[static_cast<size_t>(z0 + z) * gridSize + (x0 + x)];
					hMin = std::min(hMin, h);
					hMax = std::max(hMax, h);
				}
			}

			const Vector3 centre((x0 + cells * 0.5f) * cellSize - halfExtent,
			                     (hMin + hMax) * 0.5f,
			                     (z0 + cells * 0.5f) * cellSize - halfExtent);

			positions.clear(); normals.clear(); texCoords.clear();
			for (int z = 0; z <= cells; ++z) {
				for (int x = 0; x <= cells; ++x) {
					const int   ix = x0 + x, iz = z0 + z;
					const float wx = ix * cellSize - halfExtent;
					const float wz = iz * cellSize - halfExtent;
					const float h  = heights[static_cast<size_t>(iz) * gridSize + ix];

					positions.push_back(Vector3(wx - centre.x, h - centre.y, wz - centre.z));
					normals.push_back(NormalAt(ix, iz));
					texCoords.push_back(Vector2(wx * uvPerUnit, wz * uvPerUnit));   // world-based, so tiling is continuous
				}
			}

			// Sphere that encloses the tile's footprint (half-diagonal) and its height range.
			const float half = cells * cellSize * 0.5f;
			const float halfHeight = (hMax - hMin) * 0.5f;

			Chunk chunk;
			chunk.mesh   = std::make_unique<ProceduralMesh>(positions, texCoords, indices, &normals);
			chunk.centre = centre;
			chunk.radius = std::sqrt(2.0f * half * half + halfHeight * halfHeight);
			chunks.push_back(std::move(chunk));
		}
	}
}

float Terrain::HeightAt(float x, float z) const {
	const float gx = (x + halfExtent) / cellSize;
	const float gz = (z + halfExtent) / cellSize;

	if (gx < 0.0f || gz < 0.0f || gx > gridSize - 1 || gz > gridSize - 1) {
		return kSeabedHeight;
	}

	const int ix = std::min(static_cast<int>(gx), gridSize - 2);
	const int iz = std::min(static_cast<int>(gz), gridSize - 2);
	const float fx = gx - ix;
	const float fz = gz - iz;

	const float h00 = heights[static_cast<size_t>(iz) * gridSize + ix];
	const float h10 = heights[static_cast<size_t>(iz) * gridSize + ix + 1];
	const float h01 = heights[static_cast<size_t>(iz + 1) * gridSize + ix];
	const float h11 = heights[static_cast<size_t>(iz + 1) * gridSize + ix + 1];

	const float top    = h00 + (h10 - h00) * fx;
	const float bottom = h01 + (h11 - h01) * fx;
	return top + (bottom - top) * fz;
}
