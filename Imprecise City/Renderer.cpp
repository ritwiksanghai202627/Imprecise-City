#include "Renderer.h"
#include "RoadPath.h"
#include "Terrain.h"
#include "ProceduralMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

	// Shadow map tuning
	const int   kShadowMapSize      = 2048;
	const float kShadowLightDistance = 800.0f;   // how far from the focus point the sun camera sits
	const float kShadowDepthBias    = 0.0001f;   // in 0..1 depth units (range is 2 x light distance)

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

	// Placeholder car body: a smooth rounded box (so reflections look right), 2 x 1.1 x 4.4 m.
	carMesh.reset(BuildRoundedBox(Vector3(1.0f, 0.55f, 2.2f), 0.35f, 24, 48));
	seaMesh.reset(Mesh::GenerateQuad());   // also used as the full-screen quad for the sky

	terrainShader = std::make_unique<Shader>("sceneVertex.glsl", "terrainFragment.glsl");
	roadShader    = std::make_unique<Shader>("sceneVertex.glsl", "roadFragment.glsl");
	seaShader     = std::make_unique<Shader>("sceneVertex.glsl", "seaFragment.glsl");
	litShader     = std::make_unique<Shader>("sceneVertex.glsl", "litFragment.glsl");
	shadowShader  = std::make_unique<Shader>("shadowVertex.glsl", "shadowFragment.glsl");
	carShader     = std::make_unique<Shader>("sceneVertex.glsl", "carFragment.glsl");
	skinnedShader = std::make_unique<Shader>("skinnedVertex.glsl", "skinnedFragment.glsl");
	skinnedShadowShader = std::make_unique<Shader>("skinnedShadowVertex.glsl", "shadowFragment.glsl");
	skyShader     = std::make_unique<Shader>("skyVertex.glsl", "skyFragment.glsl");
	bakeSkyShader = std::make_unique<Shader>("bakeSkyVertex.glsl", "bakeSkyFragment.glsl");

	grassTexture = LoadTexture("grass.png");
	sandTexture  = LoadTexture("sand.png");
	rockTexture  = LoadTexture("rock.png");

	if (!carMesh || !terrainShader->LoadSuccess() || !roadShader->LoadSuccess() ||
	    !seaShader->LoadSuccess() || !litShader->LoadSuccess() || !shadowShader->LoadSuccess() ||
	    !carShader->LoadSuccess() || !skinnedShader->LoadSuccess() || !skinnedShadowShader->LoadSuccess() ||
	    !skyShader->LoadSuccess() || !bakeSkyShader->LoadSuccess() ||
	    !grassTexture || !sandTexture || !rockTexture) {
		std::cout << "Renderer: resource loading failed, see messages above.\n";
		return;   // init stays false, so main() exits cleanly
	}

	CreateWhiteTexture();
	BuildScene();
	BuildWalkers();
	CreateShadowMap();

	// Frame uniform buffer, bound once to binding point 0 (matches frame.glslh).
	glGenBuffers(1, &frameUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, frameUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameData), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUBO);

	// Constant "midday" environment for now; time of day and seasons will drive these later.
	Vector3 sun(0.45f, 0.70f, 0.55f);
	const float sunLen = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
	sun = sun * (1.0f / sunLen);
	sunDirection = sun;
	frameData.sunDir    = Vector4(sun.x, sun.y, sun.z, 1.0f);
	frameData.sunColour = Vector4(1.0f, 0.95f, 0.85f, 1.0f);
	frameData.fogColour = Vector4(0.62f, 0.75f, 0.90f, 0.00045f);

	zenithColour = Vector3(0.18f, 0.40f, 0.78f);
	BuildEnvironmentMap();   // needs the frame UBO and the sun / fog values above

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
	glDeleteTextures(1, &whiteTexture);
	glDeleteTextures(1, &envCube);
	glDeleteBuffers(1, &frameUBO);
	glDeleteTextures(1, &shadowTex);
	glDeleteFramebuffers(1, &shadowFBO);
}

