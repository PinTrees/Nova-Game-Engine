#pragma once
#include "ModelMesh.h"
#include "ModelRig.h"

// 모델 편집기 문서: 오브젝트 (이름 + 변환 + 편집 메시) 목록, 모드, Undo.
//  - Object 모드 = 오브젝트를 고르고 옮긴다, Edit 모드 = 활성 오브젝트의 점 · 변 · 면을 고친다 (Blender 와 같음)
//  - 저장 형식 .nmodel (JSON). 가져오기 = Assimp (FBX · OBJ · glTF · DAE …), 내보내기 = FBX(바이너리) · OBJ · GLB (ModelExport.cpp)
//  - 편집기 창과 CLI(nova model …) 가 같은 문서를 다룬다 (Doc())
namespace Modeling
{
	struct Object
	{
		std::string Name = "Object";
		Mesh M;
		Vec3 Position = Vec3(0, 0, 0);
		Quaternion Rotation = Quaternion::Identity;
		Vec3 Scale = Vec3(1, 1, 1);
		bool Visible = true;
		bool Selected = false;   // Object 모드 선택

		// ---- 모디파이어 (Blender 처럼 원래 메시는 그대로, 그림 · 내보내기 · 비교는 결과 메시)
		bool MirrorX = false;    // X 거울 (로컬 X = 0 에서 용접) — 반쪽만 만들면 나머지가 늘 따라온다
		bool MirrorClip = true;  // 가운데 점이 X = 0 을 넘어가지 않게 (옮겨도 0 에 남음)
		int Subsurf = 0;         // Subdivision Surface 미리보기 단계 (0 = 끔, 최대 3)
		bool HasModifiers() const { return MirrorX || Subsurf > 0; }

		// ---- 셰이프 키 (표정): 미리보기 값 · 고치는 셰이프 (Blender 의 Active Shape Key — 이때 옮기기 · 돌리기 · 크기는 그 셰이프의 차이를 고친다)
		std::vector<float> ShapeValues;   // M.Shapes 번호 → 0..1
		int ShapeEdit = -1;               // -1 = Basis
		bool ShapesActive() const;
		std::vector<float> ShapeWeights() const;   // 고치는 셰이프 = 1, 나머지 = 미리보기 값
		// 셰이프 없이 모디파이어만 (내보내기 · 굽기 — 셰이프는 모프 타깃으로 따로)
		Mesh RestEvaluated() const;
		// 결과 메시 (Revision 이 바뀌면 다시 만든다). 면의 Origin = 원래 면 번호
		const Mesh& Evaluated(uint64 revision) const;
		void ApplyModifiers();   // 결과를 원래 메시로 굽고 모디파이어를 끈다

		Matrix World() const { return Matrix::CreateScale(Scale) * Matrix::CreateFromQuaternion(Rotation) * Matrix::CreateTranslation(Position); }

	private:
		mutable Mesh m_Eval;
		mutable uint64 m_EvalRevision = 0;
		mutable bool m_EvalValid = false;
	};

	// 기준 그림 (Blender 의 Reference / Background Image): 앞 · 옆 그림을 그 시점의 뒤에 깔고, 모양을 맞춰 본다 (compare)
	//  - 월드 평면: 중심 Center, 높이 Height (미터), 너비 = 높이 × 그림 비율, 방향 = View 시점의 화면 (front 면 화면 오른쪽 = -X)
	struct RefImage
	{
		std::string Name;
		std::string Path;
		std::string View = "front";     // front back left right top bottom
		Vec3 Center = Vec3(0, 0.85f, 0);
		float Height = 1.7f;
		float Opacity = 0.5f;
		bool Visible = true;
		// 불러온 그림 (저장하지 않음)
		int W = 0, H = 0;
		std::vector<uint32> Pixels;     // RGBA8
		bool Load(std::string& error);
		float Width() const { return H > 0 ? Height * W / (float)H : Height; }
	};

