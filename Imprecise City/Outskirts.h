#pragma once

// The land outside the city: trees (two species, instanced, swaying in the wind) and a small village
// beside the road. Header-only, like DebugOverlay.h, so there is no project file to add.
//
// This file only makes DATA (instance lists) and the three small meshes. The Renderer uploads each
// list to a shader storage buffer and draws it with one instanced call per 192 m patch (the same
// scheme as the city), so patches off screen or outside the shadow box are frustum-culled.
//
// Instance layout (CityInstance, see City.h):
//   tree:   position = base of the trunk, sx = sz = crown width, sy = height (metres),
//           style = seed 0..1 for a pine, 2 + seed for a broadleaf tree
//   house:  a box like a city building, style = 3 + seed (the facade shader gives it a house look)
//   roof:   a gable prism on top of a house, style = 12 + seed

#include "City.h"
#include "ProceduralMesh.h"
#include "RoadPath.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace Outskirts {

namespace detail {
	const float kPi = 3.14159265f;

	// Same input -> same output every run, so the island looks identical every time.
	inline float Hash(int a, int b, int salt) {
		unsigned int n = static_cast<unsigned int>(a) * 374761393u + static_cast<unsigned int>(b) * 668265263u
		               + static_cast<unsigned int>(salt) * 2147483647u;
		n = (n ^ (n >> 13)) * 1274126177u;
		n ^= (n >> 16);
		return (n & 0xFFFFu) / 65536.0f;
	}

	inline float Smoothstep(float e0, float e1, float x) {
		float t = (x - e0) / (e1 - e0);
		t = std::min(std::max(t, 0.0f), 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Smooth value noise (0..1) on a lattice of Hash values: big soft blobs, used for forest patches.
	inline float Noise(float x, float z, int salt) {
		const float fx = std::floor(x), fz = std::floor(z);
		const int   ix = static_cast<int>(fx), iz = static_cast<int>(fz);
		float tx = x - fx, tz = z - fz;
		tx = tx * tx * (3.0f - 2.0f * tx);
		tz = tz * tz * (3.0f - 2.0f * tz);
		const float a = Hash(ix, iz, salt),     b = Hash(ix + 1, iz, salt);
		const float c = Hash(ix, iz + 1, salt), d = Hash(ix + 1, iz + 1, salt);
		const float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
		return top + (bottom - top) * tz;
	}

	// Road samples bucketed into 32 m cells: "how far is the road from here?" looks at 9 buckets
	// instead of every sample (the closed road has about a thousand).
	class RoadGrid {
	public:
		explicit RoadGrid(const RoadPath& road) : cells(kSide * kSide) {
			for (const Vector3& p : road.GetPoints()) {
				const int ix = Cell(p.x), iz = Cell(p.z);
				if (ix >= 0 && iz >= 0 && ix < kSide && iz < kSide) {
					cells[iz * kSide + ix].push_back(std::make_pair(p.x, p.z));
				}
			}
		}
		// Squared distance to the nearest road sample if it is within ~32 m, else 1e9.
		float NearestSq(float x, float z) const {
			const int cx = Cell(x), cz = Cell(z);
			float best = 1e9f;
			for (int dz = -1; dz <= 1; ++dz) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int ix = cx + dx, iz = cz + dz;
					if (ix < 0 || iz < 0 || ix >= kSide || iz >= kSide) {
						continue;
					}
					for (const std::pair<float, float>& q : cells[iz * kSide + ix]) {
						const float ex = q.first - x, ez = q.second - z;
						best = std::min(best, ex * ex + ez * ez);
					}
				}
			}
			return best;
		}
	private:
		static const int   kSide = 80;        // 80 cells x 32 m = 2560 m, more than the island
		static int Cell(float v) { return static_cast<int>(std::floor((v + 1280.0f) / 32.0f)); }
		std::vector<std::vector<std::pair<float, float>>> cells;
	};
}

// ------------------------------------------------------------------------------------------------
// Meshes. All three are built in "unit space": x and z within -0.5..0.5, y from 0 (ground) to 1.
// The instance's size scales them; the tree shader tells trunk from leaves by position.
// ------------------------------------------------------------------------------------------------
struct MeshData {
	std::vector<Vector3> p, n;
	std::vector<Vector2> uv;
	std::vector<unsigned int> idx;