// The scene graph:
//   root
//   +-- terrain (group, no mesh)
//   |     +-- 64 terrain chunks (each with its own bounding sphere)
//   +-- road
//   +-- car
//   +-- walkers (skinned, animated people; see BuildWalkers)
//   +-- sea (transparent list)
// Only nodes with a mesh are drawn or culled. Grouping nodes exist so later batches can
// hang things (wheels on the car, buildings in a district) off one parent transform.
void Renderer::BuildScene() {
	root = std::make_unique<SceneNode>();

	SceneNode* terrainGroup = new SceneNode();
	root->AddChild(terrainGroup);
	for (const Terrain::Chunk& chunk : terrain->GetChunks()) {
		SceneObject* node = new SceneObject(chunk.mesh.get(), Material::Terrain);
		node->SetTransform(Matrix4::Translation(chunk.centre));   // chunk vertices are centre-relative
		node->SetBoundingRadius(chunk.radius);
		terrainGroup->AddChild(node);
	}

	// The road is one mesh for now; a bounding sphere around the origin covers all of it.
	SceneObject* roadNode = new SceneObject(roadMesh.get(), Material::Road);
	roadNode->SetBoundingRadius(800.0f);
	roadNode->SetCastsShadow(false);   // a thin ribbon on the ground: it receives shadows but need not cast them
	root->AddChild(roadNode);

	// Reflective paint (Material::Car). The mesh already has the car's proportions. Transform set every frame.
	carNode = new SceneObject(carMesh.get(), Material::Car, Vector4(0.75f, 0.08f, 0.08f, 1.0f));
	carNode->SetBoundingRadius(3.0f);
	root->AddChild(carNode);

	// The sea quad is rotated flat and scaled huge; it has no alpha yet, but it lives in the
	// transparent list so sorting and blending are already in place when the water gets alpha.
	SceneObject* seaNode = new SceneObject(seaMesh.get(), Material::Sea, Vector4(1, 1, 1, 1), true);
	seaNode->SetTransform(Matrix4::Rotation(-90.0f, Vector3(1, 0, 0)));
	seaNode->SetModelScale(Vector3(8000.0f, 8000.0f, 1.0f));
	seaNode->SetBoundingRadius(12000.0f);
	root->AddChild(seaNode);
}

void Renderer::CreateWhiteTexture() {
	const unsigned char white[4] = { 255, 255, 255, 255 };
	glGenTextures(1, &whiteTexture);
	glBindTexture(GL_TEXTURE_2D, whiteTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // no mipmaps for a 1x1 texture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
}

// Renders the analytic sky (sky.glslh) into the six faces of a cube map. Reflective objects
// sample it, so what they mirror is exactly what is drawn behind the scene. Runs once at
// start-up; when time of day changes the sun, call it again.
void Renderer::BuildEnvironmentMap() {
	const int size = 128;
	StartDebugGroup("Bake environment map");

	if (!envCube) {
		glGenTextures(1, &envCube);
	}
	glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
	for (int face = 0; face < 6; ++face) {
		// 16-bit float, so the bright sun disc keeps values above 1 for reflections
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, size, size);

	UploadFrameData();   // the bake shader reads the sun and fog from the frame UBO
	Use(*bakeSkyShader);
	glUniform3f(glGetUniformLocation(activeProgram, "zenithColour"), zenithColour.x, zenithColour.y, zenithColour.z);
	const GLint faceLoc = glGetUniformLocation(activeProgram, "face");
	for (int face = 0; face < 6; ++face) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, envCube, 0);
		glUniform1i(faceLoc, face);
		seaMesh->Draw();   // the full-screen quad
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glViewport(0, 0, width, height);

	glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);   // blurrier mips = rougher reflections later
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	materialBound = false;   // we changed the active program outside BindMaterial
	EndDebugGroup();
}

