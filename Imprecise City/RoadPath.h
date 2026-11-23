#pragma once

#include "../nclgl/Vector3.h"

#include <functional>
#include <memory>
#include <vector>

#include "ProceduralMesh.h"

/*
The road centreline. One path, three jobs:
  1. the terrain is levelled around it (Terrain)
  2. the asphalt ribbon is extruded along it (BuildRibbon)
  3. the car and the cinematic camera travel along it (PositionAt / DirectionAt)

Control points are joined with a Catmull-Rom spline, then resampled so the samples are
evenly spaced. Even spacing means "distance along the road" maps straight to an array
index, so PositionAt is a cheap lerp with no searching.
*/
class RoadPath {
public:
	RoadPath(const std::vector<Vector3>& controlPoints, float sampleSpacing = 4.0f, bool closed = false);

	void SnapToTerrain(const std::function<float(float, float)>& baseHeight,
	                   float minHeight, int smoothingPasses, int smoothingRadius);

	void FlattenAround(const Vector3& centre, float innerRadius, float outerRadius, float height);

	float   GetLength() const  { return (closed ? points.size() : points.size() - 1) * spacing; }
	bool    IsClosed() const   { return closed; }
	float   GetSpacing() const { return spacing; }
	const std::vector<Vector3>& GetPoints() const { return points; }

	Vector3 PositionAt(float distance) const;    // open road: clamped to [0, length]; closed road: wraps
	Vector3 DirectionAt(float distance) const;   // unit tangent

	struct RibbonSegment {
		std::unique_ptr<ProceduralMesh> mesh;
		Vector3 centre;
		float   radius = 0.0f;
	};

	std::vector<RibbonSegment> BuildRibbonSegments(float halfWidth, float heightOffset, float vTileLength,
	                                               int samplesPerSegment) const;

private:
	std::vector<Vector3> points;
	float spacing;
	bool  closed;
};
