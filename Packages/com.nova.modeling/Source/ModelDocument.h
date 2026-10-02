#pragma once
#include "ModelMesh.h"

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

		Matrix World() const { return Matrix::CreateScale(Scale) * Matrix::CreateFromQuaternion(Rotation) * Matrix::CreateTranslation(Position); }
	};

	class Document
	{
	public:
		std::vector<Object> Objects;
		std::vector<std::string> Materials;   // 면의 Material 번호 → 이름 (비어 있으면 "Material")
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
		bool Export(const std::string& path, bool selectedOnly, std::string& error) const;

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

		// 요약 (AI 가 결과를 확인하는 데 쓴다): 오브젝트 · 점 · 면 수, 경계 상자, 선택
		nlohmann::json Summary(bool objectsDetail = true);

	private:
		struct Snapshot { std::string Label; std::string Data; };
		std::vector<Snapshot> m_Undo, m_Redo;
		std::string Serialize() const;
		void Deserialize(const std::string& data);
	};

	Document& Doc();
}
