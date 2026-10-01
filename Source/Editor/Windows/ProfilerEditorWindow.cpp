#include "pch.h"
#include "ProfilerEditorWindow.h"
#include "EditorTheme.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>

ProfilerEditorWindow* ProfilerEditorWindow::s_Instance = nullptr;

namespace
{
	constexpr size_t kMaxFrames = 300;
	constexpr float kBarH = 21.0f;
	constexpr float kModuleH = 128.0f;
	constexpr float kLegendW = 196.0f;
	constexpr float kStatsW = 270.0f;

	const ImU32 kBar = IM_COL32(40, 40, 40, 255);
	const ImU32 kOn = IM_COL32(70, 96, 124, 255);
	const ImU32 kRecordOn = IM_COL32(205, 70, 60, 255);
	const ImU32 kText = IM_COL32(210, 210, 210, 255);
	const ImU32 kDim = IM_COL32(140, 140, 140, 255);
	const ImU32 kPanel = IM_COL32(56, 56, 56, 255);
	const ImU32 kGraphBg = IM_COL32(34, 34, 34, 255);
	const ImU32 kLine = IM_COL32(25, 25, 25, 255);

	// ---------------------------------------------------------------- 범주 (구간 이름 → 범주, 없으면 부모 범주를 물려받는다)
	const char* kCpuNames[ProfilerEditorWindow::kCpuCats] = { "Rendering", "Scripts", "Physics", "Editor", "Present (VSync)", "Others" };
	const ImU32 kCpuColors[ProfilerEditorWindow::kCpuCats] = {
		IM_COL32(111, 200, 91, 255), IM_COL32(77, 143, 224, 255), IM_COL32(232, 154, 64, 255),
		IM_COL32(176, 124, 216, 255), IM_COL32(232, 216, 79, 255), IM_COL32(140, 140, 140, 255) };
	enum { kCpuRendering, kCpuScripts, kCpuPhysics, kCpuEditor, kCpuPresent, kCpuOthers };

	const char* kGpuNames[ProfilerEditorWindow::kGpuCats] = { "Shadows", "Depth / SSAO", "Opaque", "Sky / Transparent", "Post Processing", "UI / ImGui", "Other" };
	const ImU32 kGpuColors[ProfilerEditorWindow::kGpuCats] = {
		IM_COL32(91, 143, 217, 255), IM_COL32(57, 182, 176, 255), IM_COL32(111, 200, 91, 255), IM_COL32(232, 216, 79, 255),
		IM_COL32(217, 112, 91, 255), IM_COL32(176, 124, 216, 255), IM_COL32(140, 140, 140, 255) };
	enum { kGpuShadows, kGpuDepth, kGpuOpaque, kGpuTransparent, kGpuPost, kGpuUI, kGpuOther };

	bool Is(const char* a, const char* b) { return a == b || strcmp(a, b) == 0; }

	int CpuRule(const char* name)
	{
		static std::unordered_map<const char*, int> s_Cache;   // 이름 포인터는 Intern/문자열 상수라 바뀌지 않는다
		if (auto it = s_Cache.find(name); it != s_Cache.end())
			return it->second;
		int c = -1;
		for (const char* n : { "GameView render", "SceneView render", "ImGui Render", "Camera/Light Update", "CullingUpdate", "RenderApplication" })
			if (Is(name, n)) c = kCpuRendering;
		for (const char* n : { "Scripts.BeginFrame", "Scene.Update" })
			if (Is(name, n)) c = kCpuScripts;
		if (Is(name, "Physics.Update")) c = kCpuPhysics;
		for (const char* n : { "Editor Windows", "EditorUpdate", "Undo", "Scripts.Update" })
			if (Is(name, n)) c = kCpuEditor;
		if (Is(name, "Present")) c = kCpuPresent;
		for (const char* n : { "Audio.Update", "UI.Update", "Particles.Update", "Main Thread Tasks" })
			if (Is(name, n)) c = kCpuOthers;
		s_Cache[name] = c;
		return c;
	}