// Draws the sky last among the opaque things: depth test LEQUAL with the quad at depth 1.0 means
// only pixels nothing else covered get shaded, so no fragment work is wasted behind the scene.
void Renderer::DrawSky() {
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);

	Use(*skyShader);
	Matrix4 rotationOnly = viewMatrix;   // drop the translation: the sky is infinitely far away
	rotationOnly.values[12] = rotationOnly.values[13] = rotationOnly.values[14] = 0.0f;
	const Matrix4 inverseProjView = (projMatrix * rotationOnly).Inverse();
	glUniformMatrix4fv(glGetUniformLocation(activeProgram, "inverseProjView"), 1, false, inverseProjView.values);
	glUniform3f(glGetUniformLocation(activeProgram, "zenithColour"), zenithColour.x, zenithColour.y, zenithColour.z);
	seaMesh->Draw();

	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	materialBound = false;
}

// A few CesiumMan instances strolling along the road verge. When the final character model
// arrives, only the path / fix matrix / scale in the next few lines change.
void Renderer::BuildWalkers() {
	walkerModel = std::make_unique<AnimatedModel>();
	// Z-up model, ~1.5 units tall: scale 1.2 makes a 1.8 m person.
	if (!walkerModel->Load("../GLTF/CesiumMan/CesiumMan.gltf", AnimatedModel::ZUpFix(1.2f))) {
		std::cout << "Renderer: walkers disabled (copy the GLTF folder next to the solution, see HANDOFF.md)\n";
		walkerModel.reset();
		return;
	}

	// Spread along the whole road, each with a long stretch to stroll (100 m each way at 1.3 m/s
	// is over a minute before turning), so they do not bunch up and rarely reverse on camera.
	const int   kWalkerCount = 5;
	const float startDistance[kWalkerCount] = { 90.0f, 330.0f, 560.0f, 800.0f, 1050.0f };
	const float duration = walkerModel->ClipDuration(0);

	for (int i = 0; i < kWalkerCount; ++i) {
		// Different start times, so the walkers are not in lock-step.
		SkinnedObject* node = new SkinnedObject(walkerModel.get(), 0, duration * 0.27f * i, 1.0f);
		root->AddChild(node);

		Walker w;
		w.node        = node;
		w.distance    = startDistance[i];
		w.direction   = (i % 2 == 0) ? 1.0f : -1.0f;
		w.minDistance = std::max(5.0f, startDistance[i] - 100.0f);
		w.maxDistance = startDistance[i] + 100.0f;
		w.side        = (i % 2 == 0) ? 8.0f : -8.0f;
		w.speed       = 1.3f;
		walkers.push_back(w);
	}
	UpdateWalkers(0.0f);   // so the first frame already has them in place
}

void Renderer::UpdateWalkers(float dt) {
	for (Walker& w : walkers) {
		w.distance += w.direction * w.speed * dt;
		if (w.distance > w.maxDistance) { w.distance = w.maxDistance; w.direction = -1.0f; }
		if (w.distance < w.minDistance) { w.distance = w.minDistance; w.direction =  1.0f; }

		const Vector3 centre = road->PositionAt(w.distance);
		Vector3 along = road->DirectionAt(w.distance);
		along.y = 0.0f;
		const float len = std::sqrt(along.x * along.x + along.z * along.z);
		along = along * (1.0f / len);

		// "Right of the road" for a road heading along -Z at yaw 0 is +X: right = (-z, 0, x).
		const float px = centre.x + (-along.z) * w.side;
		const float pz = centre.z + ( along.x) * w.side;
		const float py = terrain->HeightAt(px, pz);

		const Vector3 heading = along * w.direction;   // the way the person faces
		w.node->SetTransform(
			Matrix4::Translation(Vector3(px, py, pz)) *
			Matrix4::Rotation(YawFromDirection(heading), Vector3(0, 1, 0)));
	}
}

GLuint Renderer::LoadTexture(const std::string& fileName) {
	const std::string path = std::string(TEXTUREDIR) + fileName;
	GLuint tex = SOIL_load_OGL_texture(path.c_str(), SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS);
	if (!tex) {
		std::cout << "Renderer: could not load texture " << path << "\n";
		return 0;
	}
	SetTextureRepeating(tex, true);

	// Anisotropic filtering: stops textures smearing at grazing angles (the "smudged" grass and
	// road edges). 0x84FE / 0x84FF are GL_TEXTURE_MAX_ANISOTROPY(_EXT) / GL_MAX_TEXTURE_MAX_ANISOTROPY(_EXT).
	GLfloat maxAnisotropy = 1.0f;
	glGetFloatv(0x84FF, &maxAnisotropy);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameterf(GL_TEXTURE_2D, 0x84FE, std::min(16.0f, maxAnisotropy));
	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}

