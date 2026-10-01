#pragma once
#include <string>
#include <memory>
#include <map>

class GameObject;
class Mesh;

// 엔진에 내장된 기본 메시(프로젝트 폴더와 무관). 씬에는 "builtin:<이름>" 경로로 저장된다.
enum class PrimitiveType
{
	Cube,
	Sphere,
	Capsule,
	Cylinder,
	Plane,
	Quad
};

class GameObjectFactory
{
public:
	static std::shared_ptr<Mesh> GetPrimitiveMesh(PrimitiveType type);

	// "builtin:Cube" 형태의 경로 <-> 기본 메시 (씬 저장/로드에 사용)
	static std::wstring GetBuiltinMeshPath(PrimitiveType type);
	static std::shared_ptr<Mesh> LoadBuiltinMesh(const std::wstring& path);   // 내장 경로가 아니면 nullptr
	static bool IsBuiltinMeshPath(const std::wstring& path);

	static GameObject* CreateEmpty(const std::string& name = "GameObject");
	static GameObject* CreateCube(const std::string& name = "Cube");
	static GameObject* CreateSphere(const std::string& name = "Sphere");
	static GameObject* CreateCapsule(const std::string& name = "Capsule");
	static GameObject* CreateCylinder(const std::string& name = "Cylinder");
	static GameObject* CreatePlane(const std::string& name = "Plane");
	static GameObject* CreateQuad(const std::string& name = "Quad");

	static GameObject* CreateDirectionalLight(const std::string& name = "Directional Light");
	static GameObject* CreatePointLight(const std::string& name = "Point Light");
	static GameObject* CreateSpotLight(const std::string& name = "Spot Light");
	static GameObject* CreateCamera(const std::string& name = "Main Camera");

	// 엔진 패키지의 기본 캐릭터 (Resources/Packages/Character). 루트에 Animation 또는 Animator,
	// 자식에 스킨 메시마다 Skinned Mesh Renderer 를 둔다 (Unity 에서 FBX 를 씬에 끌어다 놓은 구조와 같음).
	static constexpr const char* kDefaultCharacterModel = "Resources\\Packages\\Character\\Model_Unity_Ver1.FBX";
	static constexpr const char* kDefaultCharacterIdle = "Resources\\Packages\\Character\\Animations\\GhostSamurai_APose_Idle.FBX";
	static constexpr const char* kDefaultCharacterController = "Resources\\Packages\\Character\\DefaultCharacter.controller";
	// Animation 컴포넌트(단일 클립)로 재생하는 캐릭터
	static GameObject* CreateCharacter(const std::string& name = "Character",
		const std::string& modelPath = kDefaultCharacterModel, const std::string& clipPath = kDefaultCharacterIdle);
	// Animator 컴포넌트(Animator Controller 상태 머신)로 재생하는 캐릭터 — Unity 에서 모델을 씬에 놓았을 때와 같다
	// Unity 의 GameObject > 3D Object > Terrain: Assets 에 새 TerrainData 를 만들고 Terrain + Terrain Collider 를 붙인다
	static GameObject* CreateTerrain(const std::string& name = "Terrain");
	// Unity 의 GameObject > Volume: Global Volume / Box Volume / Sphere Volume (Local = Is Trigger 콜라이더)
	enum class VolumeShape { Global, Box, Sphere };
	static GameObject* CreateVolume(VolumeShape shape);
	// Unity 의 GameObject > Audio > Audio Source
	static GameObject* CreateAudioSource();
	// Unity 의 GameObject > Effects > Particle System: X 를 -90 도 돌려 원뿔이 위로 향한다
	static GameObject* CreateParticleSystem(const std::string& name = "Particle System");
	// 절차적 나무 (Oak 프리셋)
	static GameObject* CreateTree(const std::string& name = "Tree");
	// 지형 스탬프 (산·분화구 …): 첫 지형 가운데에 지형 너비의 30% 크기로 놓는다
	static GameObject* CreateTerrainStamp(int shape);
	static GameObject* CreateTerrainBiome(const std::string& preset);   // 바이옴 영역 (프리셋 이름)
	static GameObject* CreateWaterBody(int type);                         // 물: 0 바다, 1 호수, 2 강
	static GameObject* CreateTerrainSpline(int mode);                     // 지형 스플라인: 0 도로, 1 협곡, 2 능선
	static GameObject* CreateAnimatedCharacter(const std::string& name = "Character",
		const std::string& modelPath = kDefaultCharacterModel, const std::string& controllerPath = kDefaultCharacterController);

private:
	static GameObject* CreatePrimitive(PrimitiveType type, const std::string& name);
	// 모델의 스킨 메쉬마다 Skinned Mesh Renderer 자식을 만든다
	static void AddSkinnedChildren(GameObject* root, const std::string& modelPath);

public:

private:
	static std::map<PrimitiveType, std::shared_ptr<Mesh>> s_PrimitiveMeshes;
};