	int GpuRule(const char* name)
	{
		static std::unordered_map<const char*, int> s_Cache;
		if (auto it = s_Cache.find(name); it != s_Cache.end())
			return it->second;
		int c = -1;
		if (Is(name, "Shadows")) c = kGpuShadows;
		if (Is(name, "Depth Prepass") || Is(name, "SSAO")) c = kGpuDepth;
		if (Is(name, "Opaque")) c = kGpuOpaque;
		if (Is(name, "Sky") || Is(name, "Grid") || Is(name, "Particles")) c = kGpuTransparent;
		if (Is(name, "Post Processing")) c = kGpuPost;
		if (Is(name, "ImGui") || Is(name, "UI")) c = kGpuUI;
		s_Cache[name] = c;
		return c;
	}

	// 구간마다 범주와 자기 시간(자식 제외)
	template <class S>
	void Attribute(const std::vector<S>& samples, int (*rule)(const char*), int fallback, std::vector<int>& cats, std::vector<float>& self)
	{
		const size_t n = samples.size();
		cats.assign(n, fallback);
		self.assign(n, 0.0f);
		std::vector<size_t> stack;
		for (size_t i = 0; i < n; ++i)
		{
			const S& s = samples[i];
			while (stack.size() > s.Depth)
				stack.pop_back();
			const int r = rule(s.Name);
			cats[i] = r >= 0 ? r : (stack.empty() ? fallback : cats[stack.back()]);
			self[i] = s.Ms;
			if (!stack.empty())
				self[stack.back()] -= s.Ms;
			stack.push_back(i);
		}
	}

	// ---------------------------------------------------------------- 표 (같은 경로의 구간을 합친다)
	struct Node
	{
		const char* Name = "";
		float Total = 0.0f, Self = 0.0f;
		int Calls = 0;
		std::vector<int> Children;
	};

	template <class S>
	void BuildTree(const std::vector<S>& samples, std::vector<Node>& nodes)
	{
		nodes.clear();
		nodes.emplace_back();   // 뿌리
		std::vector<int> stack;   // 깊이마다 노드
		for (const S& s : samples)
		{
			while (stack.size() > s.Depth)
				stack.pop_back();
			const int parent = stack.empty() ? 0 : stack.back();
			int found = -1;
			for (int c : nodes[parent].Children)
				if (Is(nodes[c].Name, s.Name)) { found = c; break; }
			if (found < 0)
			{
				found = (int)nodes.size();
				Node node;
				node.Name = s.Name;
				nodes.push_back(node);
				nodes[parent].Children.push_back(found);
			}
			nodes[found].Total += s.Ms;
			nodes[found].Self += s.Ms;
			nodes[found].Calls += 1;
			if (parent != 0)
				nodes[parent].Self -= s.Ms;
			stack.push_back(found);
		}
		for (Node& n : nodes)
			std::sort(n.Children.begin(), n.Children.end(), [&](int a, int b) { return nodes[a].Total > nodes[b].Total; });
	}

	void DrawNode(const std::vector<Node>& nodes, int index, float frameMs)
	{
		const Node& n = nodes[index];
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (n.Children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		else if (frameMs > 0.0f && n.Total >= frameMs * 0.25f)
			flags |= ImGuiTreeNodeFlags_DefaultOpen;   // 뜨거운 경로는 처음부터 펼친다
		const bool open = ImGui::TreeNodeEx(n.Name, flags);
		const float pct = frameMs > 0.0f ? 100.0f * n.Total / frameMs : 0.0f;
		const float selfPct = frameMs > 0.0f ? 100.0f * (std::max)(0.0f, n.Self) / frameMs : 0.0f;
		ImGui::TableNextColumn(); ImGui::Text("%.1f%%", pct);
		ImGui::TableNextColumn(); ImGui::Text("%.1f%%", selfPct);
		ImGui::TableNextColumn(); ImGui::Text("%d", n.Calls);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", n.Total);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", (std::max)(0.0f, n.Self));
		if (open && !n.Children.empty())
		{
			for (int c : n.Children)
				DrawNode(nodes, c, frameMs);
			ImGui::TreePop();
		}
	}

