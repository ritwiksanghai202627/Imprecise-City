#include "AnimatedModel.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>

Matrix4 AnimatedModel::ZUpFix(float scale) {
	// raw x (front) -> -Z, raw y (left) -> -X, raw z (up) -> +Y: the same turn the tutorial applied to CesiumMan.
	return Matrix4::Rotation(90.0f, Vector3(0, 1, 0)) *
	       Matrix4::Rotation(270.0f, Vector3(1, 0, 0)) *
	       Matrix4::Scale(Vector3(scale, scale, scale));
}

Matrix4 AnimatedModel::YUpFix(float scale) {
	// glTF Y-up models face +Z; turn them to face -Z.
	return Matrix4::Rotation(180.0f, Vector3(0, 1, 0)) *
	       Matrix4::Scale(Vector3(scale, scale, scale));
}

bool AnimatedModel::Load(const std::string& gltfPath, const Matrix4& fixMatrix) {
	fix = fixMatrix;
	const auto started = std::chrono::steady_clock::now();

	if (!GLTFLoader::Load(gltfPath, scene)) {
		std::cout << "AnimatedModel: could not load " << gltfPath << "\n";
		return false;
	}

	// The loader only builds animations for the first skinned mesh, so use that one.
	bool found = false;
	for (size_t i = 0; i < scene.meshes.size(); ++i) {
		if (scene.meshes[i]->GetJointCount() > 0) {
			meshIndex = i;
			found = true;
			break;
		}
	}
	if (!found || scene.animations.empty()) {
		std::cout << "AnimatedModel: " << gltfPath << " has no skinned mesh or no animation\n";
		return false;
	}
	if (GetMesh()->GetJointCount() > kMaxJoints) {
		std::cout << "AnimatedModel: " << gltfPath << " has " << GetMesh()->GetJointCount()
		          << " joints, the shader supports " << kMaxJoints << "\n";
		return false;
	}

	// Report what was loaded, so model cost can be judged without opening a 3D tool.
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	std::cout << "AnimatedModel: " << gltfPath << "\n    "
	          << GetMesh()->GetTriCount() << " triangles, "
	          << GetMesh()->GetJointCount() << " joints, "
	          << GetMesh()->GetSubMeshCount() << " sub-meshes, "
	          << scene.textures.size() << " textures, "
	          << scene.animations.size() << " clip(s), loaded in " << ms << " ms\n";
	return true;
}

float AnimatedModel::ClipDuration(unsigned clip) const {
	const MeshAnimation& anim = *scene.animations[clip];
	return anim.GetFrameCount() / anim.GetFrameRate();
}

void AnimatedModel::Pose(unsigned clip, float seconds, std::vector<Matrix4>& out) const {
	const MeshAnimation& anim = *scene.animations[clip];
	const Mesh&          mesh = *GetMesh();

	const unsigned frames     = anim.GetFrameCount();
	const unsigned jointCount = std::min(mesh.GetJointCount(), anim.GetJointCount());

	// Position in the clip in frames, wrapped so the clip loops.
	float f = seconds * anim.GetFrameRate();
	f -= std::floor(f / frames) * frames;
	unsigned frameA = std::min((unsigned)f, frames - 1);
	const unsigned frameB = (frameA + 1) % frames;
	const float blend = f - (float)frameA;

	const Matrix4* a       = anim.GetJointData(frameA);
	const Matrix4* b       = anim.GetJointData(frameB);
	const Matrix4* invBind = mesh.GetInverseBindPose();

	out.resize(jointCount);
	for (unsigned j = 0; j < jointCount; ++j) {
		// Skinning is linear, so blending the two frames' matrices is a valid in-between pose.
		Matrix4 m;
		for (int k = 0; k < 16; ++k) {
			m.values[k] = a[j].values[k] + (b[j].values[k] - a[j].values[k]) * blend;
		}
		out[j] = m * invBind[j];
	}
}
