#include "City.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
	const float kTwoPi = 6.28318530718f;

	// Deterministic hash.
	float Hash(int a, int b, int salt) {
		unsigned int n = static_cast<unsigned int>(a) * 374761393u + static_cast<unsigned int>(b) * 668265263u
		               + static_cast<unsigned int>(salt) * 2147483647u;
		n = (n ^ (n >> 13)) * 1274126177u;
		n ^= (n >> 16);
		return (n & 0xFFFFu) / 65536.0f;
	}

	float Smoothstep(float e0, float e1, float x) {
		float t = (x - e0) / (e1 - e0);
		t = std::min(std::max(t, 0.0f), 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
}

bool City::ChooseSite(const RoadPath& road, const std::function<float(float, float)>& groundHeight,
                      float radius, Vector3& centre, float& roadDistance) {
	const std::vector<Vector3>& pts = road.GetPoints();
	float best = 1e9f;
	bool  found = false;

	// Every 10th road sample (40 m): measure how bumpy the ground is in a ring around it.
	for (size_t i = 0; i < pts.size(); i += 10) {
		float lo = 1e9f, hi = -1e9f;
		for (int k = 0; k <= 8; ++k) {
			const float a = k * kTwoPi / 8.0f;
			const float r = (k == 8) ? 0.0f : radius;   // k == 8 is the centre itself
			const float h = groundHeight(pts[i].x + std::cos(a) * r, pts[i].z + std::sin(a) * r);
			lo = std::min(lo, h);
			hi = std::max(hi, h);
		}
		if (lo < 9.0f) {
			continue;   // part of the ring is beach or sea
		}
		if (hi - lo < best) {
			best = hi - lo;
			centre = pts[i];
			roadDistance = i * road.GetSpacing();
			found = true;
		}
	}
	if (found) {
		std::cout << "City: site at (" << centre.x << ", " << centre.z << "), ground varies by " << best << " m inside the plateau\n";
	}
	else {
		std::cout << "City: no suitable site found, the island has no city\n";
	}
	return found;
}

void City::Generate(const RoadPath& road, const Vector3& centre, float roadDistance) {
	instances.clear();
	lampHeads.clear();

	// Local axes at the road: u along the road, v to its right (same definition as RoadPath's ribbon).
	Vector3 u = road.DirectionAt(roadDistance);
	u.y = 0.0f;
	const float ul = std::sqrt(u.x * u.x + u.z * u.z);
	u = u * (1.0f / ul);
	const Vector3 v(-u.z, 0.0f, u.x);
	const float yaw = std::atan2(-u.z, u.x);   // turns a box's local +x onto u and +z onto v

	const std::vector<Vector3>& pts = road.GetPoints();
	auto distanceToRoad = [&](float x, float z) {
		float best = 1e9f;
		for (const Vector3& p : pts) {
			const float dx = p.x - x, dz = p.z - z;
			best = std::min(best, dx * dx + dz * dz);
		}
		return std::sqrt(best);
	};

	// --- Blocks: a grid of 46 m cells, aligned with the road. Cells on the road itself stay empty.
	const float pitch = 46.0f;
	const int   half  = 4;
	for (int ia = -half; ia <= half; ++ia) {
		for (int ib = -half; ib <= half; ++ib) {
			const float a = ia * pitch, b = ib * pitch;
			const float r = std::sqrt(a * a + b * b);
			if (r > CityConst::kCityRadius) {
				continue;
			}

			const float fx = 20.0f + 12.0f * Hash(ia, ib, 1);   // footprint along the road
			const float fz = 20.0f + 12.0f * Hash(ia, ib, 2);   // footprint across it
			const float px = centre.x + u.x * a + v.x * b;
			const float pz = centre.z + u.z * a + v.z * b;

			// Keep a street (road half-width 5 + pavement) clear around the road.
			const float clearance = 10.0f + 0.5f * std::sqrt(fx * fx + fz * fz);
			if (distanceToRoad(px, pz) < clearance) {
				continue;
			}

			// Tall in the middle, low at the edge, with per-building variation.
			const float centreHeight = 20.0f + 45.0f * (1.0f - Smoothstep(0.0f, CityConst::kCityRadius, r));
			// Whole storeys only (3.6 m each, matching the window grid in buildingFragment.glsl), so the
			// top row of windows is never cut short into non-square slits.
			const float wanted = std::max(10.0f, centreHeight * (0.55f + 0.9f * Hash(ia, ib, 3)));
			const float height = std::max(3.0f, std::floor(wanted / 3.6f + 0.5f)) * 3.6f;

			instances.push_back({ px, centre.y - 0.5f, pz, yaw, fx, height, fz, Hash(ia, ib, 4) });
		}
	}

	// --- Street lamps every 48 m along the whole road, alternating sides. Each lamp is a pole plus a head.
	const float lampSpacing = 48.0f;
	const int   lampCount   = static_cast<int>(road.GetLength() / lampSpacing);
	for (int i = 0; i < lampCount; ++i) {
		const float d = i * lampSpacing;
		const Vector3 p = road.PositionAt(d);
		Vector3 t = road.DirectionAt(d);
		t.y = 0.0f;
		const float tl = std::sqrt(t.x * t.x + t.z * t.z);
		t = t * (1.0f / tl);
		const Vector3 right(-t.z, 0.0f, t.x);
		const float   side = (i % 2 == 0) ? 7.0f : -7.0f;
		const float   lampYaw = std::atan2(-t.z, t.x);

		const float x = p.x + right.x * side;
		const float z = p.z + right.z * side;
		const float poleHeight = 8.0f;

		instances.push_back({ x, p.y - 0.5f, z, lampYaw, 0.3f, poleHeight, 0.3f, 10.0f });

		// The head is an arm reaching over the road: 1.3 m across, centred 0.55 m towards the centre line,
		// so its far end overlaps the pole (no gap between post and lamp).
		const float armOffset = 0.55f;
		const float hx = x - right.x * side * (armOffset / std::fabs(side));
		const float hz = z - right.z * side * (armOffset / std::fabs(side));
		instances.push_back({ hx, p.y - 0.5f + poleHeight - 0.3f, hz, lampYaw, 0.7f, 0.3f, 1.3f, 11.0f });
		lampHeads.push_back(Vector3(hx, p.y - 0.5f + poleHeight - 0.2f, hz));
	}

	std::cout << "City: " << instances.size() << " instances (buildings + lamp parts), " << lampHeads.size() << " street lamps\n";
}