	std::string FormatValue(double v)
	{
		if (std::fabs(v - std::round(v)) > 1e-6)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%.2f", v);
			return buf;
		}
		const long long n = std::llround(v);
		std::string s = std::to_string(n < 0 ? -n : n);
		for (int i = (int)s.size() - 3; i > 0; i -= 3)
			s.insert((size_t)i, ",");
		return n < 0 ? "-" + s : s;
	}

	bool ToolButton(ImDrawList* dl, const char* id, const char* text, float& x, float y, bool on, ImU32 onColor = kOn, const char* tip = nullptr)
	{
		const float w = ImGui::CalcTextSize(text).x + 14.0f;
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, kBarH));
		const bool hovered = ImGui::IsItemHovered();
		if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", tip);
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + kBarH), on ? onColor : (hovered ? IM_COL32(64, 64, 64, 255) : kBar));
		dl->AddText(ImVec2(x + 7.0f, floorf(y + (kBarH - ImGui::GetFontSize()) * 0.5f)), kText, text);
		x += w + 1.0f;
		return clicked;
	}

	float NiceScale(float maxMs)
	{
		for (float s : { 25.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f })
			if (maxMs * 1.05f <= s)
				return s;
		return 2000.0f;
	}
}

ProfilerEditorWindow::ProfilerEditorWindow()
	: EditorWindow("Profiler", ICON_FA_CHART_AREA)
{
	s_Instance = this;
	SetIsOpened(false);   // Window > Analysis > Profiler (Ctrl+7)
}

void ProfilerEditorWindow::Toggle()
{
	if (s_Instance == nullptr)
		return;
	const bool open = !s_Instance->GetIsOpened();
	s_Instance->SetIsOpened(open);
	s_Instance->m_FocusNext = open;
}

void ProfilerEditorWindow::Update()
{
	Profiler::SetCollecting(GetIsOpened() && m_Recording);
	const auto& history = Profiler::History();
	if (m_Selected != 0 && (history.empty() || m_Selected < history.front().Index))
		m_Selected = 0;   // 고른 프레임이 기록에서 밀려났다
	if (m_Summaries.size() > kMaxFrames + 64)
	{
		const uint64_t oldest = history.empty() ? UINT64_MAX : history.front().Index;
		for (auto it = m_Summaries.begin(); it != m_Summaries.end();)
			it = it->first < oldest ? m_Summaries.erase(it) : std::next(it);
	}
}

void ProfilerEditorWindow::BeforeBegin()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(1040.0f, 600.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (m_FocusNext)
	{
		ImGui::SetNextWindowFocus();
		m_FocusNext = false;
	}
}

void ProfilerEditorWindow::PushStyle()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
}

void ProfilerEditorWindow::PopStyle()
{
	ImGui::PopStyleVar();
}

const Profiler::Frame* ProfilerEditorWindow::SelectedFrame() const
{
	const auto& history = Profiler::History();
	if (history.empty())
		return nullptr;
	if (m_Selected == 0)
	{
		// 따라가기: GPU 결과는 몇 프레임 늦게 오므로 GPU 까지 다 찬 가장 최근 프레임
		for (size_t back = 0; back < history.size() && back < 10; ++back)
			if (history[history.size() - 1 - back].GpuMs >= 0.0f)
				return &history[history.size() - 1 - back];
		return &history.back();
	}
	// Index 는 1 씩 늘어난다 (Clear 뒤에도 계속 이어짐) → 바로 찾기
	const uint64_t first = history.front().Index;
	if (m_Selected >= first && m_Selected - first < history.size() && history[(size_t)(m_Selected - first)].Index == m_Selected)
		return &history[(size_t)(m_Selected - first)];
	for (const auto& f : history)
		if (f.Index == m_Selected)
			return &f;
	return &history.back();
}

const ProfilerEditorWindow::Summary& ProfilerEditorWindow::SummaryOf(const Profiler::Frame& frame)
{
	auto [it, inserted] = m_Summaries.try_emplace(frame.Index);
	Summary& s = it->second;
	std::vector<int> cats;
	std::vector<float> self;
	if (inserted)
	{
		Attribute(frame.Cpu, CpuRule, kCpuOthers, cats, self);
		float covered = 0.0f;
		for (size_t i = 0; i < cats.size(); ++i)
		{
			s.Cpu[cats[i]] += (std::max)(0.0f, self[i]);
			if (frame.Cpu[i].Depth == 0)
				covered += frame.Cpu[i].Ms;
		}
		s.Cpu[kCpuOthers] += (std::max)(0.0f, frame.CpuMs - covered);   // 구간 밖 시간
	}
	if (!s.GpuReady && frame.GpuMs >= 0.0f)
	{
		Attribute(frame.Gpu, GpuRule, kGpuOther, cats, self);
		for (size_t i = 0; i < cats.size(); ++i)
			s.Gpu[cats[i]] += (std::max)(0.0f, self[i]);
		s.GpuReady = true;
	}
	return s;
}

