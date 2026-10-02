#pragma once
#include "EditorWindow.h"
#include "ShaderGraph.h"

// Window > Shader Graph — Unity 의 Shader Graph 창.
//  - 왼쪽 Blackboard (속성: + 로 추가, 끌어 캔버스에 놓으면 Property 노드), 가운데 노드 캔버스, 오른쪽 Graph Inspector (Graph Settings · 고른 노드 / 속성)
//  - 캔버스: 오른쪽 클릭 · Space = Create Node (검색), 핀을 끌어 잇기 (빈 곳에 놓으면 Create Node 후 자동으로 이음), Delete = 지우기
//  - Ctrl+S = 저장 + 셰이더 만들기 (오류는 아래 상태 줄), Ctrl+Z / Ctrl+Y
//  - 문서 · 연산은 CLI (nova shadergraph …) 와 같다 (ShaderGraphOps)
namespace ax { namespace NodeEditor { struct EditorContext; } }
class ShaderGraphPreview;

class ShaderGraphWindow : public EditorWindow
{
public:
	ShaderGraphWindow();
	~ShaderGraphWindow();
	static ShaderGraphWindow* Instance() { return s_Instance; }
	static void Focus();
	void OnRender() override;

protected:
	ImGuiWindowFlags ExtraWindowFlags() const override { return ImGuiWindowFlags_MenuBar; }
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	void BeforeBegin() override;

private:
	static ShaderGraphWindow* s_Instance;
	ax::NodeEditor::EditorContext* m_Ctx = nullptr;
	uint64 m_SeenRevision = 0;
	int m_SelectedNode = -1;          // 고른 노드 (Graph Inspector)
	std::string m_SelectedProp;       // 고른 속성 ref
	bool m_OpenCreate = false;
	ImVec2 m_CreatePos = ImVec2(0, 0);    // 캔버스 좌표
	uint64_t m_CreateFromPin = 0;         // 핀을 끌어 빈 곳에 놓았을 때 (새 노드를 이 핀에 잇는다)
	ImVec2 m_MouseCanvas = ImVec2(0, 0);
	char m_Search[64] = {};
	std::string m_Status;
	bool m_StatusError = false;
	bool m_EditActive = false;        // 값 끌기 중 (Undo 를 한 번만)
	std::unique_ptr<ShaderGraphPreview> m_Preview;
	bool m_ShowPreviews = true;       // 노드 미리보기
	bool m_ShowMain = true;           // Main Preview
	char m_PathBuf[128] = {};
	uint64 m_PathRevision = 0;

	void SetStatus(const std::string& s, bool error = false) { m_Status = s; m_StatusError = error; }
	void DrawMenuBar();
	void DrawBlackboard(float width, float height);
	void DrawCanvas(float width, float height);
	void DrawInspector(float width, float height);
	void DrawNode(ShaderGraph::Node& n);
	void DrawMaster();
	void DrawCreatePopup();
	bool ValueEditor(ShaderGraph::Node& n, const ShaderGraph::PortDef& p, float itemWidth);
	void Save();
	void ApplyToSelection();
	void BeginEdit();   // 값 위젯이 처음 바뀔 때 Undo
	void DrawMainPreview(float width);
};

namespace ShaderGraph
{
	// 에디터 쪽 등록: 창 · .shadergraph 에셋 종류 (Create > Shader Graph, 더블클릭 = 열기) · CLI "shadergraph"
	void RegisterEditor();
}
