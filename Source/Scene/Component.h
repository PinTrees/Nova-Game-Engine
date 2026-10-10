#pragma once
#include <nlohmann/json.hpp>
#include "JsonUtility.h"

using json = nlohmann::json;

class GameObject;

class NOVA_API Component
{
private:
	static int nextInstanceId;

protected:
	GameObject* m_pGameObject;
	int m_InstanceId;

protected:
	// Editor
	string	m_InspectorTitleName = "";
	wstring	m_InspectorIconPath = L"";  
	bool	m_InspectorOpened;
	bool	m_Enabled = true;   // Inspector 헤더의 활성 체크박스

public:
	Component();
	virtual ~Component();

	int GetInstanceID() { return m_InstanceId; }

	virtual void Awake()  { }
	virtual void Start()  { }
	// 씬 스트리밍: LoadSceneAsync 가 미리 지은 씬을 바꿔 끼우기 전에 (프레임 예산 안에서) — 처음 Start · 그리기가 만드는 캐시를 미리
	//  (Animator 의 Humanoid 표 · 나무 메시 …). 상태는 바꾸지 않는다 (Start 는 그대로 바꿔 끼울 때)
	virtual void PrewarmStaged() { }	
	virtual void Update() { }
	virtual void LateUpdate()  { }
	virtual void FixedUpdate() { }

	virtual void LastUpdate() { }

	virtual void Render() { }

	// Editor Only
	virtual void _Editor_Render() { }
	// 편집 중 (Play 아님) 프레임마다 — Unity [ExecuteAlways] 의 Update (예: Cinemachine Brain 이 Live 가상 카메라를 바로 보여 준다)
	virtual void _Editor_Update() { }

	virtual void Reset() { }
	void RenderInspectorGUI(); 
	virtual void OnInspectorGUI() { }
	// Unity 스타일 Inspector(UnityGUI)로 본문을 그리는 컴포넌트는 true (기존 스타일 push 를 생략)
	virtual bool UsesUnityInspector() const { return false; }
	// 물리 바디를 바꾸는 컴포넌트 (콜라이더 · Rigidbody · Joint): 인스펙터가 값을 바꾸면 물리 번호를 올린다 (RenderInspectorGUI 가 그리기 앞뒤 값을 비교)
	virtual bool AffectsPhysics() const { return false; }
	// 헤더 아이콘 이름(ProjectSetting/icons/svg/png/<이름>.png). nullptr 이면 타입 기본 아이콘
	virtual const char* InspectorIconName() const { return nullptr; }
	// 헤더 제목 (예: "Cube (Mesh Filter)"). 기본은 m_InspectorTitleName
	virtual std::string InspectorTitle() const { return m_InspectorTitleName; }
	// 헤더에 활성 체크박스를 표시할지 (Transform 은 표시하지 않음)
	virtual bool HasEnabledToggle() const { return true; }
	bool IsEnabled() const { return m_Enabled; }
	void SetEnabled(bool enabled) { if (m_Enabled != enabled) MarkPhysicsDirty(); m_Enabled = enabled; }
	virtual void OnDrawGizmos() { }
	// 복제/프리팹 배치로 GameObject fileID 가 바뀔 때: 옛 ID → 새 ID (복사한 묶음 안을 가리키던 참조를 고친다)
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) { }
	virtual void OnDestroy() { }
	// 오브젝트 (또는 부모) 가 켜지거나 꺼졌다 — Unity 의 GameObject 활성화로 오는 OnEnable / OnDisable.
	//  꺼진 동안은 Update · LateUpdate · FixedUpdate 가 오지 않는다 (소리 멈추기 · 시뮬레이션 내리기는 여기서)
	virtual void OnHierarchyActiveChanged(bool active) { }
public:
	GameObject* GetGameObject() { return m_pGameObject; }

	// 절두체 컬링 (SceneCulling): 추적 중인 렌더러는 CullStamp == SceneCulling::Stamp 일 때만 그린다
	uint32_t CullStamp = 0;
	uint32_t ShadowCullStamp = 0;   // 그림자 (빛) 컬링 — 카메라 결과 (CullStamp) 를 덮지 않게 따로
	bool CullTracked = false;
	uint32_t CullSlot = 0;          // SceneCulling 의 자리 번호 (CullTracked 일 때) — 프레임마다 해시 찾기 없이
	int32_t CullReg = -1;           // SceneCulling 렌더러 목록의 자리 (Mesh Renderer · Skinned Mesh Renderer)
	// Update · LateUpdate 가 비어 있는 컴포넌트 (Transform · 렌더러 · Mesh Filter): 씬 업데이트가 부르지 않는다
	//  (군중: 모듈형 캐릭터 1000 명 = 오브젝트 1 만 7 천 개 — 빈 가상 호출 3 만 4 천 번이 5 ms)
	bool SkipUpdate = false;
	// 컴포넌트 · 메시 연결 번호: 컴포넌트를 GameObject 에 붙이거나 MeshFilter · Mesh Renderer 의 메시를 바꿀 때 +1.
	//  같으면 컬링이 렌더러마다 메시를 다시 찾지 않는다 (SceneCulling::Update — 놓친 경로는 돌아가며 하는 확인이 바로잡는다)
	static uint32_t s_BindingSerial;
	// 물리 번호: 물리 바디가 달라질 수 있는 변경 (오브젝트 만들기 · 지우기 · 켜고 끄기 · 부모 · 레이어, 컴포넌트 지우기 · 켜고 끄기,
	//  콜라이더 · Rigidbody 값) 마다 +1. 같으면 물리 동기화가 씬을 다시 훑지 않고 Transform 이 바뀐 소유자만 본다 (PhysicsManager)
	static uint32_t s_PhysicsSerial;
	static void MarkPhysicsDirty();
	// LOD Group (LODGroup::SelectForView 가 뷰마다 매김): LodStamp == SceneCulling::LodStamp 일 때만 따른다
	uint32_t LodStamp = 0;
	bool LodHidden = false;         // 카메라 패스 (깊이 · 본 · 투명) 에서 안 그림
	bool LodShadowHidden = false;   // 그림자 패스에서 안 그림
	bool LodFadeBelow = false;      // 크로스페이드 디더: true = 무늬 < LodFade 인 픽셀만, false = 무늬 ≥ LodFade 인 픽셀만
	float LodFade = 0.0f;           // 0 = 디더 없음

private:
	friend class GameObject;
	void SetGameObject(GameObject* gameObject) { m_pGameObject = gameObject; ++s_BindingSerial; }

public:
	virtual json toJson() const = 0;
	virtual void fromJson(const json& j) = 0;
	virtual std::string GetType() const = 0;
};