void ProfilerEditorWindow::OnRender()
{
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 200.0f || avail.y < 120.0f)
		return;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
	ImDrawList* dl = ImGui::GetWindowDrawList();

	DrawToolbar(dl, p, avail.x);
	float y = p.y + kBarH;
	DrawModule(dl, ImVec2(p.x, y), avail.x, kModuleH, false);
	y += kModuleH + 1.0f;
	DrawModule(dl, ImVec2(p.x, y), avail.x, kModuleH, true);
	y += kModuleH + 1.0f;
	DrawDetails(dl, ImVec2(p.x, y), avail.x, p.y + avail.y - y);

	// ← → = 앞/뒤 프레임 (창에 포커스가 있을 때)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
	{
		const auto& history = Profiler::History();
		if (!history.empty())
		{
			const Profiler::Frame* sel = SelectedFrame();
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && sel->Index > history.front().Index)
				m_Selected = sel->Index - 1;
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && sel->Index < history.back().Index)
				m_Selected = sel->Index + 1;
		}
	}
	ImGui::PopStyleVar();
}

void ProfilerEditorWindow::DrawToolbar(ImDrawList* dl, ImVec2 p, float w)
{
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kBarH), kBar);
	float x = p.x;
	const std::string record = std::string(ICON_FA_CIRCLE) + " Record";
	if (ToolButton(dl, "##record", record.c_str(), x, p.y, m_Recording, kRecordOn, "Record profiling information (only while this window is open)"))
		m_Recording = !m_Recording;
	if (ToolButton(dl, "##clear", "Clear", x, p.y, false))
	{
		Profiler::Clear();
		m_Summaries.clear();
		m_Selected = 0;
	}
	x += 8.0f;

	const auto& history = Profiler::History();
	const Profiler::Frame* sel = SelectedFrame();
	char label[96];
	if (sel)
		snprintf(label, sizeof(label), "Frame: %llu / %llu", (unsigned long long)sel->Index, (unsigned long long)history.back().Index);
	else
		snprintf(label, sizeof(label), "Frame: -");
	dl->AddText(ImVec2(x, floorf(p.y + (kBarH - ImGui::GetFontSize()) * 0.5f)), kText, label);
	x += ImGui::CalcTextSize(label).x + 8.0f;
	if (ToolButton(dl, "##prev", ICON_FA_CARET_LEFT, x, p.y, false, kOn, "Previous frame (Left Arrow)") && sel && sel->Index > history.front().Index)
		m_Selected = sel->Index - 1;
	if (ToolButton(dl, "##next", ICON_FA_CARET_RIGHT, x, p.y, false, kOn, "Next frame (Right Arrow)") && sel && sel->Index < history.back().Index)
		m_Selected = sel->Index + 1;
	if (ToolButton(dl, "##current", "Current", x, p.y, m_Selected == 0, kOn, "Follow the latest frame"))
		m_Selected = 0;

	const char* hint = m_Recording ? "Recording - last 300 frames" : "Paused";
	const float hw = ImGui::CalcTextSize(hint).x;
	dl->AddText(ImVec2(p.x + w - hw - 10.0f, floorf(p.y + (kBarH - ImGui::GetFontSize()) * 0.5f)), kDim, hint);
}

