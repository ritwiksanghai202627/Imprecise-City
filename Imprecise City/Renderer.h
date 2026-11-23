#pragma once

#include "../nclgl/OGLRenderer.h"
#include "../nclgl/Camera.h"
#include "../nclgl/Frustum.h"
#include "SceneObject.h"
#include "AnimatedModel.h"
#include "SkinnedObject.h"
#include "RoadPath.h"
#include "City.h"
#include "CityDistrict.h"
#include "DebugOverlay.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class Terrain;
class RoadPath;

constexpr int kMaxPointLights = 8;   // street lamps nearest the camera (the renderer picks them each frame)
constexpr int kMaxSpotLights  = 2;   // the car's headlights

struct GpuPointLight {
	Vector4 posRadius;
	Vector4 colour;
};
struct GpuSpotLight {
	Vector4 posRange;
	Vector4 dirCosInner;
	Vector4 colourCosOuter;
};

struct FrameData {
	Matrix4 viewMatrix;
	Matrix4 projMatrix;
	Matrix4 shadowViewProj;
	Vector4 cameraPos;
	Vector4 sunDir;
	Vector4 sunColour;
	Vector4 fogColour;
	Vector4 timeInfo;
	Vector4 shadowParams;
	Vector4 environment;   // x = night factor, y = point lights used, z = spot lights used, w = ambient scale
	GpuPointLight pointLights[kMaxPointLights];
	GpuSpotLight  spotLights[kMaxSpotLights];
	// Second shadow cascade, appended at the end (see frame.glslh).
	Matrix4 shadowViewProj2;   // far cascade; 'shadowViewProj' above is the near one
	Vector4 shadowCascade;     // x = normal offset (world units), y = depth bias, both for the near cascade
};
static_assert(sizeof(Matrix4) == 64, "FrameData assumes Matrix4 is 16 floats");
static_assert(sizeof(Vector4) == 16, "FrameData assumes Vector4 is 4 floats");
static_assert(sizeof(FrameData) == 3 * 64 + 7 * 16 + kMaxPointLights * 32 + kMaxSpotLights * 48 + 64 + 16, "FrameData must match the std140 block in frame.glslh");

struct FrameStats {
	unsigned nodesTotal = 0;   // drawable nodes in the scene
	unsigned nodesDrawn = 0;   // ...that survived culling
	unsigned drawCalls  = 0;
	unsigned triangles  = 0;
	unsigned shadowDraws = 0;   // draw calls in the shadow pass
};

enum GpuPass { PassShadow, PassOpaque, PassSky, PassTransparent, PassPost, PassCount };

class Renderer : public OGLRenderer {
public:
	Renderer(Window& parent);
	~Renderer() override;

	void UpdateScene(float dt) override;   // dt ignored: we time ourselves so we can add slow-mo later
	void RenderScene() override;

private:
	enum class CameraMode { Drive, Free, Trailer };   // C cycles: chase the car -> free fly -> trailer (director)

	void BuildScene();
	void CullNodes(SceneNode* node, Frustum& frustum, bool shadowPass);   // frustum test, then fill the opaque / transparent / shadow-caster lists
	void CreateShadowMap();
	void UpdateShadowMatrix();
	void FitShadowCascade(int cascade, const Vector3& forward, float halfSize);
	void RenderShadowPass();                   // draws every cascade
	void RenderShadowCascade(int cascade);     // one depth pass into one layer of the shadow array
	void SortNodeLists();
	void ClearNodeLists();
	void DrawNodes(const std::vector<SceneObject*>& list);
	void DrawNode(SceneObject* node);
	void DrawSkinned(SkinnedObject* node);
	void BindMaterial(Material material);

	void BuildEnvironmentMap();   // bakes the sky into a cube map (once; again when the sun moves)
	void DrawSky();
	void CreateWhiteTexture();
	void BuildWalkers();
	void UpdateWalkers(float dt);

	void   UpdateCinematicCamera(float dt);   // the shot director (see EvaluateShot)
	void   StartShot(int index);
	void   ResumeDirector();                  // back to auto mode: continue with the next shot not yet seen
	void   EvaluateShot(int index, float t, Vector3& eye, Vector3& target, float& fov) const;
	void   GetCarFrame(Vector3& position, Vector3& forward, Vector3& right) const;

