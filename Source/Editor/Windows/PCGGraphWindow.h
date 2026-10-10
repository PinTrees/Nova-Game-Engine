#pragma once
#include "EditorWindow.h"
#include "PCGGraph.h"
#include <memory>

namespace ax { namespace NodeEditor { struct EditorContext; } }

// Window > PCG Graph (Unreal 의 PCG Graph 편집기).
//  - 노드를 잇는다: Surface Sampler → 거르기 (Density Noise · Height · Slope · Biome · Density Filter) → Self Pruning · Difference →
//    Transform Points → Static Mesh Spawner. 오른쪽 클릭 · Space = 노드 추가, Delete = 지우기, 핀을 끌어 잇기
//  - 오른쪽 Details: 고른 노드의 값 (끄기 · 이름 · 값 · 스포너의 메시 목록 — Project 에서 고른 모델 넣기, 역할로 한꺼번에)
//  - 값을 바꾸면 바로 장면의 PCG Volume 이 그 규칙의 셀을 다시 만든다 (같은 그래프 객체), 놓을 때 .pcg 저장
class PCGGraphWindow : public EditorWindow
{
public:
	PCGGraphWindow();
	~PCGGraphWindow();
	static PCGGraphWindow* Instance() { return s_Instance; }
	static void Open(const std::string& path);
	static void RegisterEditor();   // 창 · .pcg 에셋 종류 · PCG Volume 의 Open Graph
	void OnRender() override;

protected:
	ImGuiWindowFlags ExtraWindowFlags() const override { return ImGuiWindowFlags_MenuBar; }
	ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	void BeforeBegin() override;

private:
	static PCGGraphWindow* s_Instance;
	ax::NodeEditor::EditorContext* m_Ctx = nullptr;
	std::string m_Path;
	std::shared_ptr<PCG::Graph> m_Graph;
	bool m_FocusPending = false;
	bool m_LayoutPending = true;     // 노드 자리를 그래프에서 다시 넣는다
	bool m_SavePending = false;
	int m_Selected = 0;              // 고른 노드 Id
	bool m_OpenAdd = false;
	ImVec2 m_AddAt = ImVec2(0, 0);
	std::vector<std::pair<std::string, std::string>> m_Models;   // 역할 · 모델 (메시 넣기)
	int m_ModelsAge = 0;

	void DrawCanvas(float width, float height);
	void DrawDetails(float width, float height);
	void DrawNode(PCG::Node& n);
	void Save();
};