void ProfilerEditorWindow::DrawModule(ImDrawList* dl, ImVec2 p, float w, float h, bool gpu)
{
	const int cats = gpu ? kGpuCats : kCpuCats;
	const char* const* names = gpu ? kGpuNames : kCpuNames;
	const ImU32* colors = gpu ? kGpuColors : kCpuColors;
	const auto& history = Profiler::History();
	const Profiler::Frame* sel = SelectedFrame();

	// ---- 왼쪽: 모듈 이름 + 범례 (고른 프레임의 범주별 ms)
	dl->AddRectFilled(p, ImVec2(p.x + kLegendW, p.y + h), kPanel);
	dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + h), kLine);
	dl->AddLine(ImVec2(p.x + kLegendW, p.y), ImVec2(p.x + kLegendW, p.y + h), kLine);
	dl->AddText(ImVec2(p.x + 8.0f, p.y + 4.0f), kText, gpu ? "GPU Usage" : "CPU Usage");
	if (sel)
	{
		char total[32];
		if (gpu && sel->GpuMs < 0.0f)
			snprintf(total, sizeof(total), "pending");
		else
			snprintf(total, sizeof(total), "%.2f ms", gpu ? sel->GpuMs : sel->CpuMs);
		dl->AddText(ImVec2(p.x + kLegendW - 8.0f - ImGui::CalcTextSize(total).x, p.y + 4.0f), kText, total);
	}
	const Summary* selSummary = sel ? &SummaryOf(*sel) : nullptr;
	for (int c = 0; c < cats; ++c)
	{
		const float ly = p.y + 24.0f + c * 14.5f;
		dl->AddRectFilled(ImVec2(p.x + 10.0f, ly + 3.0f), ImVec2(p.x + 19.0f, ly + 12.0f), colors[c]);
		dl->AddText(ImVec2(p.x + 25.0f, ly), kDim, names[c]);
		if (selSummary && (!gpu || selSummary->GpuReady))
		{
			char v[32];
			snprintf(v, sizeof(v), "%.2fms", gpu ? selSummary->Gpu[c] : selSummary->Cpu[c]);
			dl->AddText(ImVec2(p.x + kLegendW - 8.0f - ImGui::CalcTextSize(v).x, ly), kDim, v);
		}
	}

	// ---- 오른쪽: 쌓은 막대 그래프 (오른쪽 끝 = 최근)
	const ImVec2 g0(p.x + kLegendW + 1.0f, p.y);
	const ImVec2 g1(p.x + w, p.y + h);
	const float gw = g1.x - g0.x, gh = h - 4.0f;
	if (gw < 20.0f)
		return;
	dl->AddRectFilled(g0, g1, kGraphBg);
	const size_t n = history.size();
	// 눈금: 위 3% 는 버린다 (튀는 프레임 하나 때문에 나머지가 납작해지지 않게. 튄 막대는 위에서 잘린다)
	std::vector<float> values;
	values.reserve(n);
	for (const auto& f : history)
		values.push_back(gpu ? f.GpuMs : f.CpuMs);
	float maxMs = 0.0f;
	if (!values.empty())
	{
		const size_t k = values.size() - 1 - values.size() * 3 / 100;
		std::nth_element(values.begin(), values.begin() + k, values.end());
		maxMs = values[k];
	}
	const float scale = NiceScale(maxMs);
	const float colW = gw / (float)kMaxFrames;
	auto frameX = [&](size_t i) { return g1.x - (float)(n - i) * colW; };
	dl->PushClipRect(g0, g1, true);
	for (size_t i = 0; i < n; ++i)
	{
		const Profiler::Frame& f = history[i];
		if (gpu && f.GpuMs < 0.0f)
			continue;
		const Summary& s = SummaryOf(f);
		const float x0 = frameX(i), x1 = x0 + (std::max)(1.0f, colW);
		float yb = g1.y;
		for (int c = 0; c < cats; ++c)
		{
			const float v = gpu ? s.Gpu[c] : s.Cpu[c];
			if (v <= 0.0f)
				continue;
			const float yt = (std::max)(g0.y + 4.0f, yb - v / scale * gh);
			dl->AddRectFilled(ImVec2(x0, yt), ImVec2(x1, yb), colors[c]);
			yb = yt;
		}
	}
	// 60 / 30 FPS 선 (글자가 겹치면 뒤의 것은 선만)
	float lastLabelY = 1e9f;
	for (float ms : { 1000.0f / 60.0f, 1000.0f / 30.0f })
	{
		if (ms >= scale)
			continue;
		const float ly = floorf(g1.y - ms / scale * gh) + 0.5f;
		dl->AddLine(ImVec2(g0.x, ly), ImVec2(g1.x, ly), IM_COL32(255, 255, 255, 70));
		if (lastLabelY - ly < ImGui::GetFontSize() + 2.0f)
			continue;
		lastLabelY = ly;
		char t[32];
		snprintf(t, sizeof(t), "%.1fms (%dFPS)", ms, (int)std::lround(1000.0f / ms));
		dl->AddText(ImVec2(g0.x + 4.0f, ly - ImGui::GetFontSize() - 1.0f), IM_COL32(255, 255, 255, 140), t);
	}
	char top[16];
	snprintf(top, sizeof(top), "%.0fms", scale);
	dl->AddText(ImVec2(g1.x - ImGui::CalcTextSize(top).x - 4.0f, g0.y + 2.0f), IM_COL32(255, 255, 255, 110), top);

	// 고른 프레임
	if (sel && n > 0)
	{
		const size_t si = (size_t)(sel->Index - history.front().Index);
		if (si < n)
		{
			const float sx = floorf(frameX(si) + colW * 0.5f) + 0.5f;
			dl->AddLine(ImVec2(sx, g0.y), ImVec2(sx, g1.y), IM_COL32(255, 255, 255, 220), 1.0f);
		}
	}
	if (n == 0)
	{
		const char* msg = m_Recording ? "Collecting frames..." : "Press Record to collect frames";
		const ImVec2 ts = ImGui::CalcTextSize(msg);
		dl->AddText(ImVec2(g0.x + (gw - ts.x) * 0.5f, g0.y + (h - ts.y) * 0.5f), kDim, msg);
	}
	dl->PopClipRect();

	// 누르거나 끌어 프레임 고르기
	ImGui::SetCursorScreenPos(g0);
	ImGui::InvisibleButton(gpu ? "##gpuGraph" : "##cpuGraph", ImVec2(gw, h));
	if (n > 0 && (ImGui::IsItemActive() || ImGui::IsItemHovered()))
	{
		const float mx = ImGui::GetIO().MousePos.x;
		const long long fromRight = (long long)floorf((g1.x - mx) / colW);
		const long long i = (long long)n - 1 - fromRight;
		if (i >= 0 && i < (long long)n)
		{
			const Profiler::Frame& f = history[(size_t)i];
			if (ImGui::IsItemActive())
				m_Selected = f.Index;
			else if (gpu)
				ImGui::SetTooltip("Frame %llu\nGPU %s", (unsigned long long)f.Index, f.GpuMs < 0.0f ? "pending" : (std::to_string(f.GpuMs).substr(0, 5) + " ms").c_str());
			else
				ImGui::SetTooltip("Frame %llu\nCPU %.2f ms (%.0f FPS)", (unsigned long long)f.Index, f.CpuMs, f.CpuMs > 0.0f ? 1000.0f / f.CpuMs : 0.0f);
		}
	}
}

