#pragma once
#include "EditorWindow.h"
#include "VfxAsset.h"

namespace ax { namespace NodeEditor { struct EditorContext; } }

// Window > Visual Effect Graph (Unity VFX Graph 편집기).
//  - 시스템마다 Spawn → Initialize Particle → Update Particle → Output 문맥 노드가 세로로 이어지고, 문맥 안에 블록을 쌓는다
//  - Space · 오른쪽 클릭 = 블록 / 시스템 검색 창, 왼쪽 Blackboard (Exposed Property), 오른쪽 Inspector (고른 블록 · 문맥)
//  - 고칠 때마다 장면의 Visual Effect 가 바로 따라온다 (Vfx::SetLive), Ctrl+S 로 저장
//  - GPU Event (부모 시스템의 파티클이 죽을 때) 는 부모 Update 문맥에서 자식 Spawn 으로 가는 선
class VfxGraphWindow : public EditorWindow
{
public:
	VfxGraphWindow();
	~VfxGraphWindow();
	static VfxGraphWindow* Instance() { return s_Instance; }
	static void Open(const std::string& assetPath);
	// 고르기 (nova vfx window --system S --context initialize|update|spawn|output --block i)
	static void Select(int system, int context, int block);
	static void RegisterEditor();   // 창 · VFX Assistant · .vfx 에셋 종류 · nova vfx
	void OnRender() override;

	// 문서 (열린 .vfx 하나)
	const std::string& Path() const { return m_Path; }
	bool Dirty() const { return m_Dirty; }

protected:
	ImGuiWindowFlags ExtraWindowFlags() const override { return ImGuiWindowFlags_MenuBar; }
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	void BeforeBegin() override;

private:
	static VfxGraphWindow* s_Instance;
	ax::NodeEditor::EditorContext* m_Ctx = nullptr;
	bool m_FocusPending = false;     // Open: 다음 Begin 에서 앞으로 (창이 아직 없으면 SetWindowFocus 가 듣지 않는다)
	bool m_NavigatePending = false;  // 새로 연 에셋: 보기를 맞춘다

	std::string m_Path;
	Vfx::Asset m_Asset;
	bool m_Dirty = false;
	uint64_t m_SeenRevision = 0;     // Vfx::Load 의 마지막 판 (CLI · Assistant 가 파일을 고치면 다시 읽는다)
	uint64_t m_LayoutRevision = 1;   // 노드 자리를 다시 넣어야 할 때 오른다
	uint64_t m_AppliedLayout = 0;
	std::vector<Vfx::Asset> m_Undo, m_Redo;
	bool m_EditActive = false;       // 값 끌기 중 (Undo 를 한 번만)

	// 고른 것
	int m_SelSystem = -1;
	int m_SelContext = -1;           // 0 Spawn, 1 Initialize, 2 Update, 3 Output
	int m_SelBlock = -1;
	int m_SelProperty = -1;
	int m_SelOp = -1;                // 고른 연산 노드 (Id)
	int m_SelAttribute = -1;         // 고른 사용자 속성

	// 검색 창 (Space)
	bool m_OpenSearch = false;
	int m_SearchSystem = -1, m_SearchContext = -1;   // 블록을 넣을 곳 (-1 = 새 시스템)
	ImVec2 m_SearchPos = ImVec2(0, 0);
	char m_Search[64] = {};
	int m_SearchCursor = 0;

	std::string m_Status;
	bool m_StatusError = false;

	void Load(const std::string& path);
	void Save();
	void Snapshot();
	void BeginEdit();
	void EndEditIfIdle();
	void Changed(bool layout = false);
	void Undo();
	void Redo();

	void DrawMenuBar();
	void DrawBlackboard(float width, float height);
	void DrawCanvas(float width, float height);
	void DrawInspector(float width, float height);
	void DrawSystem(int s);
	// compact = 노드 안 (s · ctx · block 이 있으면 연산 노드를 이을 핀도), allowBind = 속성 연결 단추 (Inspector)
	bool DrawBlockFields(Vfx::Block& b, const Vfx::BlockDesc& d, bool compact, int s = -1, int ctx = -1, int block = -1, bool allowBind = true);
	void DrawOperator(Vfx::OperatorNode& n);
	void DrawOperatorInspector(Vfx::OperatorNode& n);
	void AddOperator(const std::string& type, ImVec2 canvasPos);
	void RemoveOperator(int id);
	void DrawSearchPopup();
	void AddSystem(const Vfx::System& s, ImVec2 canvasPos);
};