	class Document
	{
	public:
		std::vector<Object> Objects;
		std::vector<std::string> Materials;   // 면의 Material 번호 → 이름 (비어 있으면 "Material")
		std::vector<Vec3> MaterialColors;     // 같은 번호의 색 (툰 미리보기 · FBX Diffuse), 없으면 회색
		Vec3 MaterialColor(int index) const { return index >= 0 && index < (int)MaterialColors.size() ? MaterialColors[index] : Vec3(0.8f, 0.8f, 0.8f); }
		std::vector<RefImage> Refs;           // 기준 그림 (Undo 에 들지 않음)
		Armature Rig;                         // 아마추어 (본 · 충돌체 · 포즈 미리보기)
		std::vector<AnimClip> Clips;          // 애니메이션 클립 (glTF / VRM 로 내보낸다)
		int ActiveClip = -1;
		float AnimTime = 0.0f;                // 지금 보는 시각 (포즈 = 이 시각의 클립)
		AnimClip* ActiveAnim() { return ActiveClip >= 0 && ActiveClip < (int)Clips.size() ? &Clips[ActiveClip] : nullptr; }
		RefImage* FindRef(const std::string& name);
		int Active = -1;
		bool EditMode = false;
		SelectMode Mode = SelectMode::Vertex;
		std::string Path;            // .nmodel (없으면 저장한 적 없음)
		std::string SourcePath;      // 가져온 원본 (FBX 등)
		bool Dirty = false;
		uint64 Revision = 1;         // 바뀔 때마다 + (창이 다시 그린다)

		Object* ActiveObject() { return Active >= 0 && Active < (int)Objects.size() ? &Objects[Active] : nullptr; }
		const Object* ActiveObject() const { return Active >= 0 && Active < (int)Objects.size() ? &Objects[Active] : nullptr; }
		int Find(const std::string& name) const;
		std::string UniqueName(const std::string& base) const;
		void Changed() { ++Revision; Dirty = true; }
		// 그릴 메시 = 모디파이어 결과 + 포즈 (아마추어에 포즈가 있을 때). Revision 이 바뀌면 다시
		const Mesh& Displayed(int objectIndex);
		void New();

		// 오브젝트 하나 더하기 (활성 · 선택), 번호를 돌려준다
		int AddObject(const std::string& name);

		nlohmann::json ToJson() const;
		void FromJson(const nlohmann::json& j);
		bool Save(const std::string& path, std::string& error);
		bool Load(const std::string& path, std::string& error);
		// Assimp 로 읽는다 (append = 지금 문서에 더하기). 노드 변환은 점에 굽는다 (엔진과 같은 축 · 미터)
		bool Import(const std::string& path, bool append, std::string& error);
		// 확장자로 형식 (.fbx · .obj · .glb · .gltf). selectedOnly = Object 모드에서 고른 것만
		// .vrm = GLB + VRM 1.0 (humanoid · spring bone · MToon) — 아마추어 필요. options: title, author, outlineWidth (m)
		bool Export(const std::string& path, bool selectedOnly, std::string& error, const nlohmann::json& options = nlohmann::json::object()) const;

		// ---- Undo (문서 전체 스냅숏, 최대 64) ----
		void PushUndo(const std::string& label);
		bool Undo();
		bool Redo();
		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }
		const std::string& UndoLabel() const;
		const std::string& RedoLabel() const;
		// 마지막 스냅숏으로 되돌리기만 (Undo 목록은 그대로) — 끌어서 조절하는 연산이 매번 처음 상태에서 다시 하도록
		void RestoreLastSnapshot();
		// 마지막 스냅숏으로 되돌리고 목록에서 뺀다 (연산이 실패했을 때)
		void CancelUndo();

		// ---- 체크포인트: 이름 붙인 문서 상태 (AI 가 시도해 보고 되돌릴 때). 되살리기는 Undo 된다
		bool SaveCheckpoint(const std::string& name);
		bool RestoreCheckpoint(const std::string& name);
		bool DeleteCheckpoint(const std::string& name) { return m_Checkpoints.erase(name) > 0; }
		std::vector<std::string> CheckpointNames() const;

		// 요약 (AI 가 결과를 확인하는 데 쓴다): 오브젝트 · 점 · 면 수, 경계 상자, 선택
		nlohmann::json Summary(bool objectsDetail = true);

	private:
		struct Snapshot { std::string Label; std::string Data; };
		std::vector<Snapshot> m_Undo, m_Redo;
		std::map<std::string, std::string> m_Checkpoints;
		std::vector<Mesh> m_Posed;
		std::vector<uint64> m_PosedRevision;
		std::string Serialize() const;
		void Deserialize(const std::string& data);
	};

	Document& Doc();
}