void ProfilerEditorWindow::DrawDetails(ImDrawList* dl, ImVec2 p, float w, float h)
{
	if (h < 40.0f)
		return;
	// ---- 보기 고르기 + 프레임 요약
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kBarH), kBar);
	float x = p.x;
	const char* views[3] = { "Hierarchy", "Timeline", "GPU" };
	for (int v = 0; v < 3; ++v)
	{
		char id[16];
		snprintf(id, sizeof(id), "##view%d", v);
		if (ToolButton(dl, id, views[v], x, p.y, m_View == v))
			m_View = v;
	}
	const Profiler::Frame* sel = SelectedFrame();
	if (sel)
	{
		char info[128];
		if (sel->GpuMs >= 0.0f)
			snprintf(info, sizeof(info), "CPU: %.2fms  GPU: %.2fms  (%.0f FPS)", sel->CpuMs, sel->GpuMs, sel->CpuMs > 0.0f ? 1000.0f / sel->CpuMs : 0.0f);
		else
			snprintf(info, sizeof(info), "CPU: %.2fms  GPU: pending  (%.0f FPS)", sel->CpuMs, sel->CpuMs > 0.0f ? 1000.0f / sel->CpuMs : 0.0f);
		const float iw = ImGui::CalcTextSize(info).x;
		dl->AddText(ImVec2(p.x + w - iw - 10.0f, floorf(p.y + (kBarH - ImGui::GetFontSize()) * 0.5f)), kText, info);
	}

	const ImVec2 c0(p.x, p.y + kBarH);
	const float ch = h - kBarH;
	const float statsW = w > 640.0f ? kStatsW : 0.0f;
	const ImVec2 size(w - statsW, ch);
	if (sel == nullptr)
		return;
	if (m_View == 1)
		DrawTimeline(dl, *sel, c0, size);
	else
	{
		ImGui::SetCursorScreenPos(c0);
		DrawHierarchy(*sel, m_View == 2, size);
	}
	if (statsW > 0.0f)
		DrawStats(dl, *sel, ImVec2(p.x + w - statsW, c0.y), ImVec2(statsW, ch));
}

