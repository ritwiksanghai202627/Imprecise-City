#pragma once

#include "../nclgl/Vector3.h"

#include <functional>
#include <vector>

class ProceduralMesh;

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
	// Only x and z of the control points are used; heights come from SnapToTerrain.
	RoadPath(const std::vector<Vector3>& controlPoints, float sampleSpacing = 4.0f);

	// Sets each sample's height from the terrain, keeps it above minHeight, then smooths
	// it so the road has gentle gradients instead of following every bump.
	void SnapToTerrain(const std::function<float(float, float)>& baseHeight,
	                   float minHeight, int smoothingPasses, int smoothingRadius);

	float   GetLength() const  { return (points.size() - 1) * spacing; }
	float   GetSpacing() const { return spacing; }
	const std::vector<Vector3>& GetPoints() const { return points; }

	Vector3 PositionAt(float distance) const;    // clamped to [0, length]
	Vector3 DirectionAt(float distance) const;   // unit tangent

	// Flat ribbon of asphalt. Texture coords: x = 0..1 across, y = distance / vTileLength.
	ProceduralMesh* BuildRibbon(float halfWidth, float heightOffset, float vTileLength) const;

private:
	std::vector<Vector3> points;
	float spacing;
};