	void   CreatePostTargets();
	void   DestroyPostTargets();
	void   CycleRenderScale();
	void   BuildShoreTexture();   // island height map for the sea shader (shore foam, shallow water)
	void   RenderPostProcessing();
	void   UpdateFreeCamera(float dt);
	void   SwitchCameraMode();
	void   UpdateCar(float dt);
	void   UploadFrameData();
	void   ApplyTimeOfDay();      // sun, moon, sky and fog colours for 'timeOfDay'
	void   UpdateSeason(float dt);   // moves the weather state towards the current season's targets
	void   ApplyVSync();          // swap interval from 'vsyncMode'
	void   UpdateLights();        // street lamps near the camera + car headlights -> frame UBO
	void   BuildCity(const Vector3& centre, float roadDistance);
	void   BuildOutskirts();      // trees + village (Outskirts.h)
	void   AddInstancedPatches(const std::vector<CityInstance>& all, ProceduralMesh* mesh, Material material);   // groups instances into culled 192 m patches
	void   BuildTraffic();
	void   UpdateTraffic(float dt);
	void   UpdateWindowTitle(float dt);
	void   DrawOverlay();         // debug text on top of the finished frame (O, K, I)
	GLuint LoadTexture(const std::string& fileName);
	void   Use(Shader& shader);

	static const int kShotCount = 14;
	int    shotIndex = 0;
	float  shotTime  = 0.0f;
	float  shotStartCarDistance = 0.0f;   // where the car was when the shot began (some shots wait for it)
	char   shotSeen[kShotCount] = {};
	float  shotFov   = 60.0f;
	float  fade      = 0.0f;              // 0..1 black, drives the composite pass
	float  startupTimer = 0.0f;           // screen black at start for recording
	float  shoreAngle[2]  = { 0.9f, 3.9f };
	float  shoreRadius[2] = { 800.0f, 800.0f };
	void   FindShores();
	bool   hasCity   = false;
	Vector3 cityCentre;
	float   cityRoadDistance = 0.0f;

	GLuint hdrFBO = 0, hdrColour = 0, hdrDepth = 0;
	GLuint bloomFBO[2] = {}, bloomTex[2] = {};
	int    sceneWidth = 0, sceneHeight = 0;    // size of the HDR target = window size x renderScale
	int    bloomWidth = 0, bloomHeight = 0;
	int    renderScaleIndex = 0;               // R cycles 100% / 85% / 70% (a safety net for slow GPUs)
	std::unique_ptr<Shader> brightShader, blurShader, compositeShader;
	bool   bloomEnabled    = true;   // B toggles
	bool   vignetteEnabled = true;   // V toggles (vignette + colour grade)
	bool   lensEnabled     = true;   // L toggles (chromatic aberration + film grain, in the composite pass)
	bool   verboseTitle    = false;  // I adds the GPU / CPU timings to the overlay

	std::unique_ptr<DebugOverlay> overlay;
	bool   overlayEnabled = false;
	bool   keysPanel      = false;
	float  displayFps = 0.0f, displayAvg = 0.0f, displayLow = 0.0f;   // refreshed twice a second with the title

	Camera     camera;               // nclgl free-fly camera
	CameraMode cameraMode = CameraMode::Drive;
	float      freeCameraSpeed = 1.0f;   // starts at nclgl speed; mouse buttons change it
	Vector3    eyePosition;
	Vector3    lookTarget;

	// World data (meshes are owned here or by Terrain; scene nodes only point at them)
	std::unique_ptr<RoadPath> road;
	std::unique_ptr<Terrain>  terrain;
	std::vector<RoadPath::RibbonSegment> roadSegments;
	std::unique_ptr<Mesh>     carMesh;
	std::unique_ptr<City>            city;
	std::unique_ptr<ProceduralMesh>  unitBox;       // shared by every building / lamp instance
	std::unique_ptr<ProceduralMesh>  pineMesh, broadleafMesh, roofMesh;   // trees and village roofs (Outskirts.h)
	std::vector<GLuint>              citySSBOs;     // one instance buffer per district
	std::unique_ptr<Mesh>     seaMesh;
	float carDistance = 0.0f;

	struct TrafficCar {
		SceneObject* node;
		float distance;    // metres along the road
		float speed;       // metres per second
		float direction;   // +1 with the road's direction, -1 against it
		float lane;        // sideways offset from the centre line (metres, + = right of the road's direction)
	};
	std::vector<TrafficCar> traffic;

