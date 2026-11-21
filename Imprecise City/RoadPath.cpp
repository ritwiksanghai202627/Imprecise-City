#include "RoadPath.h"
#include "ProceduralMesh.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
	float Length(const Vector3& v) {
		return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	}

	// Uniform Catmull-Rom: returns p1 at t = 0 and p2 at t = 1, and the curve is smooth
	// across segment joins because p0 and p3 steer the tangents.
	Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
		const float t2 = t * t;
		const float t3 = t2 * t;
		return (p1 * 2.0f
		      + (p2 - p0) * t
		      + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
		      + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
	}
}

RoadPath::RoadPath(const std::vector<Vector3>& controlPoints, float sampleSpacing)
	: spacing(sampleSpacing) {
	assert(controlPoints.size() >= 2);

	// Step 1: sample the spline densely (64 samples per segment).
	std::vector<Vector3> dense;
	const int    stepsPerSegment = 64;
	const size_t last            = controlPoints.size() - 1;

	for (size_t i = 0; i < last; ++i) {
		const Vector3& p0 = controlPoints[i == 0 ? 0 : i - 1];
		const Vector3& p1 = controlPoints[i];
		const Vector3& p2 = controlPoints[i + 1];
		const Vector3& p3 = controlPoints[i + 2 > last ? last : i + 2];

		for (int s = 0; s < stepsPerSegment; ++s) {
			dense.push_back(CatmullRom(p0, p1, p2, p3, s / float(stepsPerSegment)));
		}
	}
	dense.push_back(controlPoints[last]);

	// Step 2: walk along the dense polyline and drop a point every 'spacing' units.
	Vector3 previous = dense.front();
	previous.y = 0.0f;
	points.push_back(previous);

	for (size_t i = 1; i < dense.size(); ++i) {
		Vector3 target = dense[i];
		target.y = 0.0f;

		float d = Length(target - previous);
		while (d >= spacing) {
			previous = previous + (target - previous) * (spacing / d);
			points.push_back(previous);
			d = Length(target - previous);
		}
	}
}

void RoadPath::SnapToTerrain(const std::function<float(float, float)>& baseHeight,
                             float minHeight, int smoothingPasses, int smoothingRadius) {
	for (Vector3& p : points) {
		p.y = std::max(baseHeight(p.x, p.z), minHeight);
	}

	// Moving-average smoothing along the road. Each pass blends a point with its
	// neighbours; several passes approximate a Gaussian blur of the height profile.
	const int n = static_cast<int>(points.size());
	std::vector<float> smoothed(n);

	for (int pass = 0; pass < smoothingPasses; ++pass) {
		for (int i = 0; i < n; ++i) {
			float sum = 0.0f;
			int   count = 0;
			for (int j = i - smoothingRadius; j <= i + smoothingRadius; ++j) {
				const int clamped = std::min(std::max(j, 0), n - 1);   // repeat the end values
				sum += points[clamped].y;
				++count;
			}
			smoothed[i] = sum / count;
		}
		for (int i = 0; i < n; ++i) {
			points[i].y = smoothed[i];
		}
	}
}

Vector3 RoadPath::PositionAt(float distance) const {
	const float maxIndex = static_cast<float>(points.size() - 1);
	const float f = std::min(std::max(distance / spacing, 0.0f), maxIndex);
	const size_t i = static_cast<size_t>(f);

	if (i >= points.size() - 1) {
		return points.back();
	}
	const float t = f - static_cast<float>(i);
	return points[i] + (points[i + 1] - points[i]) * t;
}

Vector3 RoadPath::DirectionAt(float distance) const {
	// Central difference one sample either side; PositionAt clamps at the ends.
	const Vector3 d = PositionAt(distance + spacing) - PositionAt(distance - spacing);
	const float len = Length(d);
	return len > 1e-5f ? d * (1.0f / len) : Vector3(0.0f, 0.0f, -1.0f);
}

ProceduralMesh* RoadPath::BuildRibbon(float halfWidth, float heightOffset, float vTileLength) const {
	std::vector<Vector3>      positions;
	std::vector<Vector2>      texCoords;
	std::vector<unsigned int> indices;

	const unsigned int n = static_cast<unsigned int>(points.size());
	positions.reserve(n * 2);
	texCoords.reserve(n * 2);
	indices.reserve((n - 1) * 6);

	for (unsigned int i = 0; i < n; ++i) {
		const Vector3 t = DirectionAt(i * spacing);

		// 'Right' of the direction of travel, kept horizontal so the road never banks.
		// This is cross(forward, up) with up = (0,1,0), written out.
		float hx = t.x, hz = t.z;
		const float hl = std::sqrt(hx * hx + hz * hz);
		if (hl > 1e-5f) { hx /= hl; hz /= hl; }
		const Vector3 right(-hz, 0.0f, hx);

		Vector3 centre = points[i];
		centre.y += heightOffset;

		positions.push_back(centre - right * halfWidth);   // left edge  (index 2i)
		positions.push_back(centre + right * halfWidth);   // right edge (index 2i + 1)

		const float v = (i * spacing) / vTileLength;
		texCoords.push_back(Vector2(0.0f, v));
		texCoords.push_back(Vector2(1.0f, v));
	}

	// Two triangles per step, wound so the front face points up.
	for (unsigned int i = 0; i + 1 < n; ++i) {
		const unsigned int l0 = 2 * i, r0 = 2 * i + 1, l1 = 2 * i + 2, r1 = 2 * i + 3;
		indices.push_back(l0); indices.push_back(r0); indices.push_back(l1);
		indices.push_back(r0); indices.push_back(r1); indices.push_back(l1);
	}

	return new ProceduralMesh(positions, texCoords, indices);
}
