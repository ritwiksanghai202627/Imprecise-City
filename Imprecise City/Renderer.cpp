#include "Renderer.h"
#include "RoadPath.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
	const float kRadToDeg = 57.2957795f;

	// Cinematic camera tuning
	const float kCarSpeed      = 20.0f;   // metres per second
	const float kCamBehind     = 16.0f;
	const float kCamHeight     = 4.5f;
	const float kLookAhead     = 12.0f;
	const float kLookHeight    = 1.5f;

	// nclgl convention: yaw 0 looks down -Z, positive pitch looks up.
	float YawFromDirection(const Vector3& f) {
		return std::atan2(-f.x, -f.z) * kRadToDeg;
	}
	float PitchFromDirection(const Vector3& f) {
		return std::asin(std::max(-1.0f, std::min(1.0f, f.y))) * kRadToDeg;
	}
}

Renderer::Renderer(Window& parent) : OGLRenderer(parent) {
	const auto startTime = std::chrono::steady_clock::now();

	// World: the road needs the natural terrain height, then the terrain is carved around the road.
	const std::vector<Vector3> roadControlPoints = {
		Vector3(-480, 0,  340), Vector3(-330, 0,  250), Vector3(-370, 0,   90),
		Vector3(-210, 0,  -10), Vector3( -40, 0,  120), Vector3( 130, 0,   40),
		Vector3( 200, 0, -140), Vector3(  60, 0, -260), Vector3( -90, 0, -330)
	};
	road = std::make_unique<RoadPath>(roadControlPoints, 4.0f);
	road->SnapToTerrain(&Terrain::IslandHeight, 10.0f, 8, 10);
	terrain = std::make_unique<Terrain>(*road);
	roadMesh.reset(road->BuildRibbon(5.0f, 0.15f, 8.0f));

	carMesh.reset(Mesh::LoadFromMeshFile("Cube.msh"));
	seaMesh.reset(Mesh::GenerateQuad());

	terrainShader = std::make_unique<Shader>("sceneVertex.glsl", "terrainFragment.glsl");
	roadShader    = std::make_unique<Shader>("sceneVertex.glsl", "roadFragment.glsl");
	seaShader     = std::make_unique<Shader>("sceneVertex.glsl", "seaFragment.glsl");
	litShader     = std::make_unique<Shader>("sceneVertex.glsl", "litFragment.glsl");

	grassTexture = LoadTexture("grass.png");
	sandTexture  = LoadTexture("sand.png");
	rockTexture  = LoadTexture("rock.png");

	if (!carMesh || !terrainShader->LoadSuccess() || !roadShader->LoadSuccess() ||
	    !seaShader->LoadSuccess() || !litShader->LoadSuccess() ||
	    !grassTexture || !sandTexture || !rockTexture) {
		std::cout << "Renderer: resource loading failed, see messages above.\n";
		return;   // init stays false, so main() exits cleanly
	}

	// Frame uniform buffer, bound once to binding point 0 (matches frame.glslh).
	glGenBuffers(1, &frameUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, frameUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameData), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUBO);

	// Constant "midday" environment for now; time of day and seasons will drive these later.
	Vector3 sun(0.45f, 0.70f, 0.55f);
	const float sunLen = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
	sun = sun * (1.0f / sunLen);
	frameData.sunDir    = Vector4(sun.x, sun.y, sun.z, 1.0f);
	frameData.sunColour = Vector4(1.0f, 0.95f, 0.85f, 1.0f);
	frameData.fogColour = Vector4(0.62f, 0.75f, 0.90f, 0.00045f);

	glEnable(GL_DEPTH_TEST);
	glClearColor(0.62f, 0.75f, 0.90f, 1.0f);

	lastTick = std::chrono::steady_clock::now();
	init = true;

	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(lastTick - startTime).count();
	std::cout << "Renderer ready in " << ms << " ms\n";
}

Renderer::~Renderer() {
	glDeleteTextures(1, &grassTexture);
	glDeleteTextures(1, &sandTexture);
	glDeleteTextures(1, &rockTexture);
	glDeleteBuffers(1, &frameUBO);
}

GLuint Renderer::LoadTexture(const std::string& fileName) {
	const std::string path = std::string(TEXTUREDIR) + fileName;
	GLuint tex = SOIL_load_OGL_texture(path.c_str(), SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS);
	if (!tex) {
		std::cout << "Renderer: could not load texture " << path << "\n";
		return 0;
	}
	SetTextureRepeating(tex, true);
	return tex;
}

void Renderer::Use(Shader& shader) {
	BindShader(&shader);
	activeProgram = shader.GetProgram();
}

void Renderer::SetModelMatrix(const Matrix4& m) {
	glUniformMatrix4fv(glGetUniformLocation(activeProgram, "modelMatrix"), 1, false, m.values);
}