void Renderer::Use(Shader& shader) {
	BindShader(&shader);
	activeProgram = shader.GetProgram();
}

void Renderer::UpdateScene(float /*dt*/) {
	const auto now = std::chrono::steady_clock::now();
	const float rawDt = std::chrono::duration<float>(now - lastTick).count();   // real frame time, for the stats
	const float dt    = std::min(rawDt, 0.1f);                                  // the simulation never jumps by more than 0.1 s
	lastTick = now;
	elapsedSeconds += dt;

	auto* keys = Window::GetKeyboard();
	if (keys->KeyTriggered(KEYBOARD_C)) { SwitchCameraMode(); }
	if (keys->KeyTriggered(KEYBOARD_F)) { cullingFrozen = !cullingFrozen; }
	if (keys->KeyTriggered(KEYBOARD_H)) { shadowsEnabled = !shadowsEnabled; }

	if (cameraMode == CameraMode::Cinematic) {
		UpdateCar(dt);   // the car only drives while the cinematic camera is following it
		UpdateCinematicCamera();
	}
	else {
		UpdateFreeCamera(dt);
	}

	projMatrix = Matrix4::Perspective(1.0f, 6000.0f, (float)width / (float)height, 60.0f);

	UpdateWalkers(dt);   // set the walkers' transforms before the graph recomputes world matrices
	root->Update(dt);   // recomputes every node's world transform from its parent's

	if (!cullingFrozen) {
		cullFrustum.FromMatrix(projMatrix * viewMatrix);
	}

	UpdateShadowMatrix();
	UpdateWindowTitle(rawDt);
}

void Renderer::UpdateCar(float dt) {
	carDistance += kCarSpeed * dt;
	if (carDistance > road->GetLength() - 30.0f) {
		carDistance = 0.0f;   // loop for now
	}

	const Vector3 pos = road->PositionAt(carDistance);
	const Vector3 dir = road->DirectionAt(carDistance);
	carNode->SetTransform(
		Matrix4::Translation(pos + Vector3(0.0f, 0.6f, 0.0f)) *
		Matrix4::Rotation(YawFromDirection(dir), Vector3(0, 1, 0)) *
		Matrix4::Rotation(PitchFromDirection(dir), Vector3(1, 0, 0)));
}

void Renderer::UpdateCinematicCamera() {
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
	}
	else {
		cameraMode = CameraMode::Cinematic;
		cullingFrozen = false;   // a frozen frustum only makes sense while flying around
	}
}

void Renderer::UpdateWindowTitle(float frameTime) {
	// Lifetime min/max/average skip the first seconds, which are dominated by start-up hitches.
	if (elapsedSeconds > 3.0f) {
		++fpsFrames;
		fpsTotalTime += frameTime;
		worstFrame = std::max(worstFrame, frameTime);
		bestFrame  = std::min(bestFrame, frameTime);
	}

	titleTimer += frameTime;
	++titleFrames;
	if (titleTimer < 0.5f) {
		return;   // refresh the title twice a second, not every frame
	}

	const double average = fpsFrames ? fpsFrames / fpsTotalTime : 0.0;
	const double low     = worstFrame > 0.0f ? 1.0 / worstFrame : 0.0;
	const double high    = bestFrame < 1e8f ? 1.0 / bestFrame : 0.0;

	char title[320];
	std::snprintf(title, sizeof(title),
		"Grand Theft const auto& : Imprecise City | %.0f fps (avg %.0f, low %.0f, high %.0f) | nodes %u/%u | draws %u (+%u shadow) | tris %u | frustum %s | shadows %s | cam %s",
		titleFrames / titleTimer, average, low, high,
		stats.nodesDrawn, stats.nodesTotal, stats.drawCalls, stats.shadowDraws, stats.triangles,
		cullingFrozen ? "FROZEN" : "camera",
		shadowsEnabled ? "on" : "off",
		cameraMode == CameraMode::Cinematic ? "cinematic" : "free");
	if (HWND window = GetActiveWindow()) {
		SetWindowTextA(window, title);
	}
	titleTimer  = 0.0f;
	titleFrames = 0;
}

