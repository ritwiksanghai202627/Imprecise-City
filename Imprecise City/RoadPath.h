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
	// Only x and z of the control points are used; heights come from SnapToTerrain.
	// closed = true joins the last point back to the first, so distance wraps around (no jump).
	RoadPath(const std::vector<Vector3>& controlPoints, float sampleSpacing = 4.0f, bool closed = false);

	// Sets each sample's height from the terrain, keeps it above minHeight, then smooths
	// it so the road has gentle gradients instead of following every bump.
	void SnapToTerrain(const std::function<float(float, float)>& baseHeight,
	                   float minHeight, int smoothingPasses, int smoothingRadius);

	// Levels the road toward 'height' near (centre.x, centre.z): full strength inside innerRadius,
	// fading out by outerRadius. Used for the city plateau; call before the terrain is built.
	void FlattenAround(const Vector3& centre, float innerRadius, float outerRadius, float height);

	float   GetLength() const  { return (closed ? points.size() : points.size() - 1) * spacing; }
	bool    IsClosed() const   { return closed; }
	float   GetSpacing() const { return spacing; }
	const std::vector<Vector3>& GetPoints() const { return points; }

	Vector3 PositionAt(float distance) const;    // open road: clamped to [0, length]; closed road: wraps
	Vector3 DirectionAt(float distance) const;   // unit tangent

	// One piece of the road: its own mesh (vertices relative to 'centre') and bounding sphere,
	// so the scene graph can frustum-cull the road piece by piece instead of all or nothing.
	struct RibbonSegment {
		std::unique_ptr<ProceduralMesh> mesh;
		Vector3 centre;
		float   radius = 0.0f;
	};

	// Flat ribbon of asphalt cut into pieces of 'samplesPerSegment' road samples.
	// Texture coords: x = 0..1 across, y = distance along the road / vTileLength (continuous across pieces).
	std::vector<RibbonSegment> BuildRibbonSegments(float halfWidth, float heightOffset, float vTileLength,
	                                               int samplesPerSegment) const;

private:
	std::vector<Vector3> points;
	float spacing;
	bool  closed;
};
