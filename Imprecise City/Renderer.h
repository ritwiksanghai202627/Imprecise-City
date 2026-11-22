#pragma once

#include "../nclgl/OGLRenderer.h"
#include "../nclgl/Camera.h"
#include "../nclgl/Frustum.h"
#include "SceneObject.h"
#include "AnimatedModel.h"
#include "SkinnedObject.h"
#include "RoadPath.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class Terrain;
class RoadPath;

// CPU mirror of the FrameData uniform block in Shaders/frame.glslh (std140).
// Field order and types must match the GLSL block exactly.
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
};
static_assert(sizeof(Matrix4) == 64, "FrameData assumes Matrix4 is 16 floats");
static_assert(sizeof(Vector4) == 16, "FrameData assumes Vector4 is 4 floats");
static_assert(sizeof(FrameData) == 3 * 64 + 6 * 16, "FrameData must match the std140 block in frame.glslh");

// Per-frame counters, shown in the window title.
struct FrameStats {
	unsigned nodesTotal = 0;   // drawable nodes in the scene
	unsigned nodesDrawn = 0;   // ...that survived culling
	unsigned drawCalls  = 0;
	unsigned triangles  = 0;
	unsigned shadowDraws = 0;   // draw calls in the shadow pass
};

// GPU time per pass, measured with GL_TIME_ELAPSED queries. The result of a query is read one
// frame late (two query sets alternate), so reading never stalls the pipeline.
enum GpuPass { PassShadow, PassOpaque, PassSky, PassTransparent, PassCount };

class Renderer : public OGLRenderer {
public:
	Renderer(Window& parent);
	~Renderer() override;

	void UpdateScene(float dt) override;   // dt ignored: we time ourselves so we can add slow-mo later
	void RenderScene() override;

private:
	enum class CameraMode { Cinematic, Free };

	// Scene graph
	void BuildScene();
	void CullNodes(SceneNode* node, Frustum& frustum, bool shadowPass);   // frustum test, then fill the opaque / transparent / shadow-caster lists
	void CreateShadowMap();
	void UpdateShadowMatrix();
	void RenderShadowPass();
	void SortNodeLists();
	void ClearNodeLists();
	void DrawNodes(const std::vector<SceneObject*>& list);
	void DrawNode(SceneObject* node);
	void DrawSkinned(SkinnedObject* node);
	void BindMaterial(Material material);

	// Sky, environment map, characters
	void BuildEnvironmentMap();   // bakes the sky into a cube map (once; again when the sun moves)
	void DrawSky();
	void CreateWhiteTexture();
	void BuildWalkers();
	void UpdateWalkers(float dt);

	// Camera and per-frame
	void   UpdateCinematicCamera();
	void   UpdateFreeCamera(float dt);
	void   SwitchCameraMode();
	void   UpdateCar(float dt);
	void   UploadFrameData();
	void   UpdateWindowTitle(float dt);
	GLuint LoadTexture(const std::string& fileName);
	void   Use(Shader& shader);

	// Camera
	Camera     camera;               // nclgl free-fly camera
	CameraMode cameraMode = CameraMode::Cinematic;
	Vector3    eyePosition;
	Vector3    lookTarget;

	// World data (meshes are owned here or by Terrain; scene nodes only point at them)
	std::unique_ptr<RoadPath> road;
	std::unique_ptr<Terrain>  terrain;
	std::vector<RoadPath::RibbonSegment> roadSegments;
	std::unique_ptr<Mesh>     carMesh;
	std::unique_ptr<Mesh>     seaMesh;
	float carDistance = 0.0f;

	// Animated characters: one shared model, several instances walking along the verge of the road
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

	// Scene graph: declared after the meshes, so it is destroyed before them
	std::unique_ptr<SceneNode> root;
	SceneObject*               carNode = nullptr;
	std::vector<SceneObject*>  opaqueList;
	std::vector<SceneObject*>  transparentList;

	// Culling. The frustum is rebuilt every frame unless frozen (F), so you can fly out
	// and see what the camera that was frozen could not see.
	Frustum cullFrustum;
	bool    cullingFrozen = false;

	// Sun shadow: one orthographic depth map that follows the camera
	Frustum                   shadowFrustum;
	std::vector<SceneObject*> shadowCasterList;
	std::unique_ptr<Shader>   shadowShader;
	GLuint  shadowFBO = 0;

	// GPU pass timers (see GpuPass). [set][pass]; 'set' alternates every frame.
	GLuint queries[2][PassCount] = {};
	float  gpuMs[PassCount]      = {};   // latest smoothed result in milliseconds
	int    querySet              = 0;
	bool   queriesPrimed         = false;   // false until set 0 has been issued once
	void   BeginGpuTimer(GpuPass pass);
	void   EndGpuTimer();
	void   ReadGpuTimers();
	GLuint  shadowTex = 0;
	Matrix4 shadowView, shadowProj;
	float   shadowTexelWorld = 0.0f;   // world size of one shadow texel
	bool    shadowsEnabled = true;     // H toggles
	Vector3 sunDirection;              // unit vector towards the sun

	// Shaders and textures
	std::unique_ptr<Shader> terrainShader, roadShader, seaShader, litShader;
	std::unique_ptr<Shader> carShader, skinnedShader, skinnedShadowShader, skyShader, bakeSkyShader;
	GLuint grassTexture = 0, sandTexture = 0, rockTexture = 0;
	GLuint whiteTexture = 0;           // 1x1 white, bound when a model has no texture
	GLuint envCube = 0;                // sky baked into a cube map, reflected by the car
	Vector3 zenithColour;              // sky colour straight up (horizon colour = fog colour)
	GLuint activeProgram = 0;

	// Material binding cache (uniform locations are looked up once per material switch)
	bool     materialBound = false;
	Material boundMaterial = Material::Terrain;
	GLint    modelLoc  = -1;
	GLint    colourLoc = -1;
	GLint    jointsLoc = -1;   // bone matrix array, skinned material only

	// Frame data
	GLuint frameUBO = 0;
	FrameData frameData;

	// Time and stats
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
