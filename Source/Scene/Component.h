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
	virtual void Update() { }
	virtual void LateUpdate()  { }
	virtual void FixedUpdate() { }

	virtual void LastUpdate() { }

	virtual void Render() { }

	// Editor Only
	virtual void _Editor_Render() { }
	virtual void _Editor_Update() { }

	virtual void Reset() { }
	void RenderInspectorGUI(); 
	virtual void OnInspectorGUI() { }
	// Unity 스타일 Inspector(UnityGUI)로 본문을 그리는 컴포넌트는 true (기존 스타일 push 를 생략)
	virtual bool UsesUnityInspector() const { return false; }
	// 헤더 아이콘 이름(ProjectSetting/icons/svg/png/<이름>.png). nullptr 이면 타입 기본 아이콘
	virtual const char* InspectorIconName() const { return nullptr; }
	// 헤더 제목 (예: "Cube (Mesh Filter)"). 기본은 m_InspectorTitleName
	virtual std::string InspectorTitle() const { return m_InspectorTitleName; }
	// 헤더에 활성 체크박스를 표시할지 (Transform 은 표시하지 않음)
	virtual bool HasEnabledToggle() const { return true; }
	bool IsEnabled() const { return m_Enabled; }
	void SetEnabled(bool enabled) { m_Enabled = enabled; }
	virtual void OnDrawGizmos() { }
	// 복제/프리팹 배치로 GameObject fileID 가 바뀔 때: 옛 ID → 새 ID (복사한 묶음 안을 가리키던 참조를 고친다)
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) { }
	virtual void OnDestroy() { }
public:
	GameObject* GetGameObject() { return m_pGameObject; }

	// 절두체 컬링 (SceneCulling): 추적 중인 렌더러는 CullStamp == SceneCulling::Stamp 일 때만 그린다
	uint32_t CullStamp = 0;
	bool CullTracked = false;
	uint32_t CullSlot = 0;          // SceneCulling 의 자리 번호 (CullTracked 일 때) — 프레임마다 해시 찾기 없이
	// LOD Group (LODGroup::SelectForView 가 뷰마다 매김): LodStamp == SceneCulling::LodStamp 일 때만 따른다
	uint32_t LodStamp = 0;
	bool LodHidden = false;         // 카메라 패스 (깊이 · 본 · 투명) 에서 안 그림
	bool LodShadowHidden = false;   // 그림자 패스에서 안 그림
	bool LodFadeBelow = false;      // 크로스페이드 디더: true = 무늬 < LodFade 인 픽셀만, false = 무늬 ≥ LodFade 인 픽셀만
	float LodFade = 0.0f;           // 0 = 디더 없음

private:
	friend class GameObject;
	void SetGameObject(GameObject* gameObject) { m_pGameObject = gameObject; }

public:
	virtual json toJson() const = 0;
	virtual void fromJson(const json& j) = 0;
	virtual std::string GetType() const = 0;
};

