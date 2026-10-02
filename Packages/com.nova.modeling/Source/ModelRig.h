#pragma once
#include "ModelMesh.h"

// 리깅 (4 단계): 아마추어 (본) · 스킨 가중치 · 흔들림 사슬 · 충돌체 · 포즈 미리보기.
//  - 본 = 문서 (월드) 좌표의 머리 → 꼬리. 쉬는 자세의 본 회전은 없다 (월드 축) — glTF / VRM 로 내보낼 때 노드 회전 = 단위
//  - 스킨 가중치 = 본 이름과 같은 버텍스 그룹 (Blender 와 같은 규칙). 연산을 거쳐도 그룹이 따라가고, X 거울은 Left ↔ Right 를 바꿔 단다
//  - Human = Unity HumanBodyBones 이름 (Hips, LeftUpperArm …) → VRM humanoid · 엔진 Humanoid 리타게팅
//  - Spring = 흔들림 본 (머리카락 · 치마) → VRM Spring Bone → 엔진 Dynamic Bone
namespace Modeling
{
	class Document;
	struct Object;

	struct Bone
	{
		std::string Name;
		int Parent = -1;
		Vec3 Head = Vec3(0, 0, 0);
		Vec3 Tail = Vec3(0, 0.1f, 0);
		std::string Human;            // Unity HumanBodyBones 이름 (없으면 일반 본)
		bool Deform = true;
		// 흔들림 사슬 (VRM Spring Bone 값)
		bool Spring = false;
		float Stiffness = 0.8f;
		float Drag = 0.4f;
		float Gravity = 0.05f;
		float HitRadius = 0.02f;
		std::string Chain;            // 같은 사슬 이름 (내보낼 때 한 spring)
		// 포즈 미리보기: 머리를 중심으로 한 회전 (월드 축, 부모 회전 위에) — 저장 · 내보내기 하지 않는 미리보기
		Quaternion Pose = Quaternion::Identity;
		Vec3 PoseEuler = Vec3(0, 0, 0);   // 같은 포즈의 오일러 각 (도, 창 · CLI 표시)
	};

	// 흔들림 본이 피하는 몸 (구 또는 캡슐). Offset · Tail = 본 머리에서 (월드 축)
	struct Collider
	{
		int Bone = -1;
		Vec3 Offset = Vec3(0, 0, 0);
		Vec3 Tail = Vec3(0, 0, 0);
		float Radius = 0.05f;
		bool Capsule = false;
	};

	struct Armature
	{
		std::vector<Bone> Bones;
		std::vector<Collider> Colliders;

		bool Empty() const { return Bones.empty(); }
		int Find(const std::string& name) const;
		int FindHuman(const std::string& human) const;
		bool HasPose() const;
		void ClearPose();
		// 쉬는 자세 월드 점 → 포즈 월드 점 (행 벡터: p * M). 부모가 먼저 나와야 한다 (Sort)
		std::vector<Matrix> SkinMatrices() const;
		// 포즈 적용한 본 머리 · 꼬리 (그리기)
		void PosedSegment(int bone, const std::vector<Matrix>& skin, Vec3& head, Vec3& tail) const;
		// 부모가 자식보다 앞에 오게 (번호가 바뀌면 Parent · Collider 도 고친다)
		void Sort();
		void Remove(int bone);   // 자식은 그 부모에 붙는다

		nlohmann::json ToJson(bool withPose = false) const;
		void FromJson(const nlohmann::json& j);
	};

	// 점 p 에서 선분 ab 까지 거리
	float SegmentDistance(const Vec3& p, const Vec3& a, const Vec3& b, float* t = nullptr);

	// 메시 덩어리 (면으로 이어진 점들) — 덩어리 번호 (점마다)
	int Islands(const Mesh& m, std::vector<int>& islandOf);

	// ---- 연산 (CLI: rig.*) — 결과 JSON 을 report 에, 실패면 error
	// Humanoid 뼈대를 메시에 맞춘다: 오브젝트 · 버텍스 그룹 이름 (Head · Body · ArmL …) 으로 부위를 찾고, 없으면 키 비율 틀
	bool FitHumanoid(Document& d, const nlohmann::json& args, nlohmann::json& report, std::string& error);
	// 자동 가중치: 덩어리마다 가까운 본 후보 → 거리⁻⁴ → 메시 이웃으로 부드럽게 (열 확산 근사)
	bool AutoWeights(Document& d, const nlohmann::json& args, nlohmann::json& report, std::string& error);
	// 흔들림 사슬: 오브젝트 (또는 덩어리 · 방사 조각) 마다 뿌리 → 끝 본 + 가중치
	bool AddChains(Document& d, const nlohmann::json& args, nlohmann::json& report, std::string& error);
	// 몸 충돌체: 머리 = 구, 몸통 · 팔 · 다리 = 캡슐, 굵기 = 그 본 점까지 거리
	bool AutoColliders(Document& d, const nlohmann::json& args, nlohmann::json& report, std::string& error);

	// 메시 그룹 번호 → 본 번호 (본 이름과 같은 그룹, 없으면 -1)
	std::vector<int> GroupToBone(const Armature& arm, const Mesh& mesh);
	// 점의 본 가중치 (본 번호, 가중치) 최대 4 개, 합 1. nearest = 그룹이 없으면 가장 가까운 deform 본 하나 (내보내기), 아니면 비어 있음
	void VertexBones(const Armature& arm, const std::vector<int>& groupToBone, const Vert& v, const Vec3& worldPos, bool nearest, std::vector<std::pair<int, float>>& out);
	// 포즈를 메시에 (모디파이어 결과 위에) — Armature 모디파이어처럼. 그룹이 없는 점은 그대로
	void PoseMesh(const Armature& arm, const std::vector<Matrix>& skin, const Matrix& objectWorld, Mesh& mesh);

	// VRM humanoid 이름 (hips, leftUpperArm …) ↔ Unity 이름
	std::string VrmHumanName(const std::string& unityName);
}