void ProfilerEditorWindow::DrawHierarchy(const Profiler::Frame& frame, bool gpu, ImVec2 size)
{
	std::vector<Node> nodes;
	if (gpu)
	{
		if (frame.GpuMs < 0.0f)
		{
			ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + 10.0f, ImGui::GetCursorScreenPos().y + 8.0f));
			ImGui::TextDisabled("GPU timings for this frame have not arrived yet (timestamp queries are read a few frames later).");
			return;
		}
		BuildTree(frame.Gpu, nodes);
	}
	else
		BuildTree(frame.Cpu, nodes);
	const float frameMs = gpu ? frame.GpuMs : frame.CpuMs;

	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 2.0f));
	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable(gpu ? "##gpuHierarchy" : "##cpuHierarchy", 6, flags, size))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(gpu ? "GPU Overview" : "Overview", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 62.0f);
		ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 62.0f);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn(gpu ? "GPU ms" : "Time ms", ImGuiTableColumnFlags_WidthFixed, 66.0f);
		ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_WidthFixed, 66.0f);
		ImGui::TableHeadersRow();
		for (int c : nodes[0].Children)
			DrawNode(nodes, c, frameMs);
		ImGui::EndTable();
	}
	ImGui::PopStyleVar();
}

void ProfilerEditorWindow::DrawTimeline(ImDrawList* dl, const Profiler::Frame& frame, ImVec2 p, ImVec2 size)
{
	constexpr float kRulerH = 18.0f, kRowH = 19.0f;
	const ImVec2 p1(p.x + size.x, p.y + size.y);
	dl->AddRectFilled(p, p1, kGraphBg);
	const float frameMs = (std::max)(0.01f, frame.CpuMs);
	const float x0 = p.x + 8.0f, tw = size.x - 16.0f;
	if (tw < 20.0f)
		return;

	// 휠 = 마우스 아래를 기준으로 확대, 끌기 = 옮기기
	ImGui::SetCursorScreenPos(p);
	ImGui::InvisibleButton("##timeline", ImVec2(size.x, (std::max)(1.0f, size.y)));
	const bool hovered = ImGui::IsItemHovered();
	ImGuiIO& io = ImGui::GetIO();
	float visibleMs = frameMs / m_TimelineZoom;
	if (hovered && io.MouseWheel != 0.0f)
	{
		const float mouseMs = m_TimelineOffset + (io.MousePos.x - x0) / tw * visibleMs;
		m_TimelineZoom = std::clamp(m_TimelineZoom * (io.MouseWheel > 0.0f ? 1.25f : 0.8f), 1.0f, 2000.0f);
		visibleMs = frameMs / m_TimelineZoom;
		m_TimelineOffset = mouseMs - (io.MousePos.x - x0) / tw * visibleMs;
	}
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		m_TimelineOffset -= io.MouseDelta.x / tw * visibleMs;
	m_TimelineOffset = std::clamp(m_TimelineOffset, 0.0f, (std::max)(0.0f, frameMs - visibleMs));
	const float pxPerMs = tw / visibleMs;
	auto toX = [&](float ms) { return x0 + (ms - m_TimelineOffset) * pxPerMs; };

	dl->PushClipRect(p, p1, true);
	// 눈금
	{
		float step = 0.001f;
		for (float s : { 0.001f, 0.002f, 0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f })
			if (s * pxPerMs >= 70.0f) { step = s; break; }
		dl->AddRectFilled(p, ImVec2(p1.x, p.y + kRulerH), kBar);
		for (float t = floorf(m_TimelineOffset / step) * step; t <= m_TimelineOffset + visibleMs + step; t += step)
		{
			const float tx = floorf(toX(t)) + 0.5f;
			dl->AddLine(ImVec2(tx, p.y + kRulerH - 5.0f), ImVec2(tx, p1.y), IM_COL32(255, 255, 255, 18));
			char lbl[24];
			snprintf(lbl, sizeof(lbl), step < 0.01f ? "%.3fms" : (step < 1.0f ? "%.2fms" : "%.0fms"), t);
			dl->AddText(ImVec2(tx + 3.0f, p.y + 2.0f), kDim, lbl);
		}
	}
	// 구간 막대
	std::vector<int> cats;
	std::vector<float> self;
	Attribute(frame.Cpu, CpuRule, kCpuOthers, cats, self);
	const char* tipName = nullptr;
	float tipMs = 0.0f, tipStart = 0.0f, tipSelf = 0.0f;
	for (size_t i = 0; i < frame.Cpu.size(); ++i)
	{
		const Profiler::CpuSample& s = frame.Cpu[i];
		float bx0 = toX(s.StartMs), bx1 = toX(s.StartMs + s.Ms);
		if (bx1 < p.x || bx0 > p1.x)
			continue;
		bx1 = (std::max)(bx1, bx0 + 1.0f);
		const float by0 = p.y + kRulerH + 4.0f + s.Depth * kRowH, by1 = by0 + kRowH - 2.0f;
		if (by0 > p1.y)
			continue;
		const ImU32 col = kCpuColors[cats[i]];
		dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), col);
		dl->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(0, 0, 0, 90));
		if (bx1 - bx0 > 28.0f)
		{
			char lbl[160];
			snprintf(lbl, sizeof(lbl), "%s (%.2fms)", s.Name, s.Ms);
			const ImVec4 clip((std::max)(bx0, p.x) + 3.0f, by0, (std::min)(bx1, p1.x) - 2.0f, by1);
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2((std::max)(bx0, p.x) + 4.0f, by0 + 1.0f), IM_COL32(20, 20, 20, 255), lbl, nullptr, 0.0f, &clip);
		}
		if (hovered && io.MousePos.x >= bx0 && io.MousePos.x < bx1 && io.MousePos.y >= by0 && io.MousePos.y < by1)
		{
			tipName = s.Name;
			tipMs = s.Ms;
			tipStart = s.StartMs;
			tipSelf = self[i];
		}
	}
	dl->PopClipRect();
	if (tipName)
		ImGui::SetTooltip("%s\n%.3f ms (self %.3f ms)\nstarts at %.3f ms", tipName, tipMs, (std::max)(0.0f, tipSelf), tipStart);
	else if (hovered)
		ImGui::SetTooltip("Mouse wheel: zoom   Drag: pan");
}

