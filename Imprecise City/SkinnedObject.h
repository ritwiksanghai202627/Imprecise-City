#pragma once

#include "SceneObject.h"
#include "AnimatedModel.h"

/*
One instance of an AnimatedModel in the scene graph. The model (mesh, textures, clips) is shared;
each instance only owns its playback time and its bone matrices.

The pose is computed lazily, the first time the renderer asks for it in a frame, so characters
that are culled cost nothing beyond advancing a float.
*/
class SkinnedObject : public SceneObject {
public:
	SkinnedObject(const AnimatedModel* model, unsigned clip, float startTime, float playRate)
		: SceneObject(model->GetMesh(), Material::Skinned), model(model), clip(clip), time(startTime), rate(playRate) {
		SetBoundingRadius(2.5f);   // generous sphere around the feet-origin; characters are < 2 m tall
	}

	void Update(float dt) override {
		SceneNode::Update(dt);   // world transform and children
		time += dt * rate;
		poseValid = false;
	}

	const AnimatedModel& GetModel() const { return *model; }

	const std::vector<Matrix4>& GetPose() {
		if (!poseValid) {
			model->Pose(clip, time, pose);
			poseValid = true;
		}
		return pose;
	}

private:
	const AnimatedModel* model;
	unsigned             clip;
	float                time;
	float                rate;
	bool                 poseValid = false;
	std::vector<Matrix4> pose;
};
