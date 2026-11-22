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

RoadPath::RoadPath(const std::vector<Vector3>& controlPoints, float sampleSpacing, bool isClosed)
	: spacing(sampleSpacing), closed(isClosed) {
	assert(controlPoints.size() >= 3);

	// Step 1: sample the spline densely (64 samples per segment). A closed road has one more
	// segment (last -> first) and the control point indices wrap around.
	std::vector<Vector3> dense;
	const int    stepsPerSegment = 64;
	const int    count           = static_cast<int>(controlPoints.size());
	const int    segments        = closed ? count : count - 1;

	auto control = [&](int i) -> const Vector3& {
		return closed ? controlPoints[((i % count) + count) % count]
		              : controlPoints[std::min(std::max(i, 0), count - 1)];
	};

	for (int i = 0; i < segments; ++i) {
		for (int s = 0; s < stepsPerSegment; ++s) {
			dense.push_back(CatmullRom(control(i - 1), control(i), control(i + 1), control(i + 2), s / float(stepsPerSegment)));
		}
	}
	dense.push_back(closed ? control(0) : control(count - 1));   // closes the loop / ends the road
	for (Vector3& p : dense) {
		p.y = 0.0f;
	}

	// Step 2: cumulative arc length along the dense polyline.
	std::vector<float> arc(dense.size(), 0.0f);
	for (size_t i = 1; i < dense.size(); ++i) {
		arc[i] = arc[i - 1] + Length(dense[i] - dense[i - 1]);
	}
	const float total = arc.back();

	// Step 3: pick evenly spaced samples by arc length. For a closed road the spacing is nudged
	// so a whole number of samples fits the loop exactly (the seam is then as even as the rest).
	int sampleCount = static_cast<int>(total / spacing);
	if (closed) {
		sampleCount = std::max(sampleCount, 3);
		spacing     = total / sampleCount;
	}
	else {
		sampleCount += 1;   // include the start point
	}

	size_t seg = 0;
	for (int k = 0; k < sampleCount; ++k) {
		const float s = k * spacing;
		while (seg + 2 < dense.size() && arc[seg + 1] < s) {
			++seg;
		}
		const float span = arc[seg + 1] - arc[seg];
		const float t    = span > 1e-5f ? (s - arc[seg]) / span : 0.0f;
		points.push_back(dense[seg] + (dense[seg + 1] - dense[seg]) * t);
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
				// Closed: wrap around so the seam is smoothed too. Open: repeat the end values.
				const int clamped = closed ? ((j % n) + n) % n : std::min(std::max(j, 0), n - 1);
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
	const int n = static_cast<int>(points.size());

	if (closed) {
		// Wrap the distance into [0, length), then lerp between neighbours (the last one links to the first).
		const float length = GetLength();
		distance -= std::floor(distance / length) * length;
		const float f = distance / spacing;
		const int   i = std::min(static_cast<int>(f), n - 1);
		const float t = f - static_cast<float>(i);
		return points[i] + (points[(i + 1) % n] - points[i]) * t;
	}

	const float maxIndex = static_cast<float>(n - 1);
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

std::vector<RoadPath::RibbonSegment> RoadPath::BuildRibbonSegments(float halfWidth, float heightOffset,
                                                                    float vTileLength, int samplesPerSegment) const {
	std::vector<RibbonSegment> result;
	const int pointCount = static_cast<int>(points.size());
	const int quadCount  = closed ? pointCount : pointCount - 1;   // road steps to cover

	for (int first = 0; first < quadCount; first += samplesPerSegment) {
		const int last = std::min(first + samplesPerSegment, quadCount);   // this piece covers steps first..last

		// Centre of the piece (average of its samples); vertices are stored relative to it.
		Vector3 centre(0, 0, 0);
		for (int i = first; i <= last; ++i) {
			centre = centre + points[i % pointCount];
		}
		centre = centre * (1.0f / (last - first + 1));
		centre.y += heightOffset;

		std::vector<Vector3>      positions;
		std::vector<Vector2>      texCoords;
		std::vector<unsigned int> indices;
		float radius = 0.0f;

		for (int i = first; i <= last; ++i) {
			const Vector3 t = DirectionAt(i * spacing);

			// 'Right' of the direction of travel, kept horizontal so the road never banks.
			// This is cross(forward, up) with up = (0,1,0), written out.
			float hx = t.x, hz = t.z;
			const float hl = std::sqrt(hx * hx + hz * hz);
			if (hl > 1e-5f) { hx /= hl; hz /= hl; }
			const Vector3 right(-hz, 0.0f, hx);

			Vector3 middle = points[i % pointCount];
			middle.y += heightOffset;

			const Vector3 left  = middle - right * halfWidth - centre;   // left edge  (index 2k)
			const Vector3 rightP = middle + right * halfWidth - centre;  // right edge (index 2k + 1)
			positions.push_back(left);
			positions.push_back(rightP);
			radius = std::max(radius, std::max(Length(left), Length(rightP)));

			const float v = (i * spacing) / vTileLength;
			texCoords.push_back(Vector2(0.0f, v));
			texCoords.push_back(Vector2(1.0f, v));
		}

		// Two triangles per step, wound so the front face points up.
		for (unsigned int k = 0; k + 1 < positions.size() / 2; ++k) {
			const unsigned int l0 = 2 * k, r0 = 2 * k + 1, l1 = 2 * k + 2, r1 = 2 * k + 3;
			indices.push_back(l0); indices.push_back(r0); indices.push_back(l1);
			indices.push_back(r0); indices.push_back(r1); indices.push_back(l1);
		}

		RibbonSegment segment;
		segment.mesh.reset(new ProceduralMesh(positions, texCoords, indices));
		segment.centre = centre;
		segment.radius = radius;
		result.push_back(std::move(segment));
	}
	return result;
}
