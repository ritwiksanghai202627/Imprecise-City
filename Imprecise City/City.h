#pragma once

#include "RoadPath.h"

#include <functional>
#include <vector>

// One building or lamp-post part. 32 bytes, laid out exactly like struct Instance in instance.glslh
// (two vec4s), so the array can be uploaded to a shader storage buffer as it is.
struct CityInstance {
	float px, py, pz, yaw;    // base-centre position, rotation about Y (radians)
	float sx, sy, sz, style;  // box size in metres; style: 0..1 = building seed, 10 = lamp pole, 11 = lamp head
};
static_assert(sizeof(CityInstance) == 32, "CityInstance must match struct Instance in instance.glslh");

namespace CityConst {
	const float kPlateauInner = 170.0f;   // terrain is dead flat inside this radius
	const float kPlateauOuter = 255.0f;   // ...and blends back to the natural hills by here
	const float kCityRadius   = 160.0f;   // buildings stand inside this radius
}

/*
The city: a block grid aligned with the road where it passes the site, plus street lamps along the
whole road. This class only makes DATA (instance lists and lamp positions); the Renderer uploads
the instances to the GPU and draws them instanced.
*/
class City {
public:
	// Looks along the road for the flattest piece of land big enough for the city.
	// On success: 'centre' is a road sample (so the road passes through the middle of town) and
	// 'roadDistance' its distance along the road.
	static bool ChooseSite(const RoadPath& road, const std::function<float(float, float)>& groundHeight,
	                       float radius, Vector3& centre, float& roadDistance);

	void Generate(const RoadPath& road, const Vector3& centre, float roadDistance);

	const std::vector<CityInstance>& GetInstances() const { return instances; }
	const std::vector<Vector3>&      GetLampHeads() const { return lampHeads; }   // where the lamp lights are

private:
	std::vector<CityInstance> instances;
	std::vector<Vector3>      lampHeads;
};
