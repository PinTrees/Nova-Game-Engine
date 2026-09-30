#pragma once
#include <string>
#include <vector>
#include <set>

class GameObject;
class Scene;

// 프리팹 인스턴스 연결 정보 (인스턴스의 각 GameObject 가 가진다)
struct PrefabLink
{
	std::string Asset;       // .prefab 경로 (프로젝트 기준)
	uint64 Source = 0;       // 프리팹 에셋 안에서 대응하는 오브젝트의 fileID
	bool Root = false;       // 인스턴스의 루트인지
	int Revision = 0;        // 이 인스턴스를 만든 에셋 버전 (오버라이드는 이 버전과 비교해야 에셋 변경이 전파된다, 저장 안 함)
	bool IsValid() const { return !Asset.empty() && Source != 0; }
};

// Unity 의 PrefabUtility.
//
// 에셋(.prefab) = { "nova_prefab": 1, "root": <GameObject JSON> } — 오브젝트 fileID 가 곧 "source ID".
// 인스턴스 = 씬의 GameObject 들이 PrefabLink{에셋, source} 를 가진다. 씬에는 인스턴스의 전체 값과 함께
// 에셋과 다른 속성의 목록(overrides)을 저장한다. 씬을 읽을 때(또는 Undo/Apply 뒤 다시 만들 때) 인스턴스는
// "현재 에셋 값 + overrides 만 인스턴스 값" 으로 다시 만들어지므로, 에셋을 고치면 모든 인스턴스에 반영된다.
//
// 속성 키: "GameObject/name|active|tag|layer|static", "<컴포넌트 타입>#<같은 타입 순번>/<필드>",
//          추가된 컴포넌트 "+<타입>#<순번>", 제거된 컴포넌트 "-<타입>#<순번>".
// 루트의 위치/회전과 이름, Transform 의 월드 값은 항상 인스턴스 값이다 (Unity 와 같이 오버라이드로 보이지 않음).
namespace PrefabUtility
{
	// ---- 에셋 ----
	bool IsPrefabAssetPath(const std::string& path);
	// 오브젝트를 새 프리팹 에셋으로 저장하고, 그 오브젝트를 인스턴스로 연결한다 (Hierarchy → Project 드래그)
	bool SaveAsPrefabAssetAndConnect(GameObject* root, const std::string& assetPath);
	// 프리팹 인스턴스 만들기 (Project → Hierarchy 드래그, 스크립트의 Instantiate)
	GameObject* InstantiatePrefab(const std::string& assetPath, Scene* scene, GameObject* parent = nullptr);
	// 에셋 정보 (Inspector / Project 표시용)
	int CountAssetObjects(const std::string& assetPath);
	// 에셋의 현재 버전 번호 (파일이 바뀌거나 Apply 할 때마다 증가)
	int CurrentRevision(const std::string& assetPath);

	// ---- 인스턴스 ----
	GameObject* GetInstanceRoot(GameObject* go);
	bool IsPartOfPrefabInstance(const GameObject* go);
	// 인스턴스 전체의 오버라이드 목록 ("<오브젝트 이름>: <키>")
	std::vector<std::string> GetOverrideDescriptions(GameObject* instanceRoot);
	// 이 오브젝트에서 오버라이드된 컴포넌트 키 ("Transform#0" 등, GameObject 속성은 "GameObject")
	std::set<std::string> GetOverriddenComponentKeys(GameObject* go);
	// Overrides ▾ > Apply All: 인스턴스 값을 에셋에 쓰고 모든 인스턴스를 다시 만든다
	bool ApplyAll(GameObject* instanceRoot);
	// Overrides ▾ > Revert All: 오버라이드를 버리고 에셋 값으로
	void RevertAll(GameObject* instanceRoot);
	// Prefab > Unpack Completely: 연결을 끊어 일반 오브젝트로
	void UnpackCompletely(GameObject* instanceRoot);

	// ---- 직렬화 (GameObject 의 to_json / from_json 이 부른다) ----
	// to_json: 이 오브젝트의 오버라이드 목록 계산 (json 은 이 오브젝트 하나의 직렬화 결과)
	std::vector<std::string> ComputeOverrides(const json& objectJson, const PrefabLink& link);
	// from_json 앞: 인스턴스 루트 JSON 을 현재 에셋 값 + 오버라이드로 다시 조립해야 하면 true
	bool NeedsMerge(const json& objectJson);
	json MergeWithAsset(const json& instanceRootJson);
}
