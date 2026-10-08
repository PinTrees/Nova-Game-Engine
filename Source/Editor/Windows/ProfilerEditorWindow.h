#pragma once
#include "EditorWindow.h"
#include "Profiler.h"
#include <unordered_map>

// Unity 의 Profiler 창 (Window > Analysis > Profiler, Ctrl+7).
//  - 위: CPU Usage / GPU Usage 모듈. 프레임마다 범주별로 쌓은 막대 그래프 (16.7 ms = 60 FPS, 33.3 ms = 30 FPS 선)
//    그래프를 누르거나 끌면 그 프레임을 고른다. 왼쪽 범례는 고른 프레임의 범주별 ms
//  - 아래: Hierarchy (같은 경로의 구간을 합친 표: Total / Self / Calls / ms) · Timeline (구간 막대, 휠 확대 + 끌어 이동) · GPU
//    오른쪽: 그 프레임의 통계 (드로 콜, 묶음, 삼각형, 컬링 결과, 나무 LOD …)
//  - 창이 열려 있고 Record 가 켜져 있을 때만 모은다 (Profiler::SetCollecting)
class ProfilerEditorWindow
	: public EditorWindow
{
public:
	ProfilerEditorWindow();

	static void Toggle();   // 메뉴 / Ctrl+7
	// CLI (nova window profiler --category hierarchy|timeline|threads|gpu|memory): 아래 보기를 바꾼다 (threads = Timeline + 스레드 줄로 내림)
	static bool SetView(const std::string& name);

	void Update() override;

protected:
	void BeforeBegin() override;
	void PushStyle() override;
	void PopStyle() override;
	void OnRender() override;

public:
	enum { kCpuCats = 6, kGpuCats = 7 };
	struct Summary
	{
		float Cpu[kCpuCats] = {};
		float Gpu[kGpuCats] = {};
		bool GpuReady = false;
	};

private:
	const Profiler::Frame* SelectedFrame() const;
	const Summary& SummaryOf(const Profiler::Frame& frame);
	void DrawToolbar(ImDrawList* dl, ImVec2 p, float w);
	void DrawModule(ImDrawList* dl, ImVec2 p, float w, float h, bool gpu);
	void DrawMemoryModule(ImDrawList* dl, ImVec2 p, float w, float h);
	void DrawMemoryDetails(ImVec2 p, ImVec2 size);
	void DrawDetails(ImDrawList* dl, ImVec2 p, float w, float h);
	void DrawHierarchy(const Profiler::Frame& frame, bool gpu, ImVec2 size);
	void DrawTimeline(ImDrawList* dl, const Profiler::Frame& frame, ImVec2 p, ImVec2 size);
	void DrawStats(ImDrawList* dl, const Profiler::Frame& frame, ImVec2 p, ImVec2 size);

	static ProfilerEditorWindow* s_Instance;
	bool m_Recording = true;
	bool m_FocusNext = false;
	uint64_t m_Selected = 0;    // 0 = 마지막 프레임을 따라간다
	int m_View = 0;             // 0 Hierarchy, 1 Timeline, 2 GPU, 3 Memory
	bool m_ModuleOpen[3] = { true, true, true };   // CPU / GPU / Memory (제목을 눌러 접기)
	float m_TimelineZoom = 1.0f;
	float m_TimelineOffset = 0.0f;   // ms
	float m_TimelineScrollY = 0.0f;  // 줄 (메인 구간 + 스레드 줄) 이 넘치면 세로로
	bool m_ScrollToThreads = false;  // CLI --category threads: 스레드 줄이 보이게
	std::unordered_map<uint64_t, Summary> m_Summaries;
};
