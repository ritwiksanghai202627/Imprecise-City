#pragma once

#include "SceneObject.h"
#include "ProceduralMesh.h"

/*
A group of city instances (buildings / lamp parts) in one area, drawn with ONE instanced draw call.
Every district is its own scene node with its own bounding sphere, so districts outside the view
(or outside the sun's shadow box) are culled exactly like terrain chunks.

The instance data lives in this district's shader storage buffer; Draw() binds it to binding 0,
where instance.glslh expects it.
*/
class CityDistrict : public SceneObject {
public:
	// 'mesh' is drawn 'instanceCount' times. Buildings use the unit box; trees and roofs pass their own
	// mesh and material (Material::Tree for trees, Material::Building for roofs).
	CityDistrict(ProceduralMesh* mesh, GLuint instanceBuffer, int instanceCount, Material material = Material::Building)
		: SceneObject(mesh, material), box(mesh), buffer(instanceBuffer), count(instanceCount) {}

	void Draw(const OGLRenderer& /*renderer*/) override {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
		box->DrawInstanced(count);
	}

	int InstanceCount() const { return count; }

private:
	ProceduralMesh* box;
	GLuint          buffer;
	int             count;
};
