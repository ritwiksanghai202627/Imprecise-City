#pragma once

#include "../nclgl/SceneNode.h"

// Which shader/material a drawable node uses. Opaque nodes sort by this first, then by distance.
// Buildings come before the ground so early-z rejects the terrain they cover.
enum class Material { Building, Tree, Road, Lit, Car, Skinned, Terrain, Sea };

/*
A SceneNode that knows how it should be drawn. nclgl's SceneNode only holds a mesh, a
colour and a texture, so this adds the material, the opaque/transparent flag and whether
the node casts shadows.

Invariant used by Renderer::CullNodes: every node that has a mesh is a SceneObject
(plain SceneNodes are mesh-less grouping nodes only).
*/
class SceneObject : public SceneNode {
public:
	SceneObject(Mesh* mesh, Material material, Vector4 colour = Vector4(1, 1, 1, 1), bool transparent = false)
		: SceneNode(mesh, colour), material(material), transparent(transparent) {}

	Material GetMaterial() const { return material; }
	bool     IsTransparent() const { return transparent; }

	bool CastsShadow() const { return castsShadow; }
	void SetCastsShadow(bool value) { castsShadow = value; }

private:
	Material material;
	bool     transparent;
	bool     castsShadow = true;
};
