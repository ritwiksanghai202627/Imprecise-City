#include "Renderer.h"
#include "RoadPath.h"
#include "Terrain.h"
#include "ProceduralMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <vector>

namespace {
	const float kRadToDeg = 57.2957795f;
	const float kPi       = 3.14159265f;

	float Smoothstep(float e0, float e1, float x) {
		float t = (x - e0) / (e1 - e0);
		t = std::min(std::max(t, 0.0f), 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
	Vector3 Mix(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }

	// Cinematic shots: how long each plays (seconds). EvaluateShot has the matching camera moves.
	const float kShotLength[8] = { 12.0f, 12.0f, 12.0f, 9.0f, 12.0f, 7.0f, 8.0f, 8.0f };
	const bool  kShotNeedsCity[8] = { false, false, true, false, true, true, false, false };

	const float kStartDelay = 2.0f;                        // seconds of black before anything moves
	const float kRenderScales[3] = { 1.0f, 0.85f, 0.7f };  // R key: internal resolution of the scene

	float MixF(float a, float b, float t) { return a + (b - a) * t; }

	// Time-of-day presets (hours) the T key cycles through.
	const float kTimePresets[5]  = { 12.0f, 16.5f, 18.4f, 0.0f, 6.2f };
	const char* kTimePresetName[5] = { "noon", "afternoon", "dusk", "midnight", "dawn" };

	// Cinematic camera tuning
	const float kCarSpeed      = 20.0f;   // metres per second
	const float kPlayerLane    = 2.6f;    // the player drives in the right-hand lane, like the traffic
	const float kCamBehind     = 16.0f;
	const float kCamHeight     = 4.5f;
	const float kLookAhead     = 12.0f;
	const float kLookHeight    = 1.5f;

	// Free-camera fly speed: held left / right mouse button ramps it at runtime.
	const float kFlySpeedRate = 1.0f;    // e-fold per second of holding (right = faster, left = slower)
	const float kFlySpeedMin  = 0.05f;   // slowest (fine placement)
	const float kFlySpeedMax  = 20.0f;   // fastest (overview shots)

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
		Vector3(-470, 0,  260), Vector3(-330, 0,  250), Vector3(-370, 0,   90),
		Vector3(-210, 0,  -10), Vector3( -40, 0,  120), Vector3( 130, 0,   40),
		Vector3( 200, 0, -140), Vector3(  60, 0, -260), Vector3( -90, 0, -330),
		// the way back round the west side, so the road is a closed loop
		Vector3(-260, 0, -350), Vector3(-430, 0, -240), Vector3(-530, 0,  -70), Vector3(-535, 0,  110)
	};
	road = std::make_unique<RoadPath>(roadControlPoints, 4.0f, true);
	road->SnapToTerrain(&Terrain::IslandHeight, 10.0f, 8, 10);

	// The city goes on the flattest land the road passes: level the road and the ground there first.
	Vector3 cityCentre;
	float   cityRoadDistance = 0.0f;
	TerrainPlateau plateau;
	// Try a big flat site first, then settle for smaller ring checks if the coast is in the way.
	bool cityFound = false;
	for (float scale : { 1.0f, 0.8f, 0.6f }) {
		cityFound = City::ChooseSite(*road, &Terrain::IslandHeight, CityConst::kPlateauInner * scale, cityCentre, cityRoadDistance);
		if (cityFound) {
			break;
		}
	}
	if (cityFound) {
		road->FlattenAround(cityCentre, CityConst::kPlateauInner, CityConst::kPlateauOuter, cityCentre.y);
		plateau.x = cityCentre.x; plateau.z = cityCentre.z; plateau.y = cityCentre.y;
		plateau.inner = CityConst::kPlateauInner; plateau.outer = CityConst::kPlateauOuter;
	}
	terrain = std::make_unique<Terrain>(*road, plateau);
	roadSegments = road->BuildRibbonSegments(5.0f, 0.15f, 8.0f, 48);   // 48 samples = 192 m per piece, so the road can be culled

	// Placeholder car body: a smooth rounded box (so reflections look right), 2 x 1.1 x 4.4 m.
	carMesh.reset(BuildRoundedBox(Vector3(1.0f, 0.55f, 2.2f), 0.35f, 24, 48));
	seaMesh.reset(Mesh::GenerateQuad());   // also used as the full-screen quad for the sky

	terrainShader = std::make_unique<Shader>("sceneVertex.glsl", "terrainFragment.glsl");
	roadShader    = std::make_unique<Shader>("sceneVertex.glsl", "roadFragment.glsl");
	seaShader     = std::make_unique<Shader>("sceneVertex.glsl", "seaFragment.glsl");
	litShader     = std::make_unique<Shader>("sceneVertex.glsl", "litFragment.glsl");
	shadowShader  = std::make_unique<Shader>("shadowVertex.glsl", "shadowFragment.glsl");
	carShader     = std::make_unique<Shader>("carVertex.glsl", "carFragment.glsl");
	skinnedShader = std::make_unique<Shader>("skinnedVertex.glsl", "skinnedFragment.glsl");
	skinnedShadowShader = std::make_unique<Shader>("skinnedShadowVertex.glsl", "shadowFragment.glsl");
	buildingShader       = std::make_unique<Shader>("buildingVertex.glsl", "buildingFragment.glsl");
	buildingShadowShader = std::make_unique<Shader>("buildingShadowVertex.glsl", "shadowFragment.glsl");
	brightShader    = std::make_unique<Shader>("postVertex.glsl", "brightFragment.glsl");
	blurShader      = std::make_unique<Shader>("postVertex.glsl", "blurFragment.glsl");
	compositeShader = std::make_unique<Shader>("postVertex.glsl", "compositeFragment.glsl");
	skyShader     = std::make_unique<Shader>("skyVertex.glsl", "skyFragment.glsl");
	bakeSkyShader = std::make_unique<Shader>("bakeSkyVertex.glsl", "bakeSkyFragment.glsl");

	grassTexture = LoadTexture("grass.png");
	sandTexture  = LoadTexture("sand.png");
	rockTexture  = LoadTexture("rock.png");

	if (!carMesh || !terrainShader->LoadSuccess() || !roadShader->LoadSuccess() ||
	    !seaShader->LoadSuccess() || !litShader->LoadSuccess() || !shadowShader->LoadSuccess() ||
	    !carShader->LoadSuccess() || !skinnedShader->LoadSuccess() || !skinnedShadowShader->LoadSuccess() ||
	    !buildingShader->LoadSuccess() || !buildingShadowShader->LoadSuccess() ||
	    !brightShader->LoadSuccess() || !blurShader->LoadSuccess() || !compositeShader->LoadSuccess() ||
	    !skyShader->LoadSuccess() || !bakeSkyShader->LoadSuccess() ||
	    !grassTexture || !sandTexture || !rockTexture) {
		std::cout << "Renderer: resource loading failed, see messages above.\n";
		return;   // init stays false, so main() exits cleanly
	}

	CreateWhiteTexture();
	BuildShoreTexture();
	BuildScene();
	hasCity = cityFound;
	this->cityCentre = cityCentre;
	this->cityRoadDistance = cityRoadDistance;
	if (cityFound) {
		BuildCity(cityCentre, cityRoadDistance);

		// Start the drive about 450 m before the city, so the opening is the winding approach into town.
		const float length = road->GetLength();
		carDistance = cityRoadDistance - 450.0f;
		carDistance -= std::floor(carDistance / length) * length;
	}
	BuildTraffic();
	BuildWalkers();
	CreateShadowMap();
	CreatePostTargets();
	StartShot(0);
	glGenQueries(2 * PassCount, &queries[0][0]);

	// Frame uniform buffer, bound once to binding point 0 (matches frame.glslh).
	glGenBuffers(1, &frameUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, frameUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameData), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUBO);