void Renderer::UploadFrameData() {
	frameData.viewMatrix      = viewMatrix;
	frameData.projMatrix      = projMatrix;
	frameData.shadowViewProj  = shadowProj * shadowView;
	frameData.cameraPos       = Vector4(eyePosition.x, eyePosition.y, eyePosition.z, 1.0f);
	frameData.timeInfo        = Vector4(elapsedSeconds, 0.0f, 0.0f, 0.0f);
	// x = size of one shadow-map texel in UV units, y = depth bias, z = normal offset in world units, w = on/off
	frameData.shadowParams    = Vector4(1.0f / kShadowMapSize, kShadowDepthBias, 1.5f * shadowTexelWorld, shadowsEnabled ? 1.0f : 0.0f);

	glBindBuffer(GL_UNIFORM_BUFFER, frameUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameData), &frameData);
}

// Walks the graph once and keeps every mesh node whose bounding sphere touches 'frustum'.
// Main pass: survivors go into the opaque or transparent list (opposite sort orders).
// Shadow pass: survivors that cast shadows go into the caster list, tested against the
// light's frustum, so off-screen-for-the-sun terrain is not rendered into the shadow map.
void Renderer::CullNodes(SceneNode* node, Frustum& frustum, bool shadowPass) {
	if (node->GetMesh()) {
		SceneObject* object = static_cast<SceneObject*>(node);   // valid: see SceneObject.h

		if (shadowPass) {
			if (object->CastsShadow() && !object->IsTransparent() && frustum.InsideFrustum(*node)) {
				shadowCasterList.push_back(object);
			}
		}
		else {
			++stats.nodesTotal;
			if (frustum.InsideFrustum(*node)) {
				++stats.nodesDrawn;

				// Squared distance is enough for ordering and skips the square root.
				const Vector3 d = node->GetWorldTransform().GetPositionVector() - eyePosition;
				node->SetCameraDistance(d.x * d.x + d.y * d.y + d.z * d.z);

				(object->IsTransparent() ? transparentList : opaqueList).push_back(object);
			}
		}
	}
	for (auto i = node->GetChildIteratorStart(); i != node->GetChildIteratorEnd(); ++i) {
		CullNodes(*i, frustum, shadowPass);
	}
}

// Fits an orthographic "sun camera" around the area in front of the viewer.
void Renderer::UpdateShadowMatrix() {
	// The camera's forward vector is minus the third row of the view matrix's rotation.
	const Vector3 forward(-viewMatrix.values[2], -viewMatrix.values[6], -viewMatrix.values[10]);

	// Bigger box when the camera is high (overview shots); stepped, so it does not change every frame.
	const float halfSize = std::min(700.0f, 90.0f + 50.0f * std::floor(std::max(eyePosition.y, 0.0f) / 80.0f));
	const Vector3 focus    = eyePosition + forward * (halfSize * 0.6f);
	const Vector3 lightEye = focus + sunDirection * kShadowLightDistance;
	const Vector3 up       = std::fabs(sunDirection.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);

	shadowView       = Matrix4::BuildViewMatrix(lightEye, focus, up);
	shadowTexelWorld = 2.0f * halfSize / kShadowMapSize;

	// Texel snapping: round the light-space origin to a whole number of shadow texels. Without
	// this the shadow map slides by sub-texel amounts as the camera moves, and shadow edges shimmer.
	shadowView.values[12] = std::floor(shadowView.values[12] / shadowTexelWorld + 0.5f) * shadowTexelWorld;
	shadowView.values[13] = std::floor(shadowView.values[13] / shadowTexelWorld + 0.5f) * shadowTexelWorld;

	shadowProj = Matrix4::Orthographic(1.0f, kShadowLightDistance * 2.0f, halfSize, -halfSize, halfSize, -halfSize);
	shadowFrustum.FromMatrix(shadowProj * shadowView);
}