	float timeOfDay      = 8.5f;    // starts on a bright morning
	bool  timeAuto       = true;    // the clock runs by default (Y stops it)
	int   timePreset     = 0;
	int   season         = 0;       // starts in the rain
	bool  seasonAuto     = true;
	float seasonTimer    = 0.0f;    // seconds spent in the current season
	float wetness = 0.0f, snowCover = 0.0f, autumnAmount = 0.0f, rainFall = 0.0f, snowFall = 0.0f, overcast = 0.0f;
	int   vsyncMode      = 2;       // X cycles: 0 off, 1 on, 2 adaptive
	float lastBakedHour  = -100.0f;   // the sky was last baked into the cube map at this hour
	std::vector<std::pair<float, int>> lampScratch;   // reused every frame: (distance^2, lamp index)

	struct Walker {
		SkinnedObject* node;
		float distance;    // metres along the road
		float direction;   // +1 or -1
		float minDistance, maxDistance;
		float side;        // sideways offset from the road centre line (metres, + = right)
		float speed;       // metres per second
	};
	std::unique_ptr<AnimatedModel> walkerModel;
	std::vector<Walker>            walkers;

	std::unique_ptr<SceneNode> root;
	SceneObject*               carNode = nullptr;
	std::vector<SceneObject*>  opaqueList;
	std::vector<SceneObject*>  transparentList;

	Frustum cullFrustum;
	bool    cullingFrozen = false;

	static const int          kShadowCascades = 2;
	Frustum                   shadowFrustum[kShadowCascades];
	std::vector<SceneObject*> shadowCasterList;
	std::unique_ptr<Shader>   shadowShader;
	GLuint  shadowFBO = 0;

	GLuint queries[2][PassCount] = {};
	GLuint frameStamps[2][2] = {};
	float  gpuFrameMs = 0.0f;     // GPU time from the first to the last command of the frame (smoothed)
	float  cpuUpdateMs = 0.0f;    // CPU time inside UpdateScene (smoothed)
	float  cpuRenderMs = 0.0f;    // CPU time inside RenderScene = command submission + any driver waits (smoothed)
	float  gpuMs[PassCount]      = {};   // latest smoothed result in milliseconds
	int    querySet              = 0;
	bool   queriesPrimed         = false;   // false until set 0 has been issued once
	void   BeginGpuTimer(GpuPass pass);
	void   EndGpuTimer();
	void   ReadGpuTimers();
	GLuint  shadowTex = 0;
	Matrix4 shadowView[kShadowCascades], shadowProj[kShadowCascades];
	float   shadowTexelWorld[kShadowCascades] = {};   // world size of one shadow texel, per cascade
	bool    shadowsEnabled = true;     // H toggles
	Vector3 sunDirection;              // unit vector towards the sun

	std::unique_ptr<Shader> terrainShader, roadShader, seaShader, litShader;
	std::unique_ptr<Shader> buildingShader, buildingShadowShader;
	std::unique_ptr<Shader> treeShader, treeShadowShader;
	std::unique_ptr<Shader> carShader, skinnedShader, skinnedShadowShader, skyShader, bakeSkyShader;
	GLuint grassTexture = 0, sandTexture = 0, rockTexture = 0;
	GLuint whiteTexture = 0;           // 1x1 white, bound when a model has no texture
	GLuint shoreTex = 0;               // terrain height over the whole island, unit 6 (see water.glslh)
	GLuint envCube = 0;                // sky baked into a cube map, reflected by the car
	GLuint envFBO  = 0;                // framebuffer the sky is baked through (created once, reused by every re-bake)
	Vector3 zenithColour;              // sky colour straight up (horizon colour = fog colour)
	GLuint activeProgram = 0;

	bool     materialBound = false;
	Material boundMaterial = Material::Terrain;
	GLint    modelLoc  = -1;
	GLint    colourLoc = -1;
	GLint    jointsLoc = -1;   // bone matrix array, skinned material only

	GLuint frameUBO = 0;
	FrameData frameData;

	std::chrono::steady_clock::time_point lastTick;
	float      elapsedSeconds = 0.0f;
	FrameStats stats;
	float      titleTimer  = 0.0f;
	int        titleFrames = 0;
	unsigned long fpsFrames = 0;       // lifetime fps stats (after warm-up)
	double     fpsTotalTime = 0.0;
	float      worstFrame = 0.0f;
	float      bestFrame  = 1e9f;
};
