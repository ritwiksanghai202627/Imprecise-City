#pragma once

#include "../nclgl/OGLRenderer.h"
#include "../nclgl/Camera.h"

#include <chrono>
#include <memory>
#include <string>

class Terrain;
class RoadPath;

// CPU mirror of the FrameData uniform block in Shaders/frame.glslh (std140).
// Field order and types must match the GLSL block exactly.
struct FrameData {
	Matrix4 viewMatrix;
	Matrix4 projMatrix;
	Vector4 cameraPos;
	Vector4 sunDir;
	Vector4 sunColour;
	Vector4 fogColour;
	Vector4 timeInfo;
};
static_assert(sizeof(Matrix4) == 64, "FrameData assumes Matrix4 is 16 floats");
static_assert(sizeof(Vector4) == 16, "FrameData assumes Vector4 is 4 floats");
static_assert(sizeof(FrameData) == 2 * 64 + 5 * 16, "FrameData must match the std140 block in frame.glslh");

class Renderer : public OGLRenderer {
public:
	Renderer(Window& parent);
	~Renderer() override;

	void UpdateScene(float dt) override;   // dt ignored: we time ourselves so we can add slow-mo later
	void RenderScene() override;

private:
	enum class CameraMode { Cinematic, Free };

	void   UpdateCinematicCamera(float dt);
	void   UpdateFreeCamera(float dt);
	void   SwitchCameraMode();
	void   UploadFrameData();
	GLuint LoadTexture(const std::string& fileName);

	// Shader helpers. Matrices shared by every draw live in the frame UBO, so per draw
	// we only set the model matrix.
	void Use(Shader& shader);
	void SetModelMatrix(const Matrix4& m);

	// Camera
	Camera     camera;               // nclgl free-fly camera
	CameraMode cameraMode = CameraMode::Cinematic;
	Vector3    eyePosition;
	Vector3    lookTarget;

	// World
	std::unique_ptr<RoadPath> road;
	std::unique_ptr<Terrain>  terrain;
	std::unique_ptr<Mesh>     roadMesh;
	std::unique_ptr<Mesh>     carMesh;
	std::unique_ptr<Mesh>     seaMesh;
	float carDistance = 0.0f;

	// Shaders and textures
	std::unique_ptr<Shader> terrainShader, roadShader, seaShader, litShader;
	GLuint grassTexture = 0, sandTexture = 0, rockTexture = 0;
	GLuint activeProgram = 0;

	// Frame data
	GLuint frameUBO = 0;
	FrameData frameData;

	// Time
	std::chrono::steady_clock::time_point lastTick;
	float elapsedSeconds = 0.0f;
};