// A depth-only texture the sun renders into. Hardware depth comparison (REF_TO_TEXTURE) lets
// the fragment shader ask "is this point in shadow?" and get a filtered answer back.
void Renderer::CreateShadowMap() {
	glGenTextures(1, &shadowTex);
	glBindTexture(GL_TEXTURE_2D, shadowTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowMapSize, kShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // outside the map counts as "nothing in the way"
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

	glGenFramebuffers(1, &shadowFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
	glDrawBuffer(GL_NONE);   // depth only: no colour attachment
	glReadBuffer(GL_NONE);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cout << "Renderer: shadow map framebuffer is not complete!\n";
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::RenderShadowPass() {
	StartDebugGroup("Shadow pass");

	shadowCasterList.clear();
	CullNodes(root.get(), shadowFrustum, true);

	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glViewport(0, 0, kShadowMapSize, kShadowMapSize);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Slope-scaled depth bias, so surfaces nearly parallel to the sun do not shadow themselves.
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.0f, 2.0f);

	Use(*shadowShader);
	const GLint shadowModelLoc = glGetUniformLocation(activeProgram, "modelMatrix");
	bool anySkinned = false;
	for (SceneObject* node : shadowCasterList) {
		if (node->GetMaterial() == Material::Skinned) {
			anySkinned = true;   // drawn below with the skinning shader
			continue;
		}
		const Matrix4 model = node->GetWorldTransform() * Matrix4::Scale(node->GetModelScale());
		glUniformMatrix4fv(shadowModelLoc, 1, false, model.values);
		node->Draw(*this);
		++stats.shadowDraws;
	}

	if (anySkinned) {
		Use(*skinnedShadowShader);
		const GLint skinModelLoc  = glGetUniformLocation(activeProgram, "modelMatrix");
		const GLint skinJointsLoc = glGetUniformLocation(activeProgram, "joints");
		for (SceneObject* node : shadowCasterList) {
			if (node->GetMaterial() != Material::Skinned) {
				continue;
			}
			SkinnedObject* skinned = static_cast<SkinnedObject*>(node);   // valid: only SkinnedObject uses Material::Skinned
			const Matrix4 model = skinned->GetWorldTransform() * skinned->GetModel().GetFix();
			glUniformMatrix4fv(skinModelLoc, 1, false, model.values);
			const std::vector<Matrix4>& pose = skinned->GetPose();
			glUniformMatrix4fv(skinJointsLoc, (GLsizei)pose.size(), false, pose[0].values);
			for (int i = 0; i < skinned->GetModel().SubMeshCount(); ++i) {
				skinned->GetMesh()->DrawSubMesh(i);
			}
			++stats.shadowDraws;
		}
	}

	glDisable(GL_POLYGON_OFFSET_FILL);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
	EndDebugGroup();
}

void Renderer::SortNodeLists() {
	// Opaque: group by material (fewest shader switches), then nearest first so the depth
	// test rejects hidden fragments before the fragment shader runs (early-z).
	std::sort(opaqueList.begin(), opaqueList.end(), [](const SceneObject* a, const SceneObject* b) {
		if (a->GetMaterial() != b->GetMaterial()) {
			return a->GetMaterial() < b->GetMaterial();
		}
		return a->GetCameraDistance() < b->GetCameraDistance();
	});

	// Transparent: farthest first, so nearer surfaces blend over farther ones correctly.
	std::sort(transparentList.begin(), transparentList.end(), [](const SceneObject* a, const SceneObject* b) {
		return a->GetCameraDistance() > b->GetCameraDistance();
	});
}

void Renderer::ClearNodeLists() {
	opaqueList.clear();
	transparentList.clear();
}

void Renderer::BindMaterial(Material material) {
	if (materialBound && material == boundMaterial) {
		return;
	}

	switch (material) {
		case Material::Terrain:
			Use(*terrainShader);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, grassTexture);
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, sandTexture);
			glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, rockTexture);
			break;
		case Material::Road: Use(*roadShader); break;
		case Material::Lit:  Use(*litShader);  break;
		case Material::Car:  Use(*carShader);  break;
		case Material::Skinned: Use(*skinnedShader); break;
		case Material::Sea:  Use(*seaShader);  break;
	}

	// Pull the road slightly toward the camera so it never z-fights the terrain under it.
	if (material == Material::Road) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -1.0f);
	}
	else {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	// Uniform locations are fixed per program, so look them up here, not per draw.
	modelLoc  = glGetUniformLocation(activeProgram, "modelMatrix");
	colourLoc = glGetUniformLocation(activeProgram, "baseColour");   // -1 if the shader has none
	jointsLoc = glGetUniformLocation(activeProgram, "joints");       // only the skinning shader has bones

	boundMaterial = material;
	materialBound = true;
}

