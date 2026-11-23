#pragma once

#include "../nclgl/Extra/GLTFLoader.h"
#include <string>
#include <vector>

/*
A skinned glTF model that can be shared by many instances.

It wraps nclgl's GLTFLoader (mesh + per-sub-mesh materials + baked animation clips) and adds
what the renderer needs: a placement "fix" matrix and a function that turns (clip, time) into
the array of bone matrices the skinning shader expects.

The fix matrix converts the model's own axes to ours (Y up, facing -Z when unrotated) and scales it
to metres. Two helpers cover the usual glTF conventions.
*/
class AnimatedModel {
public:
	static const unsigned kMaxJoints = 128;   // must match MAX_JOINTS in skinning.glslh

	// Z-up models (CesiumMan): +X is the model's front. Y-up glTF models: +Z is the front.
	static Matrix4 ZUpFix(float scale);
	static Matrix4 YUpFix(float scale);

	bool Load(const std::string& gltfPath, const Matrix4& fix);

	Mesh*          GetMesh() const { return scene.meshes[meshIndex].get(); }
	const Matrix4& GetFix() const  { return fix; }
	int            SubMeshCount() const { return GetMesh()->GetSubMeshCount(); }
	const GLTFMaterialLayer& Layer(int subMesh) const { return scene.materials[meshIndex].allLayers[subMesh]; }

	unsigned ClipCount() const { return (unsigned)scene.animations.size(); }
	float    ClipDuration(unsigned clip) const;   // seconds

	// Bone matrices (animated pose x inverse bind pose) for 'seconds' into the clip, looping.
	// Frames are baked at 30 fps; neighbouring frames are blended for smooth motion at 60+ fps.
	void Pose(unsigned clip, float seconds, std::vector<Matrix4>& out) const;

private:
	GLTFScene scene;
	size_t    meshIndex = 0;   // the first mesh in the file that has a skeleton
	Matrix4   fix;
};
