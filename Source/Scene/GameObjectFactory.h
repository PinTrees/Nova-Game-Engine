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
	// 2D Object > Sprites: SpriteRenderer 하나 (sprite = "builtin:Square" 같은 내장 도형 또는 Assets 의 그림)
	static GameObject* CreateSprite(const std::string& name, const std::string& sprite);

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
	static GameObject* CreateLineEffect(bool trail);   // GameObject > Effects > Line · Trail
	static GameObject* CreateVisualEffect(const std::string& assetPath = "");   // GameObject > Effects > Visual Effect
	// 절차적 나무 (Oak 프리셋)
	static GameObject* CreateTree(const std::string& name = "Tree");
	static GameObject* CreateRock(int preset);   // 절차적 바위·절벽 (RockDesc 프리셋, 씨앗은 무작위)
	static GameObject* CreateRockScatter(int preset);   // 바위 흩뿌리기 (영역 안 수백 개, 인스턴싱)
	// 지형 스탬프 (산·분화구 …): 첫 지형 가운데에 지형 너비의 30% 크기로 놓는다
	static GameObject* CreateTerrainStamp(int shape);
	static GameObject* CreateTerrainBiome(const std::string& preset);   // 바이옴 영역 (프리셋 이름)
	static GameObject* CreateWaterBody(int type);                         // 물: 0 바다, 1 호수, 2 강
	static GameObject* CreatePrototypeShape(int shape);                    // 블록아웃 도형 (PrototypeShape: 0 상자 1 계단 2 경사 3 원기둥 4 원뿔 5 아치 벽 6 원호 벽) + Prototype 재질 + Mesh Collider
	static GameObject* CreateSpline(int preset);                           // Spline Container (+ Spline Instantiate): 0 빈 곡선, 1 성벽 (휘기), 2 울타리 (반복), 3 판자 길 (반복), 4 길 (휘기)
	static GameObject* CreateTerrainSpline(int mode);
	// 초대형 열린 월드: World Terrain (size m 정사각) + PCG Volume (Assets/PCG/OpenWorld.pcg — 없으면 프로젝트 모델로 숲 · 초원 · 사막 규칙을 만든다)
	static GameObject* CreateOpenWorld(float size = 32768.0f);
	static GameObject* CreatePCGVolume(const std::string& graphPath = "");                     // 지형 스플라인: 0 도로, 1 협곡, 2 능선
	// 바로 움직일 수 있는 3인칭 캐릭터: 기본 캐릭터 + Character Controller + ThirdPersonController(C#),
	// 씬의 Main Camera 에 Follow Camera. 필요한 패키지(com.nova.starter-assets → cameras)가 없으면 프로젝트에 넣는다.
	// 넣은 직후에는 C# 컴파일이 끝나야 스크립트가 동작한다 (컴포넌트는 지금 붙여 둔다). note = 사용자에게 보일 안내
	static GameObject* CreateThirdPersonCharacter(const std::string& name = "Player", std::string* note = nullptr);
	static GameObject* CreateAnimatedCharacter(const std::string& name = "Character",
		const std::string& modelPath = kDefaultCharacterModel, const std::string& controllerPath = kDefaultCharacterController);
	// Starter Assets 차: Rigidbody + Wheel Collider 4 + CarController (C#), 처음엔 Assets/StarterAssets/Car.prefab 로 저장 (재질도 그 폴더에),
	// 다음부터는 그 프리팹의 인스턴스. Main Camera 의 Follow Camera 가 따라간다. 장면에 넣어 돌려준다
	static GameObject* CreateCar(const std::string& name = "Car", std::string* note = nullptr);
	// Starter Assets 래그돌 표적: 기본 캐릭터 + Ragdoll (꺼짐) + RagdollTarget (C# — 맞으면 쓰러짐), Main Camera 에 RagdollShooter (클릭 = 쏘기).
	// 장면에 넣어 돌려준다
	static GameObject* CreateRagdollTarget(const std::string& name = "Ragdoll Target", std::string* note = nullptr);
	// 내장 메시 오브젝트 (MeshFilter + MeshRenderer, 콜라이더 없음)
	static GameObject* CreatePrimitive(PrimitiveType type, const std::string& name);

private:
	// 모델의 스킨 메쉬마다 Skinned Mesh Renderer 자식을 만든다
	static void AddSkinnedChildren(GameObject* root, const std::string& modelPath);

public:
	// 씬에 넣기 전의 트리에 자식을 붙인다 (씬 목록은 건드리지 않음 — 모델 배치 ModelPlacement)
	static void AttachChild(GameObject* child, GameObject* parent);

private:
	static std::map<PrimitiveType, std::shared_ptr<Mesh>> s_PrimitiveMeshes;
};