// A skinned character: upload this instance's bone matrices, then draw each sub-mesh with its
// own glTF material (texture and/or colour factor). Sub-mesh n uses material layer n.
void Renderer::DrawSkinned(SkinnedObject* node) {
	BindMaterial(Material::Skinned);

	const AnimatedModel& model = node->GetModel();
	const Matrix4 world = node->GetWorldTransform() * model.GetFix();
	glUniformMatrix4fv(modelLoc, 1, false, world.values);

	const std::vector<Matrix4>& pose = node->GetPose();
	glUniformMatrix4fv(jointsLoc, (GLsizei)pose.size(), false, pose[0].values);

	glActiveTexture(GL_TEXTURE0);
	for (int i = 0; i < model.SubMeshCount(); ++i) {
		const GLTFMaterialLayer& layer = model.Layer(i);
		glBindTexture(GL_TEXTURE_2D, layer.albedo ? layer.albedo->GetObjectID() : whiteTexture);
		glUniform4f(colourLoc, layer.albedoColour.x, layer.albedoColour.y, layer.albedoColour.z, layer.albedoColour.w);
		node->GetMesh()->DrawSubMesh(i);
		++stats.drawCalls;
	}
	stats.triangles += node->GetMesh()->GetTriCount();
}

void Renderer::DrawNode(SceneObject* node) {
	if (node->GetMaterial() == Material::Skinned) {
		DrawSkinned(static_cast<SkinnedObject*>(node));   // valid: only SkinnedObject uses Material::Skinned
		return;
	}
	BindMaterial(node->GetMaterial());

	const Matrix4 model = node->GetWorldTransform() * Matrix4::Scale(node->GetModelScale());
	glUniformMatrix4fv(modelLoc, 1, false, model.values);
	if (colourLoc >= 0) {
		const Vector4 colour = node->GetColour();
		glUniform4f(colourLoc, colour.x, colour.y, colour.z, colour.w);
	}

	node->Draw(*this);

	++stats.drawCalls;
	stats.triangles += node->GetMesh()->GetTriCount();
}

void Renderer::DrawNodes(const std::vector<SceneObject*>& list) {
	for (SceneObject* node : list) {
		DrawNode(node);
	}
}

void Renderer::RenderScene() {
	stats = FrameStats();
	UploadFrameData();

	if (shadowsEnabled) {
		RenderShadowPass();
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	// The shadow map lives on texture unit 4 for the whole main pass (see lighting.glslh).
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, shadowTex);
	glActiveTexture(GL_TEXTURE5);   // the environment cube map lives on unit 5 (see carFragment.glsl)
	glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
	glActiveTexture(GL_TEXTURE0);
	materialBound = false;

	StartDebugGroup("Scene culling and sorting");
	CullNodes(root.get(), cullFrustum, false);
	SortNodeLists();
	EndDebugGroup();

	StartDebugGroup("Opaque pass");
	DrawNodes(opaqueList);
	EndDebugGroup();

	StartDebugGroup("Sky pass");
	DrawSky();
	EndDebugGroup();

	StartDebugGroup("Transparent pass");
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	DrawNodes(transparentList);
	glDisable(GL_BLEND);
	EndDebugGroup();

	ClearNodeLists();
}