void ProfilerEditorWindow::DrawStats(ImDrawList* dl, const Profiler::Frame& frame, ImVec2 p, ImVec2 size)
{
	dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), kPanel);
	dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x, p.y + size.y), kLine);
	float y = p.y + 6.0f;
	const float lx = p.x + 10.0f, rx = p.x + size.x - 10.0f;
	dl->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
	dl->AddText(ImVec2(lx, y), kText, "Rendering Statistics");
	y += 20.0f;
	if (frame.Stats.empty())
		dl->AddText(ImVec2(lx, y), kDim, "No statistics for this frame");
	// "그룹/이름" → 그룹 제목 아래에 이름 = 값
	std::string group;
	for (const Profiler::Stat& s : frame.Stats)
	{
		const char* slash = strchr(s.Name, '/');
		const std::string g = slash ? std::string(s.Name, slash - s.Name) : std::string();
		const char* label = slash ? slash + 1 : s.Name;
		if (g != group)
		{
			group = g;
			y += 4.0f;
			dl->AddText(ImVec2(lx, y), IM_COL32(150, 190, 235, 255), group.c_str());
			y += 17.0f;
		}
		dl->AddText(ImVec2(lx + 10.0f, y), kDim, label);
		const std::string v = FormatValue(s.Value);
		dl->AddText(ImVec2(rx - ImGui::CalcTextSize(v.c_str()).x, y), kText, v.c_str());
		y += 16.0f;
	}
	dl->PopClipRect();
}