void Renderer::UpdateScene(float /*dt*/) {
	const auto now = std::chrono::steady_clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - lastTick).count(), 0.1f);
	lastTick = now;
	elapsedSeconds += dt;

	if (Window::GetKeyboard()->KeyTriggered(KEYBOARD_C)) {
		SwitchCameraMode();
	}

	if (cameraMode == CameraMode::Cinematic) {
		UpdateCinematicCamera(dt);
	}
	else {
		UpdateFreeCamera(dt);
	}

	projMatrix = Matrix4::Perspective(1.0f, 6000.0f, (float)width / (float)height, 60.0f);
}

void Renderer::UpdateCinematicCamera(float dt) {
	carDistance += kCarSpeed * dt;
	if (carDistance > road->GetLength() - 30.0f) {
		carDistance = 0.0f;   // loop for now
	}

	eyePosition = road->PositionAt(carDistance - kCamBehind) + Vector3(0.0f, kCamHeight, 0.0f);
	lookTarget  = road->PositionAt(carDistance + kLookAhead) + Vector3(0.0f, kLookHeight, 0.0f);
	viewMatrix  = Matrix4::BuildViewMatrix(eyePosition, lookTarget);
}

void Renderer::UpdateFreeCamera(float dt) {
	camera.UpdateCamera(dt * 1000.0f);   // nclgl camera takes milliseconds

	// Stay above the ground and the sea.
	Vector3 p = camera.GetPosition();
	const float minY = std::max(terrain->HeightAt(p.x, p.z), 0.0f) + 2.0f;
	if (p.y < minY) {
		p.y = minY;
		camera.SetPosition(p);
	}

	eyePosition = camera.GetPosition();
	viewMatrix  = camera.BuildViewMatrix();
}

void Renderer::SwitchCameraMode() {
	if (cameraMode == CameraMode::Cinematic) {
		// Hand the current cinematic pose to the free camera so there is no jump.
		Vector3 f = lookTarget - eyePosition;
		const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
		f = f * (1.0f / len);
		camera.SetPosition(eyePosition);
		camera.SetYaw(YawFromDirection(f));
		camera.SetPitch(PitchFromDirection(f));
		cameraMode = CameraMode::Free;
		std::cout << "Camera: free\n";
	}
	else {
		cameraMode = CameraMode::Cinematic;
		std::cout << "Camera: cinematic\n";
	}
}

void Renderer::UploadFrameData() {
	frameData.viewMatrix = viewMatrix;
	frameData.projMatrix = projMatrix;
	frameData.cameraPos  = Vector4(eyePosition.x, eyePosition.y, eyePosition.z, 1.0f);
	frameData.timeInfo   = Vector4(elapsedSeconds, 0.0f, 0.0f, 0.0f);

	glBindBuffer(GL_UNIFORM_BUFFER, frameUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameData), &frameData);
}

void Renderer::RenderScene() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	UploadFrameData();

	StartDebugGroup("Terrain");
	Use(*terrainShader);
	SetModelMatrix(Matrix4());
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, grassTexture);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, sandTexture);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, rockTexture);
	terrain->GetMesh()->Draw();
	EndDebugGroup();

	StartDebugGroup("Road");
	Use(*roadShader);
	SetModelMatrix(Matrix4());
	glEnable(GL_POLYGON_OFFSET_FILL);   // pull the road slightly toward the camera so it never z-fights the terrain
	glPolygonOffset(-1.0f, -1.0f);
	roadMesh->Draw();
	glDisable(GL_POLYGON_OFFSET_FILL);
	EndDebugGroup();

	StartDebugGroup("Vehicle");
	{
		Use(*litShader);
		const Vector3 pos = road->PositionAt(carDistance);
		const Vector3 dir = road->DirectionAt(carDistance);
		// Assumes Cube.msh spans -1..1, so this is a 2 x 1.2 x 4.4 box.
		const Matrix4 model =
			Matrix4::Translation(pos + Vector3(0.0f, 0.65f, 0.0f)) *
			Matrix4::Rotation(YawFromDirection(dir), Vector3(0, 1, 0)) *
			Matrix4::Rotation(PitchFromDirection(dir), Vector3(1, 0, 0)) *
			Matrix4::Scale(Vector3(1.0f, 0.6f, 2.2f));
		SetModelMatrix(model);
		glUniform4f(glGetUniformLocation(activeProgram, "baseColour"), 0.75f, 0.08f, 0.08f, 1.0f);
		carMesh->Draw();
	}
	EndDebugGroup();

	StartDebugGroup("Sea");   // last, so it can become transparent later
	Use(*seaShader);
	SetModelMatrix(Matrix4::Rotation(-90.0f, Vector3(1, 0, 0)) * Matrix4::Scale(Vector3(8000.0f, 8000.0f, 1.0f)));
	seaMesh->Draw();
	EndDebugGroup();
}