	// Sun, sky and fog come from the time of day (starts at noon).
	ApplyTimeOfDay();
	BuildEnvironmentMap();   // needs the frame UBO and the sun / fog values above
	lastBakedHour = timeOfDay;

	glEnable(GL_DEPTH_TEST);

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
	glDeleteTextures(1, &shoreTex);
	glDeleteBuffers(1, &frameUBO);
	glDeleteTextures(1, &shadowTex);
	glDeleteFramebuffers(1, &shadowFBO);
	glDeleteQueries(2 * PassCount, &queries[0][0]);
	DestroyPostTargets();
	if (!citySSBOs.empty()) {
		glDeleteBuffers(static_cast<GLsizei>(citySSBOs.size()), citySSBOs.data());
	}
}

void Renderer::BeginGpuTimer(GpuPass pass) {
	glBeginQuery(GL_TIME_ELAPSED, queries[querySet][pass]);
}

void Renderer::EndGpuTimer() {
	glEndQuery(GL_TIME_ELAPSED);
}

// Reads the OTHER set (issued last frame), which the GPU has almost certainly finished.
void Renderer::ReadGpuTimers() {
	if (!queriesPrimed) {
		return;
	}
	const int previous = 1 - querySet;
	for (int p = 0; p < PassCount; ++p) {
		GLuint64 ns = 0;
		glGetQueryObjectui64v(queries[previous][p], GL_QUERY_RESULT, &ns);
		gpuMs[p] = gpuMs[p] * 0.9f + (float)(ns * 1e-6) * 0.1f;   // smooth so the title is readable
	}
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
// Samples the carved terrain on a 512 x 512 grid covering the whole island and uploads it as a
// 16-bit float texture. The sea shader reads it to know how deep the water is at any point, which
// is what lets the foam and the shallow-water tint follow the real shoreline on the WATER side
// (the terrain itself only ever shows the beach above y = 0).
void Renderer::BuildShoreTexture() {
	const int   size   = 512;
	const float extent = terrain->GetHalfExtent();
	std::vector<float> heights(static_cast<size_t>(size) * size);
	for (int z = 0; z < size; ++z) {
		for (int x = 0; x < size; ++x) {
			const float wx = ((x + 0.5f) / size * 2.0f - 1.0f) * extent;
			const float wz = ((z + 0.5f) / size * 2.0f - 1.0f) * extent;
			heights[static_cast<size_t>(z) * size + x] = terrain->HeightAt(wx, wz);
		}
	}

	glGenTextures(1, &shoreTex);
	glBindTexture(GL_TEXTURE_2D, shoreTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, size, size, 0, GL_RED, GL_FLOAT, heights.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

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

	// The road is cut into ~192 m pieces; each is its own node with its own bounding sphere, so
	// pieces behind the camera are culled like terrain chunks.
	for (const RoadPath::RibbonSegment& segment : roadSegments) {
		SceneObject* roadNode = new SceneObject(segment.mesh.get(), Material::Road);
		roadNode->SetTransform(Matrix4::Translation(segment.centre));
		roadNode->SetBoundingRadius(segment.radius);
		roadNode->SetCastsShadow(false);   // a thin ribbon on the ground: it receives shadows but need not cast them
		root->AddChild(roadNode);
	}

	// Reflective paint (Material::Car). The mesh already has the car's proportions. Transform set every frame.
	carNode = new SceneObject(carMesh.get(), Material::Car, Vector4(0.55f, 0.10f, 0.55f, 1.0f));   // purple
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

// Sun, moon, sky and fog for the current hour. The sun travels east -> west over a tilted arc;
// when it is below the horizon the same light slot becomes a dim blue moon (its mirror image), so
// shadows and lighting keep working at night. Everything is a blend of three palettes: day, dusk, night.
void Renderer::ApplyTimeOfDay() {
	const float theta = (timeOfDay - 6.0f) / 12.0f * kPi;   // 0 at 06:00 (sunrise), pi at 18:00 (sunset)
	Vector3 sun(std::cos(theta), std::sin(theta) * 0.75f, 0.5f);
	sun = sun * (1.0f / std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z));
	const float elevation = sun.y;

	const Vector3 dayFog(0.62f, 0.75f, 0.90f),  duskFog(0.90f, 0.55f, 0.38f),  nightFog(0.035f, 0.045f, 0.11f);
	const Vector3 dayZen(0.18f, 0.40f, 0.78f),  duskZen(0.22f, 0.28f, 0.55f),  nightZen(0.008f, 0.012f, 0.04f);
	const Vector3 daySun(1.0f, 0.95f, 0.85f),   duskSun(1.0f, 0.50f, 0.22f),   moon(0.45f, 0.55f, 1.0f);
	const float   dayDensity = 0.00045f, duskDensity = 0.00060f, nightDensity = 0.0007f;
	const float   dayPower = 1.0f, duskPower = 0.85f, moonPower = 0.22f;

	Vector3 fog, zenith, light;
	float   density, power;
	Vector3 lightDirection = sun;
	if (elevation >= 0.0f) {
		const float t = Smoothstep(0.0f, 0.45f, elevation);
		fog = Mix(duskFog, dayFog, t);  zenith = Mix(duskZen, dayZen, t);  light = Mix(duskSun, daySun, t);
		density = duskDensity + (dayDensity - duskDensity) * t;
		power   = (duskPower + (dayPower - duskPower) * t) * Smoothstep(0.0f, 0.08f, elevation);   // fades to 0 at the horizon
	}
	else {
		const float t = Smoothstep(0.0f, 0.25f, -elevation);
		fog = Mix(duskFog, nightFog, t);  zenith = Mix(duskZen, nightZen, t);  light = Mix(duskSun, moon, t);
		density = duskDensity + (nightDensity - duskDensity) * t;
		power   = (duskPower + (moonPower - duskPower) * t) * Smoothstep(0.0f, 0.12f, -elevation);   // moon fades in after sunset
		lightDirection = Vector3(-sun.x, -sun.y, sun.z);   // the moon: mirror of the sun, above the horizon
	}

	sunDirection = lightDirection;
	frameData.sunDir    = Vector4(lightDirection.x, lightDirection.y, lightDirection.z, power);
	frameData.sunColour = Vector4(light.x, light.y, light.z, 1.0f);
	frameData.fogColour = Vector4(fog.x, fog.y, fog.z, density);
	zenithColour = zenith;

	const float night = 1.0f - Smoothstep(-0.10f, 0.10f, elevation);
	const float ambientScale = 0.35f + 0.65f * Smoothstep(-0.10f, 0.30f, elevation);
	frameData.environment.x = night;
	frameData.environment.w = ambientScale;

	glClearColor(fog.x, fog.y, fog.z, 1.0f);
}

// Picks the lights for this frame. Street lamps: only the 8 nearest the camera are sent to the GPU
// (the fragment shaders loop over a short list, never over every lamp on the island).
// Headlights: two spot lights on the player's car. Both switch on as it gets dark.
void Renderer::UpdateLights() {
	const float night   = frameData.environment.x;
	const float lampsOn = std::min(std::max((night - 0.2f) * 3.0f, 0.0f), 1.0f);

	int pointCount = 0;
	if (city && lampsOn > 0.0f) {
		const std::vector<Vector3>& heads = city->GetLampHeads();
		lampScratch.clear();
		for (size_t i = 0; i < heads.size(); ++i) {
			const Vector3 d = heads[i] - eyePosition;
			lampScratch.push_back(std::make_pair(d.x * d.x + d.y * d.y + d.z * d.z, static_cast<int>(i)));
		}
		pointCount = static_cast<int>(std::min<size_t>(kMaxPointLights, lampScratch.size()));
		std::partial_sort(lampScratch.begin(), lampScratch.begin() + pointCount, lampScratch.end());

		for (int i = 0; i < pointCount; ++i) {
			const Vector3& p = heads[lampScratch[i].second];
			GpuPointLight& light = frameData.pointLights[i];
			light.posRadius = Vector4(p.x, p.y, p.z, 45.0f);
			light.colour    = Vector4(1.0f * 3.0f * lampsOn, 0.72f * 3.0f * lampsOn, 0.40f * 3.0f * lampsOn, 1.0f);
		}
	}

	int spotCount = 0;
	if (night > 0.2f) {
		Vector3 forward = road->DirectionAt(carDistance);
		forward.y = 0.0f;
		const float len = std::sqrt(forward.x * forward.x + forward.z * forward.z);
		forward = forward * (1.0f / len);
		const Vector3 right(-forward.z, 0.0f, forward.x);
		const Vector3 pos = road->PositionAt(carDistance) + right * kPlayerLane;
		Vector3 aim(forward.x, -0.08f, forward.z);   // tilted slightly down onto the road
		aim = aim * (1.0f / std::sqrt(aim.x * aim.x + aim.y * aim.y + aim.z * aim.z));

		for (int i = 0; i < kMaxSpotLights; ++i) {
			const float side = (i == 0) ? -0.7f : 0.7f;
			const Vector3 p = pos + forward * 2.3f + right * side + Vector3(0.0f, 1.0f, 0.0f);
			GpuSpotLight& light = frameData.spotLights[i];
			light.posRange       = Vector4(p.x, p.y, p.z, 80.0f);
			light.dirCosInner    = Vector4(aim.x, aim.y, aim.z, std::cos(14.0f / kRadToDeg));
			light.colourCosOuter = Vector4(5.0f, 4.65f, 3.9f, std::cos(32.0f / kRadToDeg));
		}
		spotCount = kMaxSpotLights;
	}

	frameData.environment.y = static_cast<float>(pointCount);
	frameData.environment.z = static_cast<float>(spotCount);
}

// Turns the city's instance list into districts: instances are bucketed into 192 m cells, each cell
// gets one shader storage buffer and one scene node (own bounding sphere, so it can be culled).
void Renderer::BuildCity(const Vector3& centre, float roadDistance) {
	city = std::make_unique<City>();
	city->Generate(*road, centre, roadDistance);
	unitBox.reset(BuildUnitBox());

	const float cell = 192.0f;
	std::map<std::pair<int, int>, std::vector<CityInstance>> buckets;
	for (const CityInstance& inst : city->GetInstances()) {
		const int ix = static_cast<int>(std::floor(inst.px / cell));
		const int iz = static_cast<int>(std::floor(inst.pz / cell));
		buckets[std::make_pair(ix, iz)].push_back(inst);
	}

	for (const auto& bucket : buckets) {
		const std::vector<CityInstance>& list = bucket.second;

		Vector3 average(0, 0, 0);
		for (const CityInstance& inst : list) {
			average = average + Vector3(inst.px, inst.py + inst.sy * 0.5f, inst.pz);
		}
		average = average * (1.0f / list.size());

		float radius = 1.0f;
		for (const CityInstance& inst : list) {
			const Vector3 d = Vector3(inst.px, inst.py + inst.sy * 0.5f, inst.pz) - average;
			radius = std::max(radius, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
			                          + 0.5f * std::sqrt(inst.sx * inst.sx + inst.sy * inst.sy + inst.sz * inst.sz));
		}

		GLuint buffer = 0;
		glGenBuffers(1, &buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, list.size() * sizeof(CityInstance), list.data(), GL_STATIC_DRAW);
		citySSBOs.push_back(buffer);

		CityDistrict* district = new CityDistrict(unitBox.get(), buffer, static_cast<int>(list.size()));
		district->SetTransform(Matrix4::Translation(average));   // only used for culling and sorting; instances hold world positions
		district->SetBoundingRadius(radius);
		root->AddChild(district);
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	std::cout << "City: " << buckets.size() << " districts\n";
}

// A handful of other cars driving the loop: some with the road's direction (right-hand lane),
// some against it, at different speeds, in different paint.
void Renderer::BuildTraffic() {
	const int   kCount = 8;
	const Vector4 paint[kCount] = {
		Vector4(0.10f, 0.25f, 0.65f, 1), Vector4(0.85f, 0.85f, 0.88f, 1), Vector4(0.95f, 0.75f, 0.10f, 1), Vector4(0.08f, 0.08f, 0.09f, 1),
		Vector4(0.10f, 0.50f, 0.25f, 1), Vector4(0.80f, 0.35f, 0.08f, 1), Vector4(0.55f, 0.55f, 0.60f, 1), Vector4(0.75f, 0.08f, 0.08f, 1)
	};
	const float length = road->GetLength();

	for (int i = 0; i < kCount; ++i) {
		SceneObject* node = new SceneObject(carMesh.get(), Material::Car, paint[i]);
		node->SetBoundingRadius(3.0f);
		root->AddChild(node);

		// Cars that travel WITH the player share its lane and its exact speed, so they form a convoy
		// that can never run into each other. Oncoming cars (other lane) all share one slower speed
		// for the same reason; the closing speed with the player is what makes them whoosh past.
		const float convoyStart[kCount / 2] = { 90.0f, 260.0f, 700.0f, 1500.0f };
		const bool  withPlayer = (i % 2 == 0);

		TrafficCar car;
		car.node      = node;
		car.distance  = withPlayer ? convoyStart[i / 2] : length * ((i / 2) + 0.5f) / (kCount / 2);
		car.speed     = withPlayer ? kCarSpeed : 16.0f;
		car.direction = withPlayer ? 1.0f : -1.0f;
		car.lane      = withPlayer ? kPlayerLane : -kPlayerLane;
		traffic.push_back(car);
	}
	UpdateTraffic(0.0f);
}

void Renderer::UpdateTraffic(float dt) {
	const float length = road->GetLength();
	for (TrafficCar& car : traffic) {
		car.distance += car.direction * car.speed * dt;
		car.distance -= std::floor(car.distance / length) * length;   // wrap round the loop

		const Vector3 p = road->PositionAt(car.distance);
		Vector3 d = road->DirectionAt(car.distance);
		const Vector3 right(-d.z, 0.0f, d.x);   // right of the road's direction (y ignored: the road never banks)
		const Vector3 heading = d * car.direction;

		car.node->SetTransform(
			Matrix4::Translation(p + right * car.lane + Vector3(0.0f, 0.6f, 0.0f)) *
			Matrix4::Rotation(YawFromDirection(heading), Vector3(0, 1, 0)) *
			Matrix4::Rotation(PitchFromDirection(heading), Vector3(1, 0, 0)));
	}
}

// ---- Cinematic director ---------------------------------------------------------------------

void Renderer::GetCarFrame(Vector3& position, Vector3& forward, Vector3& right) const {
	forward  = road->DirectionAt(carDistance);
	right    = Vector3(-forward.z, 0.0f, forward.x);
	position = road->PositionAt(carDistance) + right * kPlayerLane;
}

void Renderer::StartShot(int index) {
	shotIndex = index;
	shotTime  = 0.0f;
	shotStartCarDistance = carDistance;
}

// Back in auto mode: carry on with the first shot the viewer has not seen yet. When everything has
// been seen the list starts over.
void Renderer::ResumeDirector() {
	int next = -1;
	for (int i = 0; i < kShotCount; ++i) {
		if (!shotSeen[i] && (hasCity || !kShotNeedsCity[i])) {
			next = i;
			break;
		}
	}
	if (next < 0) {
		for (int i = 0; i < kShotCount; ++i) { shotSeen[i] = 0; }
		next = 0;
	}
	StartShot(next);
}

void Renderer::UpdateCinematicCamera(float dt) {
	shotTime += dt;
	if (shotTime > 3.0f) {
		shotSeen[shotIndex] = 1;   // watched long enough to count
	}
	if (shotTime >= kShotLength[shotIndex]) {
		int next = (shotIndex + 1) % kShotCount;
		while (kShotNeedsCity[next] && !hasCity) {
			next = (next + 1) % kShotCount;
		}
		StartShot(next);
	}

	// Cut through black: fade in over the first 0.4 s of a shot, out over the last 0.4 s.
	const float length = kShotLength[shotIndex];
	fade = std::max(1.0f - shotTime / 0.4f, 1.0f - (length - shotTime) / 0.4f);
	fade = std::min(std::max(fade, 0.0f), 1.0f);

	EvaluateShot(shotIndex, shotTime, eyePosition, lookTarget, shotFov);
	viewMatrix = Matrix4::BuildViewMatrix(eyePosition, lookTarget);
}

// The camera moves. t = seconds since the shot started.
void Renderer::EvaluateShot(int index, float t, Vector3& eye, Vector3& target, float& fov) const {
	fov = 60.0f;
	Vector3 carPos, fwd, right;
	GetCarFrame(carPos, fwd, right);
	const float s = Smoothstep(0.0f, 1.0f, t / kShotLength[index]);   // eased 0..1 over the shot

	switch (index) {
	case 0: {   // Chase: behind the car, looking down the road
		const Vector3 lane = right * kPlayerLane;
		eye    = road->PositionAt(carDistance - kCamBehind) + lane + Vector3(0.0f, kCamHeight, 0.0f);
		target = road->PositionAt(carDistance + kLookAhead) + lane + Vector3(0.0f, kLookHeight, 0.0f);
		break;
	}
	case 1: {   // Island overview: a slow orbit high above the whole island
		const float a = 0.6f + t * 0.10f;
		eye    = Vector3(std::cos(a) * 950.0f, 380.0f + 60.0f * std::sin(t * 0.4f), std::sin(a) * 950.0f);
		target = Vector3(0.0f, 30.0f, 0.0f);
		fov    = 55.0f;
		break;
	}
	case 2: {   // Descent: swoop down from the hills onto the city
		const Vector3 start = cityCentre + Vector3(330.0f, 230.0f, 300.0f);
		const Vector3 end   = cityCentre + Vector3(95.0f, 28.0f, 85.0f);
		eye    = Mix(start, end, s);
		target = cityCentre + Vector3(0.0f, 30.0f, 0.0f);
		break;
	}
	case 3: {   // Roadside: a fixed camera by the road, the car drives past it
		const float base = shotStartCarDistance + 95.0f;
		const Vector3 d  = road->DirectionAt(base);
		const Vector3 r(-d.z, 0.0f, d.x);
		eye    = road->PositionAt(base) + r * (kPlayerLane + 11.0f) + Vector3(0.0f, 1.8f, 0.0f);
		target = carPos + Vector3(0.0f, 1.0f, 0.0f);
		break;
	}
	case 4: {   // Fly-through: gliding down the main street of the city
		const float base = cityRoadDistance - 140.0f + t * 20.0f;
		eye    = road->PositionAt(base) + Vector3(0.0f, 12.0f + 4.0f * s, 0.0f);
		target = road->PositionAt(base + 60.0f) + Vector3(0.0f, 10.0f, 0.0f);
		break;
	}
	case 5: {   // Tilt up: standing in the street, looking up at the towers
		const float base = cityRoadDistance + 30.0f;
		const Vector3 d  = road->DirectionAt(base);
		const Vector3 r(-d.z, 0.0f, d.x);
		eye    = road->PositionAt(base) + r * 9.0f + Vector3(0.0f, 1.7f, 0.0f);
		target = cityCentre + Vector3(0.0f, MixF(10.0f, 110.0f, s), 0.0f);
		fov    = 70.0f;
		break;
	}
	case 6: {   // Bonnet: first person from the car
		eye    = carPos + fwd * 0.6f + Vector3(0.0f, 1.5f, 0.0f);
		target = carPos + fwd * 40.0f + Vector3(0.0f, 1.0f, 0.0f);
		fov    = 70.0f;
		break;
	}
	default: {  // Drone: circling the car
		const float a = t * 0.55f;
		eye    = carPos + Vector3(std::cos(a) * 16.0f, 7.0f + 2.0f * std::sin(t), std::sin(a) * 16.0f);
		target = carPos + Vector3(0.0f, 1.0f, 0.0f);
		break;
	}
	}
}

// ---- Post-processing ------------------------------------------------------------------------

// The scene is drawn into a floating-point colour target, so lights can be brighter than 1.0 and the
// bloom pass can find them. R11G11B10F is 32 bits per pixel (as cheap to write as a normal 8-bit
// target, half the memory traffic of RGBA16F) and still stores values above 1.
// Bloom works at a quarter of the scene resolution: it is blurry anyway.
void Renderer::CreatePostTargets() {
	DestroyPostTargets();

	sceneWidth  = std::max(static_cast<int>(width  * kRenderScales[renderScaleIndex]), 1);
	sceneHeight = std::max(static_cast<int>(height * kRenderScales[renderScaleIndex]), 1);

	glGenTextures(1, &hdrColour);
	glBindTexture(GL_TEXTURE_2D, hdrColour);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, sceneWidth, sceneHeight, 0, GL_RGB, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenRenderbuffers(1, &hdrDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, hdrDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, sceneWidth, sceneHeight);

	glGenFramebuffers(1, &hdrFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColour, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hdrDepth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cout << "Renderer: HDR framebuffer is not complete!\n";
	}

	bloomWidth  = std::max(sceneWidth / 4, 1);
	bloomHeight = std::max(sceneHeight / 4, 1);
	glGenTextures(2, bloomTex);
	glGenFramebuffers(2, bloomFBO);
	for (int i = 0; i < 2; ++i) {
		glBindTexture(GL_TEXTURE_2D, bloomTex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, bloomWidth, bloomHeight, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[i], 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::DestroyPostTargets() {
	if (hdrFBO) {
		glDeleteFramebuffers(1, &hdrFBO);
		glDeleteTextures(1, &hdrColour);
		glDeleteRenderbuffers(1, &hdrDepth);
		glDeleteFramebuffers(2, bloomFBO);
		glDeleteTextures(2, bloomTex);
		hdrFBO = hdrColour = hdrDepth = 0;
	}
}

// R: render the scene at 100%, 85% or 70% of the window size and let the composite pass scale it up.
// Costs sharpness, saves fill rate: the fallback if a slower lab GPU misses 60 fps.
void Renderer::CycleRenderScale() {
	renderScaleIndex = (renderScaleIndex + 1) % 3;
	CreatePostTargets();
	std::cout << "Render scale: " << static_cast<int>(kRenderScales[renderScaleIndex] * 100.0f) << "%\n";
}

void Renderer::RenderPostProcessing() {
	StartDebugGroup("Post-processing");
	BeginGpuTimer(PassPost);
	glDisable(GL_DEPTH_TEST);

	if (bloomEnabled) {
		// 1. Bright pass: HDR scene -> quarter-size texture holding only the very bright parts.
		glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
		glViewport(0, 0, bloomWidth, bloomHeight);
		Use(*brightShader);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hdrColour);
		glUniform2f(glGetUniformLocation(activeProgram, "texelSize"), 1.0f / sceneWidth, 1.0f / sceneHeight);
		seaMesh->Draw();

		// 2. Blur: horizontal then vertical, twice (ping-pong between the two bloom textures).
		Use(*blurShader);
		const GLint directionLoc = glGetUniformLocation(activeProgram, "direction");
		for (int pass = 0; pass < 2; ++pass) {
			glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[1]);
			glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
			glUniform2f(directionLoc, 1.0f / bloomWidth, 0.0f);
			seaMesh->Draw();

			glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
			glBindTexture(GL_TEXTURE_2D, bloomTex[1]);
			glUniform2f(directionLoc, 0.0f, 1.0f / bloomHeight);
			seaMesh->Draw();
		}
	}

	// 3. Composite to the screen (this is also where a reduced render scale is scaled back up).
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
	Use(*compositeShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, hdrColour);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
	glUniform1f(glGetUniformLocation(activeProgram, "bloomAmount"), bloomEnabled ? 0.7f : 0.0f);
	glUniform1f(glGetUniformLocation(activeProgram, "vignetteAmount"), vignetteEnabled ? 0.45f : 0.0f);
	glUniform1f(glGetUniformLocation(activeProgram, "fade"), fade);
	seaMesh->Draw();

	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_DEPTH_TEST);
	materialBound = false;
	EndGpuTimer();
	EndDebugGroup();
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
	float dt          = std::min(rawDt, 0.1f);                                  // the simulation never jumps by more than 0.1 s
	lastTick = now;

	// Hold still (screen black) for the first couple of seconds, so a screen recording can start
	// before anything moves and without the first frames of start-up showing.
	if (startupTimer < kStartDelay) {
		startupTimer += rawDt;
		dt = 0.0f;
	}
	elapsedSeconds += dt;

	auto* keys = Window::GetKeyboard();
	if (keys->KeyTriggered(KEYBOARD_C)) { SwitchCameraMode(); }
	if (keys->KeyTriggered(KEYBOARD_F)) { cullingFrozen = !cullingFrozen; }
	if (keys->KeyTriggered(KEYBOARD_H)) { shadowsEnabled = !shadowsEnabled; }
	if (keys->KeyTriggered(KEYBOARD_T)) {
		timePreset = (timePreset + 1) % 5;   // jump to the next time-of-day preset
		timeOfDay  = kTimePresets[timePreset];
		std::cout << "Time of day: " << kTimePresetName[timePreset] << "\n";
	}
	if (keys->KeyTriggered(KEYBOARD_Y)) { timeAuto = !timeAuto; }
	if (keys->KeyTriggered(KEYBOARD_B)) { bloomEnabled = !bloomEnabled; }
	if (keys->KeyTriggered(KEYBOARD_V)) { vignetteEnabled = !vignetteEnabled; }
	if (keys->KeyTriggered(KEYBOARD_R)) { CycleRenderScale(); }
	if (timeAuto) {
		timeOfDay += dt / 6.0f;   // 6 real seconds per hour: a whole day in 144 seconds
		if (timeOfDay >= 24.0f) { timeOfDay -= 24.0f; }
	}

	UpdateCar(dt);   // the world keeps running when the camera is released
	if (cameraMode == CameraMode::Cinematic) {
		UpdateCinematicCamera(dt);
		if (startupTimer < kStartDelay) { fade = 1.0f; }
	}
	else {
		fade    = 0.0f;
		shotFov = 60.0f;
		UpdateFreeCamera(dt);
	}

	projMatrix = Matrix4::Perspective(1.0f, 6000.0f, (float)width / (float)height, shotFov);

	ApplyTimeOfDay();
	if (std::fabs(timeOfDay - lastBakedHour) > 0.04f) {
		BuildEnvironmentMap();   // the car's reflections follow the sky (6 tiny draws; only when the hour moved)
		lastBakedHour = timeOfDay;
	}
	UpdateTraffic(dt);
	UpdateWalkers(dt);   // set the walkers' transforms before the graph recomputes world matrices
	root->Update(dt);   // recomputes every node's world transform from its parent's

	if (!cullingFrozen) {
		cullFrustum.FromMatrix(projMatrix * viewMatrix);
	}

	UpdateLights();
	UpdateShadowMatrix();
	UpdateWindowTitle(rawDt);
}

void Renderer::UpdateCar(float dt) {
	// The road is a closed loop, so the distance just wraps: no jump, no restart.
	carDistance += kCarSpeed * dt;
	if (carDistance > road->GetLength()) {
		carDistance -= road->GetLength();
	}

	const Vector3 dir   = road->DirectionAt(carDistance);
	const Vector3 right(-dir.z, 0.0f, dir.x);
	const Vector3 pos   = road->PositionAt(carDistance) + right * kPlayerLane;
	carNode->SetTransform(
		Matrix4::Translation(pos + Vector3(0.0f, 0.6f, 0.0f)) *
		Matrix4::Rotation(YawFromDirection(dir), Vector3(0, 1, 0)) *
		Matrix4::Rotation(PitchFromDirection(dir), Vector3(1, 0, 0)));
}

void Renderer::UpdateFreeCamera(float dt) {
	// Held left / right mouse button ramps the fly speed. Exponential, so one second of
	// holding changes the speed by the same factor whatever it currently is; clamped, so it
	// can neither crawl to a stop nor overshoot the island. This is the runtime version of
	// editing "msec *= 1.0f" in nclgl/Camera.cpp and rebuilding; that file stays stock.
	auto* mouse = Window::GetMouse();
	if (mouse->ButtonDown(MOUSE_LEFT))  { freeCameraSpeed *= std::exp(-kFlySpeedRate * dt); }
	if (mouse->ButtonDown(MOUSE_RIGHT)) { freeCameraSpeed *= std::exp( kFlySpeedRate * dt); }
	freeCameraSpeed = std::min(std::max(freeCameraSpeed, kFlySpeedMin), kFlySpeedMax);

	camera.UpdateCamera(dt * 1000.0f * freeCameraSpeed);   // nclgl camera takes milliseconds

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
		ResumeDirector();   // continue with a shot the viewer has not seen yet
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

	char title[420];
	std::snprintf(title, sizeof(title),
		"Grand Theft const auto& : Imprecise City | %.0f fps (avg %.0f, low %.0f, high %.0f) | nodes %u/%u | draws %u (+%u shadow) | tris %u | %02d:%02d | GPU ms: shadow %.2f opaque %.2f sky %.2f transp %.2f post %.2f | bloom %s vignette %s | frustum %s | shadows %s | cam %s | fly %.2fx",
		titleFrames / titleTimer, average, low, high,
		stats.nodesDrawn, stats.nodesTotal, stats.drawCalls, stats.shadowDraws, stats.triangles,
		static_cast<int>(timeOfDay), static_cast<int>((timeOfDay - std::floor(timeOfDay)) * 60.0f),
		gpuMs[PassShadow], gpuMs[PassOpaque], gpuMs[PassSky], gpuMs[PassTransparent], gpuMs[PassPost],
		bloomEnabled ? "on" : "off", vignetteEnabled ? "on" : "off",
		cullingFrozen ? "FROZEN" : "camera",
		shadowsEnabled ? "on" : "off",
		cameraMode == CameraMode::Cinematic ? "cinematic" : "free",
		freeCameraSpeed);
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
	bool anyBuildings = false;
	for (SceneObject* node : shadowCasterList) {
		if (node->GetMaterial() == Material::Skinned) {
			anySkinned = true;   // drawn below with the skinning shader
			continue;
		}
		if (node->GetMaterial() == Material::Building) {
			anyBuildings = true;   // drawn below with the instancing shader
			continue;
		}
		const Matrix4 model = node->GetWorldTransform() * Matrix4::Scale(node->GetModelScale());
		glUniformMatrix4fv(shadowModelLoc, 1, false, model.values);
		node->Draw(*this);
		++stats.shadowDraws;
	}

	if (anyBuildings) {
		Use(*buildingShadowShader);
		for (SceneObject* node : shadowCasterList) {
			if (node->GetMaterial() == Material::Building) {
				node->Draw(*this);   // binds the district's instance buffer and draws all its boxes at once
				++stats.shadowDraws;
			}
		}
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
		case Material::Building: Use(*buildingShader); break;
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
	if (node->GetMaterial() == Material::Building) {
		// One draw call, many boxes: count them all.
		stats.triangles += node->GetMesh()->GetTriCount() * static_cast<CityDistrict*>(node)->InstanceCount();   // valid: only CityDistrict uses Material::Building
	}
	else {
		stats.triangles += node->GetMesh()->GetTriCount();
	}
}

void Renderer::DrawNodes(const std::vector<SceneObject*>& list) {
	for (SceneObject* node : list) {
		DrawNode(node);
	}
}

void Renderer::RenderScene() {
	stats = FrameStats();
	UploadFrameData();

	ReadGpuTimers();

	if (shadowsEnabled) {
		BeginGpuTimer(PassShadow);
		RenderShadowPass();
		EndGpuTimer();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);   // the scene is drawn into the HDR target, post-processing puts it on screen
	glViewport(0, 0, sceneWidth, sceneHeight);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	// The shadow map lives on texture unit 4 for the whole main pass (see lighting.glslh).
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, shadowTex);
	glActiveTexture(GL_TEXTURE5);   // the environment cube map lives on unit 5 (see carFragment.glsl)
	glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
	glActiveTexture(GL_TEXTURE6);   // island heights for the sea's shoreline foam (see water.glslh)
	glBindTexture(GL_TEXTURE_2D, shoreTex);
	glActiveTexture(GL_TEXTURE0);
	materialBound = false;

	StartDebugGroup("Scene culling and sorting");
	CullNodes(root.get(), cullFrustum, false);
	SortNodeLists();
	EndDebugGroup();

	StartDebugGroup("Opaque pass");
	BeginGpuTimer(PassOpaque);
	DrawNodes(opaqueList);
	EndGpuTimer();
	EndDebugGroup();

	StartDebugGroup("Sky pass");
	BeginGpuTimer(PassSky);
	DrawSky();
	EndGpuTimer();
	EndDebugGroup();

	StartDebugGroup("Transparent pass");
	BeginGpuTimer(PassTransparent);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	DrawNodes(transparentList);
	glDisable(GL_BLEND);
	EndGpuTimer();
	EndDebugGroup();

	RenderPostProcessing();

	// An unused query object cannot be read, so shadows-off frames must still issue the shadow one.
	if (!shadowsEnabled) {
		BeginGpuTimer(PassShadow);
		EndGpuTimer();
	}
	queriesPrimed = true;
	querySet = 1 - querySet;

	ClearNodeLists();
}
