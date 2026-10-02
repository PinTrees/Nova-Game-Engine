#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

// 2D 애니메이터 (Spine 식 뼈대 애니메이션) 문서.
//  - 좌표 = 픽셀 (x 오른쪽, y 위), 회전 = 도 (반시계). 본 = 부모 기준 위치 · 회전 · 크기 · 길이
//  - 슬롯 = 그리는 칸 (그리는 순서 = Slots 순서, 뒤가 앞). 슬롯은 본 하나에 붙고, 첨부 (그림) 중 하나를 보인다
//  - 첨부 (Region) = PNG 그림 한 장, 본 기준 위치 · 회전 · 크기 · 너비 · 높이
//  - 애니메이션 = 본마다 회전 · 이동 · 크기 키 (셋업 자세에서의 차이), 슬롯마다 첨부 바꾸기 · 색 키. 키 사이 = 직선 · 계단 · 부드럽게
//  - Setup 모드 = 셋업 자세를 고친다, Animate 모드 = 지금 시각의 자세를 고치고 키를 찍는다 (Spine 과 같음)
namespace Anim2D
{
	struct Bone
	{
		std::string Name;
		int Parent = -1;
		float X = 0, Y = 0, Rotation = 0, ScaleX = 1, ScaleY = 1, Length = 0;   // 셋업 자세
		// 지금 자세 (Animate 모드 · 키 적용) — 셋업 자세와 같은 뜻 (부모 기준)
		float PX = 0, PY = 0, PR = 0, PSX = 1, PSY = 1;
		// 월드 행렬 (Spine 과 같은 [A B; C D] + 이동)
		float A = 1, B = 0, C = 0, D = 1, WX = 0, WY = 0;
	};

	struct Attachment
	{
		std::string Name;
		std::string Image;          // PNG (프로젝트 상대 또는 절대)
		float X = 0, Y = 0, Rotation = 0, ScaleX = 1, ScaleY = 1;
		float Width = 0, Height = 0;   // 0 = 그림 크기
	};

	struct Slot
	{
		std::string Name;
		int Bone = 0;
		std::vector<Attachment> Attachments;
		std::string SetupAttachment;   // 셋업 자세에서 보이는 첨부 ("" = 없음)
		float Color[4] = { 1, 1, 1, 1 };
		// 지금 (애니메이션 적용)
		std::string Current;
		float PColor[4] = { 1, 1, 1, 1 };
		const Attachment* Find(const std::string& name) const
		{
			for (const Attachment& a : Attachments) if (a.Name == name) return &a;
			return nullptr;
		}
	};

	enum class Curve { Linear = 0, Stepped = 1, Smooth = 2 };

	struct Key
	{
		float Time = 0;
		float V[4] = { 0, 0, 0, 0 };   // 회전: [0], 이동: [0..1], 크기: [0..1], 색: [0..3]
		Curve Ease = Curve::Linear;
	};

	struct BoneTimeline { std::vector<Key> Rotate, Translate, Scale; };
	struct SlotTimeline
	{
		std::vector<std::pair<float, std::string>> Attach;   // 계단 (그 시각부터)
		std::vector<Key> Color;
	};

	struct Animation
	{
		std::string Name = "animation";
		float Length = 1.0f;
		bool Loop = true;
		std::map<std::string, BoneTimeline> Bones;
		std::map<std::string, SlotTimeline> Slots;
		int KeyCount() const;
	};

	class Document
	{
	public:
		std::vector<Bone> Bones;
		std::vector<Slot> Slots;          // 그리는 순서
		std::vector<Animation> Animations;
		int ActiveAnim = -1;
		float Time = 0.0f;
		bool AnimateMode = false;
		int SelectedBone = -1;
		int SelectedSlot = -1;
		std::string Path;
		std::string Name = "skeleton";
		bool Dirty = false;
		uint64 Revision = 1;
		// Animate 모드에서 키 안 찍은 자세가 있다 — 시각 · 애니메이션 · 모드가 바뀔 때까지 둔다 (Spine 처럼)
		bool Posed = false;

		void New();
		void Changed() { ++Revision; Dirty = true; }
		int FindBone(const std::string& name) const;
		int FindSlot(const std::string& name) const;
		int FindAnim(const std::string& name) const;
		Animation* Active() { return ActiveAnim >= 0 && ActiveAnim < (int)Animations.size() ? &Animations[ActiveAnim] : nullptr; }
		std::string UniqueBoneName(const std::string& base) const;
		std::string UniqueSlotName(const std::string& base) const;
		// 부모가 앞에 (번호가 바뀌면 Parent · Slot.Bone · 선택도)
		void SortBones();
		void DeleteBone(int b);   // 자식은 그 부모로, 그 본의 슬롯은 부모로

		// 셋업 자세 → 지금 자세 (Setup 모드, 또는 애니메이션이 없을 때)
		void ResetPose();
		// 활성 애니메이션의 t 를 지금 자세로 (셋업 + 키 차이)
		void ApplyAnimation(const Animation& anim, float t);
		// 지금 자세로 월드 행렬
		void UpdateWorld();
		// 화면 갱신용: 모드에 맞게 자세 → 월드 (키 안 찍은 자세는 둔다)
		void Pose();
		// 시각 · 애니메이션 · 모드가 바뀜: 키 안 찍은 자세는 버리고 키대로
		void Repose() { Posed = false; Pose(); }
		// 렌더 · 내보내기가 다른 시각을 잠깐 그린 뒤 되돌릴 때
		struct PoseState { std::vector<std::array<float, 5>> B; std::vector<std::string> Cur; std::vector<std::array<float, 4>> Col; bool Posed = false, Animate = false; float Time = 0; };
		PoseState SavePose() const;
		void RestorePose(const PoseState& s);
		// 본 월드 → 지역 (부모 역행렬) — 끌어서 옮길 때
		void WorldToParent(int bone, float wx, float wy, float& lx, float& ly) const;
		float WorldRotation(int bone) const;   // 월드 회전 (도)

		// 지금 자세를 t 에 키 (bones 가 비면 모든 본, 슬롯 첨부 · 색도)
		int KeyPose(Animation& anim, float t, const std::vector<std::string>& bones, Curve ease, bool slots);

		nlohmann::json ToJson() const;
		bool FromJson(const nlohmann::json& j, std::string& error);
		bool Save(const std::string& path, std::string& error);
		bool Load(const std::string& path, std::string& error);

		// Undo (문서 스냅숏)
		void PushUndo(const std::string& label);
		bool Undo();
		bool Redo();
		void CancelUndo();
		nlohmann::json Summary();

	private:
		struct Snap { std::string Label, Data; };
		std::vector<Snap> m_Undo, m_Redo;
		std::string Serialize() const;
		void Deserialize(const std::string& s);
	};

	Document& Doc();
	// 프로젝트 상대 경로 → 절대 (UTF-8)
	std::string FullPath(const std::string& path);
}
