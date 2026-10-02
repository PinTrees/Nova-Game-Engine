#pragma once
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// VRM (glTF 기반 아바타 형식, VRoid · VRM 컨소시엄) 가져오기 도우미.
//  메시 · 스켈레톤 · 애니메이션은 Assimp 가 .vrm 을 glTF 로 읽는다 (FBXLoader 와 같은 길). 여기서는 glb 의 JSON 에서
//  - 사람 본 매핑 (VRMC_vrm.humanoid / VRM 0.x humanoid) → Humanoid 자동 매핑 대신
//  - 재질 · 그림: 묻힌 그림을 <파일>.Textures/ 에 꺼내고 재질마다 .mat 을 <파일>.Materials/ 에 (Unity 의 Extract Materials)
//  - Spring Bone (VRMC_springBone / VRM 0.x secondaryAnimation) → Dynamic Bone 컴포넌트 JSON (com.nova.animation)
//  좌표: glTF (오른손) → 엔진: FBXLoader 와 같은 ConvertToLeftHanded + Y 180° — 본 로컬 벡터 · 월드 방향 = (-x, y, z)
namespace VrmImport
{
	using json = nlohmann::json;

	// 확장자 .vrm, 또는 VRM 확장이 있는 .glb
	bool IsVrm(const std::wstring& path);
	// VRM 0.x (extensions.VRM): glTF 에서 -Z 를 보고, 스프링 본 오프셋 · 중력은 Unity 좌표 그대로
	bool IsVrm0(const std::wstring& path);
	// glb 의 JSON (+ BIN 덩어리). path 는 절대 또는 프로젝트 상대 (Assets\...)
	bool ReadGlb(const std::wstring& path, json& out, std::vector<uint8_t>* bin = nullptr);
	// Unity HumanBodyBones 이름 (Hips, LeftUpperArm …) → 노드 이름
	std::map<std::string, std::string> HumanBones(const std::wstring& path);
	// 재질 번호 → .mat 프로젝트 상대 경로 (없으면 빈 문자열). 이미 꺼낸 파일은 그대로 둔다 (사용자가 고쳤을 수 있다)
	std::vector<std::wstring> ExtractMaterials(const std::wstring& assetPath);
	// Dynamic Bone 컴포넌트 JSON ({"type":"DynamicBone", "chains":[…], "colliders":[…]}), Spring Bone 이 없으면 null
	json DynamicBoneJson(const std::wstring& path);
	// Expressions 컴포넌트 JSON ({"type":"Expressions", "expressions":[{name, binds:[{renderer, shape, index, weight}], …}]}) — VRM 1.0 expressions / 0.x blendShapeMaster, 없으면 null
	json ExpressionsJson(const std::wstring& path);
	// 라이선스 · 작가 (meta) — 크레딧 표시용
	json Meta(const std::wstring& path);
}