	unsigned int Add(const Vector3& pos, const Vector3& nor) {
		p.push_back(pos); n.push_back(nor); uv.push_back(Vector2(0.0f, 0.0f));
		return static_cast<unsigned int>(p.size() - 1);
	}
	// Adds a triangle, swapping two corners if needed so that it faces the way its normals point
	// (counter-clockwise seen from outside): no hand-checking of winding order.
	void Tri(unsigned int a, unsigned int b, unsigned int c) {
		const Vector3 e1 = p[b] - p[a], e2 = p[c] - p[a];
		const Vector3 g(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
		const Vector3 hint = n[a] + n[b] + n[c];
		if (g.x * hint.x + g.y * hint.y + g.z * hint.z < 0.0f) {
			std::swap(b, c);
		}
		idx.push_back(a); idx.push_back(b); idx.push_back(c);
	}
	ProceduralMesh* Build() const { return new ProceduralMesh(p, uv, idx, &n); }
};

inline Vector3 Unit(float x, float y, float z) {
	const float l = std::sqrt(x * x + y * y + z * z);
	return Vector3(x / l, y / l, z / l);
}

// Open cone with a flat base (the base is seen from below under the lowest branches).
inline void AddCone(MeshData& m, float baseY, float radius, float tipY, int sides) {
	const float height = tipY - baseY;
	for (int k = 0; k < sides; ++k) {
		const float a0 = 2.0f * detail::kPi * k / sides, a1 = 2.0f * detail::kPi * (k + 1) / sides, am = 0.5f * (a0 + a1);
		const unsigned int r0 = m.Add(Vector3(radius * std::cos(a0), baseY, radius * std::sin(a0)), Unit(height * std::cos(a0), radius, height * std::sin(a0)));
		const unsigned int r1 = m.Add(Vector3(radius * std::cos(a1), baseY, radius * std::sin(a1)), Unit(height * std::cos(a1), radius, height * std::sin(a1)));
		const unsigned int tip = m.Add(Vector3(0.0f, tipY, 0.0f), Unit(height * std::cos(am), radius, height * std::sin(am)));
		m.Tri(tip, r0, r1);

		const Vector3 down(0.0f, -1.0f, 0.0f);
		const unsigned int c  = m.Add(Vector3(0.0f, baseY, 0.0f), down);
		const unsigned int b0 = m.Add(Vector3(radius * std::cos(a0), baseY, radius * std::sin(a0)), down);
		const unsigned int b1 = m.Add(Vector3(radius * std::cos(a1), baseY, radius * std::sin(a1)), down);
		m.Tri(c, b0, b1);
	}
}

inline void AddCylinder(MeshData& m, float radius, float y0, float y1, int sides) {
	for (int k = 0; k < sides; ++k) {
		const float a0 = 2.0f * detail::kPi * k / sides, a1 = 2.0f * detail::kPi * (k + 1) / sides;
		const Vector3 n0(std::cos(a0), 0.0f, std::sin(a0)), n1(std::cos(a1), 0.0f, std::sin(a1));
		const unsigned int b0 = m.Add(Vector3(radius * n0.x, y0, radius * n0.z), n0);
		const unsigned int b1 = m.Add(Vector3(radius * n1.x, y0, radius * n1.z), n1);
		const unsigned int t0 = m.Add(Vector3(radius * n0.x, y1, radius * n0.z), n0);
		const unsigned int t1 = m.Add(Vector3(radius * n1.x, y1, radius * n1.z), n1);
		m.Tri(b0, b1, t1);
		m.Tri(b0, t1, t0);
	}
}

// Squashed sphere for a broadleaf crown: 5 rings x 8 segments (about 70 triangles).
inline void AddEllipsoid(MeshData& m, float centreY, float rx, float ry, int rings, int segs) {
	std::vector<unsigned int> grid;
	for (int i = 0; i <= rings; ++i) {
		const float phi = detail::kPi * i / rings;
		for (int j = 0; j <= segs; ++j) {
			const float th = 2.0f * detail::kPi * j / segs;
			const Vector3 pos(rx * std::sin(phi) * std::cos(th), centreY + ry * std::cos(phi), rx * std::sin(phi) * std::sin(th));
			grid.push_back(m.Add(pos, Unit(pos.x / (rx * rx), (pos.y - centreY) / (ry * ry), pos.z / (rx * rx))));
		}
	}
	for (int i = 0; i < rings; ++i) {
		for (int j = 0; j < segs; ++j) {
			const unsigned int v00 = grid[i * (segs + 1) + j],       v01 = grid[i * (segs + 1) + j + 1];
			const unsigned int v10 = grid[(i + 1) * (segs + 1) + j], v11 = grid[(i + 1) * (segs + 1) + j + 1];
			m.Tri(v00, v10, v11);
			m.Tri(v00, v11, v01);
		}
	}
}

inline ProceduralMesh* BuildPine() {   // trunk + three stacked cones, about 90 triangles
	MeshData m;
	AddCylinder(m, 0.04f, 0.0f, 0.35f, 6);
	AddCone(m, 0.16f, 0.50f, 0.52f, 8);
	AddCone(m, 0.38f, 0.40f, 0.76f, 8);
	AddCone(m, 0.60f, 0.28f, 1.00f, 8);
	return m.Build();
}

inline ProceduralMesh* BuildBroadleaf() {   // trunk + one round crown, about 85 triangles
	MeshData m;
	AddCylinder(m, 0.05f, 0.0f, 0.45f, 6);
	AddEllipsoid(m, 0.66f, 0.50f, 0.34f, 5, 8);
	return m.Build();
}

inline ProceduralMesh* BuildRoof() {   // gable roof: ridge along x, 10 triangles
	MeshData m;
	const Vector3 slopeA = Unit(0.0f, 0.5f, 1.0f), slopeB = Unit(0.0f, 0.5f, -1.0f);
	const unsigned int a0 = m.Add(Vector3(-0.5f, 0.0f,  0.5f), slopeA), a1 = m.Add(Vector3(0.5f, 0.0f,  0.5f), slopeA);
	const unsigned int a2 = m.Add(Vector3( 0.5f, 1.0f,  0.0f), slopeA), a3 = m.Add(Vector3(-0.5f, 1.0f, 0.0f), slopeA);
	m.Tri(a0, a1, a2); m.Tri(a0, a2, a3);
	const unsigned int b0 = m.Add(Vector3(-0.5f, 0.0f, -0.5f), slopeB), b1 = m.Add(Vector3(0.5f, 0.0f, -0.5f), slopeB);
	const unsigned int b2 = m.Add(Vector3( 0.5f, 1.0f,  0.0f), slopeB), b3 = m.Add(Vector3(-0.5f, 1.0f, 0.0f), slopeB);
	m.Tri(b0, b1, b2); m.Tri(b0, b2, b3);
	const Vector3 west(-1.0f, 0.0f, 0.0f), east(1.0f, 0.0f, 0.0f), down(0.0f, -1.0f, 0.0f);
	m.Tri(m.Add(Vector3(-0.5f, 0.0f, -0.5f), west), m.Add(Vector3(-0.5f, 0.0f, 0.5f), west), m.Add(Vector3(-0.5f, 1.0f, 0.0f), west));
	m.Tri(m.Add(Vector3( 0.5f, 0.0f, -0.5f), east), m.Add(Vector3( 0.5f, 0.0f, 0.5f), east), m.Add(Vector3( 0.5f, 1.0f, 0.0f), east));
	const unsigned int d0 = m.Add(Vector3(-0.5f, 0.0f, -0.5f), down), d1 = m.Add(Vector3(0.5f, 0.0f, -0.5f), down);
	const unsigned int d2 = m.Add(Vector3( 0.5f, 0.0f,  0.5f), down), d3 = m.Add(Vector3(-0.5f, 0.0f, 0.5f), down);
	m.Tri(d0, d1, d2); m.Tri(d0, d2, d3);
	return m.Build();
}

// ------------------------------------------------------------------------------------------------
// Placement
// ------------------------------------------------------------------------------------------------

// Looks along the road for flat dry land far from the city; returns where the village goes.
// Strict rules first; if no spot passes, the rules loosen (a village on rougher ground is better
// than none: the first version only tried the strict rules and found nothing on this island).
inline bool FindVillageSite(const RoadPath& road, const Terrain& terrain, bool hasCity, const Vector3& cityCentre,
                            Vector3& centre, float& roadDistance) {
	const std::vector<Vector3>& pts = road.GetPoints();
	const float minCityDistance[3] = { 600.0f, 450.0f, 300.0f };   // metres from the city centre
	const float minGround[3]       = { 9.0f, 7.0f, 5.5f };         // lowest ground allowed within 50 m (the beach is below ~5)

	for (int attempt = 0; attempt < 3; ++attempt) {
		float best = 1e9f;
		for (size_t i = 0; i < pts.size(); i += 5) {
			const Vector3& p = pts[i];
			if (hasCity) {
				const float dx = p.x - cityCentre.x, dz = p.z - cityCentre.z;
				if (dx * dx + dz * dz < minCityDistance[attempt] * minCityDistance[attempt]) {
					continue;   // keep the village away from the town
				}
			}
			float lo = 1e9f, hi = -1e9f;
			for (int k = 0; k <= 8; ++k) {
				const float a = k * 2.0f * detail::kPi / 8.0f;
				const float r = (k == 8) ? 0.0f : 50.0f;
				const float h = terrain.HeightAt(p.x + std::cos(a) * r, p.z + std::sin(a) * r);
				lo = std::min(lo, h);
				hi = std::max(hi, h);
			}
			if (lo < minGround[attempt]) {
				continue;   // beach or sea nearby
			}
			if (hi - lo < best) {
				best = hi - lo;
				centre = p;
				roadDistance = i * road.GetSpacing();
			}
		}
		if (best < 1e8f) {
			return true;
		}
	}
	return false;
}

inline void Generate(const RoadPath& road, const Terrain& terrain, bool hasCity, const Vector3& cityCentre,
                     std::vector<CityInstance>& pines, std::vector<CityInstance>& broadleaf,
                     std::vector<CityInstance>& houses, std::vector<CityInstance>& roofs) {
	using detail::Hash;
	using detail::Smoothstep;
	const detail::RoadGrid roadGrid(road);

	// ---- Village: houses on both sides of the road, 24 m apart, each on its own level of ground.
	Vector3 village;
	float   villageDistance = 0.0f;
	const bool hasVillage = FindVillageSite(road, terrain, hasCity, cityCentre, village, villageDistance);
	if (hasVillage) {
		std::cout << "Village: site at (" << village.x << ", " << village.z << ")\n";
		const float spacing = 24.0f;
		for (int k = -3; k <= 3; ++k) {
			for (int s = 0; s < 2; ++s) {
				if (Hash(k, s, 20) < 0.12f) {
					continue;   // a gap now and then
				}
				const float d = villageDistance + k * spacing + 6.0f * (Hash(k, s, 21) - 0.5f);
				const Vector3 p = road.PositionAt(d);
				Vector3 t = road.DirectionAt(d);
				t.y = 0.0f;
				const float tl = std::sqrt(t.x * t.x + t.z * t.z);
				t = t * (1.0f / tl);
				const Vector3 right(-t.z, 0.0f, t.x);
				const float   side = (s == 0) ? 1.0f : -1.0f;
				const float   offset = 15.0f + 5.0f * Hash(k, s, 22);
				const float   cx = p.x + right.x * side * offset, cz = p.z + right.z * side * offset;
				const float   fx = 8.0f + 4.0f * Hash(k, s, 23), fz = 7.0f + 3.0f * Hash(k, s, 24);

				// Level of the ground under the four corners: the house stands on the lowest one and
				// its walls reach up to the highest, so nothing hangs in the air on a slope.
				float lo = 1e9f, hi = -1e9f;
				for (int c = 0; c < 4; ++c) {
					const float ax = (c & 1) ? 0.5f * fx : -0.5f * fx, az = (c & 2) ? 0.5f * fz : -0.5f * fz;
					const float h = terrain.HeightAt(cx + t.x * ax + right.x * az, cz + t.z * ax + right.z * az);
					lo = std::min(lo, h);
					hi = std::max(hi, h);
				}
				if (lo < 5.5f || hi - lo > 4.5f) {
					continue;   // too wet or too steep
				}

				const float storeys = Hash(k, s, 25) < 0.35f ? 2.0f : 1.0f;
				const float base    = lo - 0.3f;
				const float wallTop = hi + storeys * 3.6f;
				const float seed    = Hash(k, s, 26);
				const float yaw     = std::atan2(-t.z, t.x);
				houses.push_back({ cx, base, cz, yaw, fx, wallTop - base, fz, 3.0f + seed });
				roofs.push_back({ cx, wallTop, cz, yaw, fx + 1.2f, 2.4f + Hash(k, s, 27), fz + 1.6f, 12.0f + seed * 0.99f });
			}
		}
	}

	// ---- Trees: a jittered grid over the island (one candidate per 10 m cell), thinned by rules.
	const float cell  = 10.0f;
	const float extent = 950.0f;
	const int   count = static_cast<int>(2.0f * extent / cell);
	for (int iz = 0; iz < count; ++iz) {
		for (int ix = 0; ix < count; ++ix) {
			const float x = -extent + (ix + Hash(ix, iz, 1)) * cell;
			const float z = -extent + (iz + Hash(ix, iz, 2)) * cell;

			const float h = terrain.HeightAt(x, z);
			if (h < 5.5f || h > 95.0f) {
				continue;   // not on the beach, not on the bare peaks
			}
			const float slope = std::fabs(terrain.HeightAt(x + 3.0f, z) - terrain.HeightAt(x - 3.0f, z))
			                  + std::fabs(terrain.HeightAt(x, z + 3.0f) - terrain.HeightAt(x, z - 3.0f));
			if (slope > 4.2f) {
				continue;   // steep (rock) ground
			}
			if (roadGrid.NearestSq(x, z) < 12.0f * 12.0f) {
				continue;   // keep the road and its lamps clear
			}
			if (hasCity) {
				const float dx = x - cityCentre.x, dz = z - cityCentre.z;
				const float keep = CityConst::kPlateauInner + 20.0f;
				if (dx * dx + dz * dz < keep * keep) {
					continue;   // no trees in town
				}
			}
			if (hasVillage) {
				const float dx = x - village.x, dz = z - village.z;
				if (dx * dx + dz * dz < 75.0f * 75.0f) {
					continue;   // the village keeps its own clearing
				}
			}

			// Forest patches: soft noise decides where trees crowd together; a few stand alone elsewhere.
			const float mask    = 0.65f * detail::Noise(x * 0.004f, z * 0.004f, 5) + 0.35f * detail::Noise(x * 0.011f, z * 0.011f, 6);
			const float density = (0.04f + 0.86f * Smoothstep(0.46f, 0.60f, mask)) * (1.0f - Smoothstep(70.0f, 95.0f, h));
			if (Hash(ix, iz, 3) > density) {
				continue;
			}

			// Lowlands get broadleaf trees, the hills get pines, with a mix in between.
			const bool  isBroadleaf = Hash(ix, iz, 4) > Smoothstep(20.0f, 60.0f, h) * 0.9f + 0.05f;
			const float seed = Hash(ix, iz, 7);
			const float yaw  = Hash(ix, iz, 8) * 6.2831853f;
			if (isBroadleaf) {
				const float height = 7.0f + 5.0f * Hash(ix, iz, 9);
				const float crown  = height * (0.85f + 0.3f * Hash(ix, iz, 10));
				broadleaf.push_back({ x, h - 0.3f, z, yaw, crown, height, crown, 2.0f + seed * 0.99f });
			}
			else {
				const float height = 9.0f + 7.0f * Hash(ix, iz, 9);
				const float crown  = height * (0.38f + 0.12f * Hash(ix, iz, 10));
				pines.push_back({ x, h - 0.3f, z, yaw, crown, height, crown, seed * 0.99f });
			}
		}
	}

	std::cout << "Outskirts: " << pines.size() << " pines, " << broadleaf.size() << " broadleaf trees, "
	          << houses.size() << " village houses" << (hasVillage ? "" : " (no village site found)") << "\n";
}

}   // namespace Outskirts
