#include "pch.h"
#include "ParticleSystemEditor.h"
#include "ParticleSystem.h"
#include "UnityGUI.h"

namespace
{
	GameObject* s_PreviewObject = nullptr;   // 비교만 한다 (지워진 뒤에도 역참조하지 않음)
	bool s_Paused = false;
	float s_Speed = 1.0f;

	const ImU32 kText = IM_COL32(196, 196, 196, 255);
	const ImU32 kTextBright = IM_COL32(230, 230, 230, 255);
	const ImU32 kTextDim = IM_COL32(120, 120, 120, 255);
	const ImU32 kFieldBg = IM_COL32(42, 42, 42, 255);
	const ImU32 kFieldBorder = IM_COL32(26, 26, 26, 255);
	const ImU32 kCurveColor = IM_COL32(230, 80, 80, 255);       // Unity 입자 곡선은 빨간색
	const ImU32 kCurveMinColor = IM_COL32(230, 80, 80, 140);

	bool InGroup(ParticleSystem* ps, ParticleSystem* root)
	{
		if (ps == nullptr || root == nullptr)
			return false;
		for (GameObject* g = ps->GetGameObject(); g != nullptr; g = g->GetParent())
			if (g == root->GetGameObject())
				return true;
		return false;
	}

	void Checker(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell = 5.0f)
	{
		dl->AddRectFilled(a, b, IM_COL32(200, 200, 200, 255));
		for (float y = a.y; y < b.y; y += cell)
			for (float x = a.x + (fmodf((y - a.y) / cell, 2.0f) >= 1.0f ? cell : 0.0f); x < b.x; x += cell * 2.0f)
				dl->AddRectFilled(ImVec2(x, y), ImVec2((std::min)(x + cell, b.x), (std::min)(y + cell, b.y)), IM_COL32(150, 150, 150, 255));
	}

	ImU32 ToU32(const Vec4& c) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w)); }

	void DrawGradientBar(ImDrawList* dl, const ParticleGradient& g, ImVec2 a, ImVec2 b)
	{
		Checker(dl, a, b);
		const int slices = (std::max)(8, (int)((b.x - a.x) / 3.0f));
		for (int i = 0; i < slices; ++i)
		{
			const float x0 = a.x + (b.x - a.x) * i / slices, x1 = a.x + (b.x - a.x) * (i + 1) / slices;
			const Vec4 c = g.Evaluate((i + 0.5f) / slices);
			dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(x1, b.y), ToU32(c));
		}
	}

	void DrawSwatch(ImDrawList* dl, const Vec4& c, ImVec2 a, ImVec2 b)
	{
		// 위: 색(불투명), 아래 3px: 알파 막대 (Unity 색 필드)
		dl->AddRectFilled(a, b, ToU32(Vec4(c.x, c.y, c.z, 1.0f)));
		const float ah = 3.0f;
		dl->AddRectFilled(ImVec2(a.x, b.y - ah), b, IM_COL32(0, 0, 0, 255));
		dl->AddRectFilled(ImVec2(a.x, b.y - ah), ImVec2(a.x + (b.x - a.x) * std::clamp(c.w, 0.0f, 1.0f), b.y), IM_COL32(255, 255, 255, 255));
	}

	// ▼ 모드 버튼 (필드 오른쪽 끝). 눌리면 true
	bool ModeButton(ImVec2 pos)
	{
		ImGui::SetCursorScreenPos(pos);
		const bool clicked = ImGui::InvisibleButton("##mode", ImVec2(14.0f, UnityGUI::kRowHeight));
		UnityGUI::DrawIcon(ImGui::GetWindowDrawList(), "dropdown", ImVec2(pos.x + 1.0f, pos.y + 3.0f), 12.0f,
			ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255));
		return clicked;
	}

	void PushPopupStyle()
	{
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
	}
	void PopPopupStyle()
	{
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}

	// ---------------------------------------------------------------- 곡선 그리기
	void DrawCurvePolyline(ImDrawList* dl, const ParticleCurve& c, ImVec2 a, ImVec2 b, float lo, float hi, ImU32 color, float thickness)
	{
		const int n = 48;
		ImVec2 prev;
		for (int i = 0; i <= n; ++i)
		{
			const float t = i / (float)n;
			const float v = c.Evaluate(t);
			const ImVec2 p(a.x + (b.x - a.x) * t, b.y - (b.y - a.y) * ((v - lo) / (hi - lo)));
			if (i > 0)
				dl->AddLine(prev, p, color, thickness);
			prev = p;
		}
	}

	void CurveRange(const MinMaxCurve& c, float& lo, float& hi)
	{
		float l1, h1;
		c.CurveMax.Range(l1, h1);
		lo = (std::min)(0.0f, l1);
		hi = (std::max)(1.0f, h1);
		if (c.Mode == ParticleCurveMode::TwoCurves)
		{
			float l2, h2;
			c.CurveMin.Range(l2, h2);
			lo = (std::min)(lo, l2);
			hi = (std::max)(hi, h2);
		}
		if (hi - lo < 1e-3f)
			hi = lo + 1.0f;
	}

	// 필드 안의 작은 곡선 그림 (Unity 처럼 곡선 아래를 채운다)
	void DrawCurveSwatch(ImDrawList* dl, const MinMaxCurve& c, ImVec2 a, ImVec2 b)
	{
		dl->AddRectFilled(a, b, IM_COL32(52, 52, 52, 255), 2.0f);
		float lo, hi;
		CurveRange(c, lo, hi);
		const ImVec2 ia(a.x + 2, a.y + 2), ib(b.x - 2, b.y - 2);
		const int n = 32;
		for (int i = 0; i < n; ++i)
		{
			const float t0 = i / (float)n, t1 = (i + 1) / (float)n;
			const float top0 = c.CurveMax.Evaluate(t0), top1 = c.CurveMax.Evaluate(t1);
			const float bot0 = c.Mode == ParticleCurveMode::TwoCurves ? c.CurveMin.Evaluate(t0) : lo;
			const float bot1 = c.Mode == ParticleCurveMode::TwoCurves ? c.CurveMin.Evaluate(t1) : lo;
			auto y = [&](float v) { return ib.y - (ib.y - ia.y) * ((v - lo) / (hi - lo)); };
			const float x0 = ia.x + (ib.x - ia.x) * t0, x1 = ia.x + (ib.x - ia.x) * t1;
			dl->AddQuadFilled(ImVec2(x0, y(top0)), ImVec2(x1, y(top1)), ImVec2(x1, y(bot1)), ImVec2(x0, y(bot0)), IM_COL32(230, 80, 80, 70));
		}
		DrawCurvePolyline(dl, c.CurveMax, ia, ib, lo, hi, kCurveColor, 1.2f);
		if (c.Mode == ParticleCurveMode::TwoCurves)
			DrawCurvePolyline(dl, c.CurveMin, ia, ib, lo, hi, kCurveMinColor, 1.0f);
		dl->AddRect(a, b, kFieldBorder, 2.0f);
	}

	// ---------------------------------------------------------------- 곡선 편집 팝업
	struct CurveEditState
	{
		const void* Target = nullptr;
		bool EditMin = false;
		int Selected = -1;
		bool Dragging = false;
	} s_CurveEdit;

	bool CurveEditor(MinMaxCurve* c)
	{
		bool changed = false;
		if (s_CurveEdit.Target != c)
			s_CurveEdit = CurveEditState{ c };
		const bool two = c->Mode == ParticleCurveMode::TwoCurves;
		if (!two)
			s_CurveEdit.EditMin = false;
		ParticleCurve& curve = s_CurveEdit.EditMin ? c->CurveMin : c->CurveMax;
		if (curve.Keys.empty())
			curve = ParticleCurve::Constant(1.0f);

		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted(two ? "Curves (Random Between Two Curves)" : "Curve");
		ImGui::PopFont();
		if (two)
		{
			ImGui::SameLine();
			if (ImGui::RadioButton("Max", !s_CurveEdit.EditMin)) { s_CurveEdit.EditMin = false; s_CurveEdit.Selected = -1; }
			ImGui::SameLine();
			if (ImGui::RadioButton("Min", s_CurveEdit.EditMin)) { s_CurveEdit.EditMin = true; s_CurveEdit.Selected = -1; }
		}

		// ---- 그래프
		const ImVec2 size(340.0f, 170.0f);
		const ImVec2 a = ImGui::GetCursorScreenPos();
		const ImVec2 b(a.x + size.x, a.y + size.y);
		ImGui::InvisibleButton("##graph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(a, b, IM_COL32(40, 40, 40, 255));
		float lo, hi;
		CurveRange(*c, lo, hi);
		const float pad = (hi - lo) * 0.08f;
		lo -= pad;
		hi += pad;
		auto toScreen = [&](float t, float v) { return ImVec2(a.x + size.x * t, b.y - size.y * ((v - lo) / (hi - lo))); };
		auto toCurve = [&](ImVec2 p, float& t, float& v) {
			t = std::clamp((p.x - a.x) / size.x, 0.0f, 1.0f);
			v = lo + (b.y - p.y) / size.y * (hi - lo);
		};
		for (int i = 0; i <= 4; ++i)
		{
			const float x = a.x + size.x * i / 4.0f;
			dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), IM_COL32(60, 60, 60, 255));
		}
		for (float v : { 0.0f, 1.0f })
			if (v >= lo && v <= hi)
			{
				const float y = toScreen(0.0f, v).y;
				dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(75, 75, 75, 255));
				char buf[16];
				snprintf(buf, sizeof(buf), "%g", v * c->Multiplier);
				dl->AddText(ImVec2(a.x + 3, y - 14), kTextDim, buf);
			}
		if (two)
		{
			// 두 곡선 사이를 채운다
			const int n = 64;
			for (int i = 0; i < n; ++i)
			{
				const float t0 = i / (float)n, t1 = (i + 1) / (float)n;
				dl->AddQuadFilled(toScreen(t0, c->CurveMax.Evaluate(t0)), toScreen(t1, c->CurveMax.Evaluate(t1)),
					toScreen(t1, c->CurveMin.Evaluate(t1)), toScreen(t0, c->CurveMin.Evaluate(t0)), IM_COL32(230, 80, 80, 45));
			}
			DrawCurvePolyline(dl, s_CurveEdit.EditMin ? c->CurveMax : c->CurveMin, a, b, lo, hi, IM_COL32(230, 80, 80, 110), 1.5f);
		}
		DrawCurvePolyline(dl, curve, a, b, lo, hi, kCurveColor, 2.0f);

		// 키: 클릭 = 선택·끌기, 빈 곳 더블클릭 = 추가, 오른쪽 클릭 = 지우기
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hoverKey = -1;
		for (int i = 0; i < (int)curve.Keys.size(); ++i)
		{
			const ImVec2 p = toScreen(curve.Keys[i].Time, curve.Keys[i].Value);
			const float dx = mouse.x - p.x, dy = mouse.y - p.y;
			if (dx * dx + dy * dy < 49.0f)
				hoverKey = i;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			s_CurveEdit.Selected = hoverKey;
			s_CurveEdit.Dragging = hoverKey >= 0;
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoverKey < 0)
		{
			float t, v;
			toCurve(mouse, t, v);
			curve.Keys.push_back({ t, v });
			curve.Sort();
			for (int i = 0; i < (int)curve.Keys.size(); ++i)
				if (curve.Keys[i].Time == t && curve.Keys[i].Value == v)
					s_CurveEdit.Selected = i;
			changed = true;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoverKey >= 0 && curve.Keys.size() > 1)
		{
			curve.Keys.erase(curve.Keys.begin() + hoverKey);
			s_CurveEdit.Selected = -1;
			changed = true;
		}
		if (s_CurveEdit.Dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_CurveEdit.Selected >= 0 && s_CurveEdit.Selected < (int)curve.Keys.size())
		{
			float t, v;
			toCurve(mouse, t, v);
			auto& key = curve.Keys[s_CurveEdit.Selected];
			if (key.Time != t || key.Value != v)
			{
				key.Time = t;
				key.Value = v;
				// 옆 키를 넘지 않게 (순서 유지)
				if (s_CurveEdit.Selected > 0) key.Time = (std::max)(key.Time, curve.Keys[s_CurveEdit.Selected - 1].Time);
				if (s_CurveEdit.Selected + 1 < (int)curve.Keys.size()) key.Time = (std::min)(key.Time, curve.Keys[s_CurveEdit.Selected + 1].Time);
				changed = true;
			}
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			s_CurveEdit.Dragging = false;
		for (int i = 0; i < (int)curve.Keys.size(); ++i)
		{
			const ImVec2 p = toScreen(curve.Keys[i].Time, curve.Keys[i].Value);
			const bool sel = i == s_CurveEdit.Selected;
			dl->AddCircleFilled(p, sel ? 5.0f : 4.0f, sel ? IM_COL32(255, 255, 255, 255) : (i == hoverKey ? IM_COL32(255, 200, 200, 255) : IM_COL32(240, 120, 120, 255)));
			dl->AddCircle(p, sel ? 5.0f : 4.0f, IM_COL32(20, 20, 20, 255));
		}
		dl->AddRect(a, b, kFieldBorder);
		dl->AddText(ImVec2(a.x + 4, b.y + 2), kTextDim, "0");
		dl->AddText(ImVec2(b.x - 10, b.y + 2), kTextDim, "1");
		ImGui::Dummy(ImVec2(size.x, 14.0f));
		ImGui::TextDisabled("Double-click: add key   Right-click: delete key");

		// ---- 선택한 키 / 배율
		ImGui::PushItemWidth(80.0f);
		if (s_CurveEdit.Selected >= 0 && s_CurveEdit.Selected < (int)curve.Keys.size())
		{
			auto& key = curve.Keys[s_CurveEdit.Selected];
			ImGui::TextUnformatted("Key");
			ImGui::SameLine(60.0f);
			if (ImGui::DragFloat("Time##k", &key.Time, 0.005f, 0.0f, 1.0f, "%.3f")) { curve.Sort(); changed = true; }
			ImGui::SameLine();
			if (ImGui::DragFloat("Value##k", &key.Value, 0.01f, 0.0f, 0.0f, "%.3f")) changed = true;
		}
		ImGui::TextUnformatted("Multiplier");
		ImGui::SameLine(80.0f);
		if (ImGui::DragFloat("##mult", &c->Multiplier, 0.05f, 0.0f, 0.0f, "%g")) changed = true;
		ImGui::PopItemWidth();

		// ---- 프리셋
		ImGui::TextUnformatted("Presets");
		struct Preset { const char* name; ParticleCurve curve; };
		static const Preset kPresets[] = {
			{ "Flat", ParticleCurve::Constant(1.0f) },
			{ "Up", ParticleCurve::Linear(0.0f, 1.0f) },
			{ "Down", ParticleCurve::Linear(1.0f, 0.0f) },
			{ "Ease In", ParticleCurve{ { 0.0f, 0.0f }, { 0.6f, 0.2f }, { 1.0f, 1.0f } } },
			{ "Ease Out", ParticleCurve{ { 0.0f, 1.0f }, { 0.4f, 0.8f }, { 1.0f, 0.0f } } },
			{ "Bell", ParticleCurve{ { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } } },
		};
		for (int i = 0; i < (int)std::size(kPresets); ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(i);
			const ImVec2 pa = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton(kPresets[i].name, ImVec2(50, 30)))
			{
				curve = kPresets[i].curve;
				s_CurveEdit.Selected = -1;
				changed = true;
			}
			const bool h = ImGui::IsItemHovered();
			if (h)
				ImGui::SetTooltip("%s", kPresets[i].name);
			dl->AddRectFilled(pa, ImVec2(pa.x + 50, pa.y + 30), h ? IM_COL32(70, 70, 70, 255) : IM_COL32(52, 52, 52, 255), 2.0f);
			DrawCurvePolyline(dl, kPresets[i].curve, ImVec2(pa.x + 4, pa.y + 4), ImVec2(pa.x + 46, pa.y + 26), -0.05f, 1.05f, kCurveColor, 1.5f);
			ImGui::PopID();
		}
		return changed;
	}

	// ---------------------------------------------------------------- 그라디언트 편집 팝업
	struct GradientEditState
	{
		const void* Target = nullptr;
		bool Alpha = false;
		int Selected = 0;
		bool Dragging = false;
	} s_GradEdit;

	ParticleGradient MakePreset(int i)
	{
		ParticleGradient g;
		switch (i)
		{
		case 0:   // 흰색 → 투명
			g.Colors = { { 0, 1, 1, 1 }, { 1, 1, 1, 1 } };
			g.Alphas = { { 0, 1 }, { 1, 0 } };
			break;
		case 1:   // 불: 노랑 → 주황 → 빨강 → 어두운 빨강
			g.Colors = { { 0.0f, 1.0f, 0.95f, 0.6f }, { 0.25f, 1.0f, 0.62f, 0.15f }, { 0.6f, 0.85f, 0.18f, 0.05f }, { 1.0f, 0.25f, 0.05f, 0.03f } };
			g.Alphas = { { 0.0f, 0.0f }, { 0.1f, 1.0f }, { 0.65f, 0.7f }, { 1.0f, 0.0f } };
			break;
		case 2:   // 연기: 회색, 천천히 나타났다 사라짐
			g.Colors = { { 0.0f, 0.55f, 0.55f, 0.55f }, { 1.0f, 0.3f, 0.3f, 0.3f } };
			g.Alphas = { { 0.0f, 0.0f }, { 0.25f, 0.55f }, { 1.0f, 0.0f } };
			break;
		case 3:   // 무지개
			g.Colors = { { 0.0f, 1, 0.2f, 0.2f }, { 0.25f, 1, 0.9f, 0.2f }, { 0.5f, 0.2f, 1, 0.3f }, { 0.75f, 0.2f, 0.5f, 1 }, { 1.0f, 0.8f, 0.2f, 1 } };
			g.Alphas = { { 0, 1 }, { 1, 1 } };
			break;
		default:  // 마법: 하늘색 → 보라, 끝에 사라짐
			g.Colors = { { 0.0f, 0.4f, 0.95f, 1.0f }, { 1.0f, 0.7f, 0.25f, 1.0f } };
			g.Alphas = { { 0.0f, 1.0f }, { 0.7f, 0.8f }, { 1.0f, 0.0f } };
			break;
		}
		return g;
	}

	bool GradientEditor(ParticleGradient* g)
	{
		bool changed = false;
		if (s_GradEdit.Target != g)
			s_GradEdit = GradientEditState{ g };
		if (g->Colors.empty()) g->Colors = { { 0, 1, 1, 1 } };
		if (g->Alphas.empty()) g->Alphas = { { 0, 1 } };

		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Gradient Editor");
		ImGui::PopFont();
		ImGui::SameLine(200.0f);
		ImGui::SetNextItemWidth(130.0f);
		static const char* kModes[] = { "Blend", "Fixed" };
		if (ImGui::Combo("##gmode", &g->Mode, kModes, 2)) changed = true;

		const float width = 330.0f, markerH = 14.0f, barH = 26.0f;
		const ImVec2 top = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 barA(top.x, top.y + markerH), barB(top.x + width, top.y + markerH + barH);
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		auto timeAt = [&](float x) { return std::clamp((x - barA.x) / width, 0.0f, 1.0f); };

		// 위(알파 키) / 아래(색 키) 표시줄: 빈 곳 클릭 = 키 추가, 키 클릭 = 선택·끌기
		auto strip = [&](bool alpha, float y) {
			ImGui::SetCursorScreenPos(ImVec2(barA.x - 6.0f, y));
			ImGui::InvisibleButton(alpha ? "##alphaStrip" : "##colorStrip", ImVec2(width + 12.0f, markerH));
			const bool hov = ImGui::IsItemHovered();
			const int count = alpha ? (int)g->Alphas.size() : (int)g->Colors.size();
			int hit = -1;
			for (int i = 0; i < count; ++i)
			{
				const float x = barA.x + width * (alpha ? g->Alphas[i].Time : g->Colors[i].Time);
				if (fabsf(mouse.x - x) <= 6.0f)
					hit = i;
			}
			if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				if (hit < 0 && count < 8)
				{
					const float t = timeAt(mouse.x);
					const Vec4 c = g->Evaluate(t);
					if (alpha) g->Alphas.push_back({ t, c.w });
					else g->Colors.push_back({ t, c.x, c.y, c.z });
					hit = count;
					changed = true;
				}
				s_GradEdit.Alpha = alpha;
				s_GradEdit.Selected = hit;
				s_GradEdit.Dragging = hit >= 0;
			}
			for (int i = 0; i < (alpha ? (int)g->Alphas.size() : (int)g->Colors.size()); ++i)
			{
				const float t = alpha ? g->Alphas[i].Time : g->Colors[i].Time;
				const float x = floorf(barA.x + width * t) + 0.5f;
				const bool sel = s_GradEdit.Alpha == alpha && s_GradEdit.Selected == i;
				ImU32 fill;
				if (alpha)
				{
					const float a = g->Alphas[i].A;
					fill = IM_COL32((int)(a * 255), (int)(a * 255), (int)(a * 255), 255);
				}
				else
					fill = ToU32(Vec4(g->Colors[i].R, g->Colors[i].G, g->Colors[i].B, 1.0f));
				// 막대 쪽을 가리키는 작은 집 모양
				const float y0 = alpha ? y : y + 2.0f, y1 = alpha ? y + markerH - 2.0f : y + markerH;
				const float tip = alpha ? y1 + 2.0f : y0 - 2.0f;
				dl->AddRectFilled(ImVec2(x - 5, alpha ? y0 : y0 + 3), ImVec2(x + 5, alpha ? y1 - 3 : y1), fill);
				dl->AddTriangleFilled(ImVec2(x - 5, alpha ? y1 - 3 : y0 + 3), ImVec2(x + 5, alpha ? y1 - 3 : y0 + 3), ImVec2(x, tip), fill);
				dl->AddRect(ImVec2(x - 5, alpha ? y0 : y0 + 3), ImVec2(x + 5, alpha ? y1 - 3 : y1), sel ? IM_COL32(60, 140, 230, 255) : IM_COL32(20, 20, 20, 255), 0.0f, 0, sel ? 2.0f : 1.0f);
			}
		};
		strip(true, top.y);
		DrawGradientBar(dl, *g, barA, barB);
		dl->AddRect(barA, barB, kFieldBorder);
		strip(false, barB.y);

		if (s_GradEdit.Dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const float t = timeAt(mouse.x);
			if (s_GradEdit.Alpha && s_GradEdit.Selected < (int)g->Alphas.size() && g->Alphas[s_GradEdit.Selected].Time != t)
			{
				g->Alphas[s_GradEdit.Selected].Time = t;
				changed = true;
			}
			else if (!s_GradEdit.Alpha && s_GradEdit.Selected < (int)g->Colors.size() && g->Colors[s_GradEdit.Selected].Time != t)
			{
				g->Colors[s_GradEdit.Selected].Time = t;
				changed = true;
			}
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_GradEdit.Dragging)
		{
			// 끌기가 끝나면 시간 순으로 (선택 유지)
			s_GradEdit.Dragging = false;
			if (s_GradEdit.Alpha && s_GradEdit.Selected < (int)g->Alphas.size())
			{
				const auto key = g->Alphas[s_GradEdit.Selected];
				g->Sort();
				for (int i = 0; i < (int)g->Alphas.size(); ++i)
					if (g->Alphas[i].Time == key.Time && g->Alphas[i].A == key.A) s_GradEdit.Selected = i;
			}
			else if (s_GradEdit.Selected < (int)g->Colors.size())
			{
				const auto key = g->Colors[s_GradEdit.Selected];
				g->Sort();
				for (int i = 0; i < (int)g->Colors.size(); ++i)
					if (g->Colors[i].Time == key.Time && g->Colors[i].R == key.R) s_GradEdit.Selected = i;
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(top.x, barB.y + markerH + 6.0f));

		// ---- 선택한 키
		const int count = s_GradEdit.Alpha ? (int)g->Alphas.size() : (int)g->Colors.size();
		if (s_GradEdit.Selected >= count)
			s_GradEdit.Selected = count - 1;
		if (s_GradEdit.Selected >= 0)
		{
			float* time = s_GradEdit.Alpha ? &g->Alphas[s_GradEdit.Selected].Time : &g->Colors[s_GradEdit.Selected].Time;
			if (s_GradEdit.Alpha)
			{
				ImGui::SetNextItemWidth(160.0f);
				if (ImGui::SliderFloat("Alpha", &g->Alphas[s_GradEdit.Selected].A, 0.0f, 1.0f, "%.2f")) changed = true;
			}
			else
			{
				auto& k = g->Colors[s_GradEdit.Selected];
				float rgb[3] = { k.R, k.G, k.B };
				ImGui::SetNextItemWidth(180.0f);
				if (ImGui::ColorPicker3("##keycolor", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_DisplayRGB))
				{
					k.R = rgb[0]; k.G = rgb[1]; k.B = rgb[2];
					changed = true;
				}
			}
			float percent = *time * 100.0f;
			ImGui::SetNextItemWidth(80.0f);
			if (ImGui::DragFloat("Location %", &percent, 0.5f, 0.0f, 100.0f, "%.1f"))
			{
				*time = std::clamp(percent / 100.0f, 0.0f, 1.0f);
				changed = true;
			}
			ImGui::SameLine();
			const bool canDelete = count > 1;
			if (!canDelete) ImGui::BeginDisabled();
			if (ImGui::Button("Delete"))
			{
				if (s_GradEdit.Alpha) g->Alphas.erase(g->Alphas.begin() + s_GradEdit.Selected);
				else g->Colors.erase(g->Colors.begin() + s_GradEdit.Selected);
				s_GradEdit.Selected = 0;
				changed = true;
			}
			if (!canDelete) ImGui::EndDisabled();
		}

		// ---- 프리셋
		ImGui::TextUnformatted("Presets");
		static const char* kPresetNames[] = { "White Fade", "Fire", "Smoke", "Rainbow", "Magic" };
		for (int i = 0; i < 5; ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(i);
			const ImVec2 pa = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##preset", ImVec2(60, 18)))
			{
				*g = MakePreset(i);
				s_GradEdit.Selected = 0;
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", kPresetNames[i]);
			DrawGradientBar(dl, MakePreset(i), pa, ImVec2(pa.x + 60, pa.y + 18));
			dl->AddRect(pa, ImVec2(pa.x + 60, pa.y + 18), kFieldBorder);
			ImGui::PopID();
		}
		if (changed)
			g->Sort();
		return changed;
	}

	bool ColorPopup(const char* id, Vec4* c)
	{
		bool changed = false;
		if (ImGui::BeginPopup(id))
		{
			float rgba[4] = { c->x, c->y, c->z, c->w };
			if (ImGui::ColorPicker4("##picker", rgba, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_AlphaBar))
			{
				*c = Vec4(rgba[0], rgba[1], rgba[2], rgba[3]);
				changed = true;
			}
			ImGui::EndPopup();
		}
		return changed;
	}

	bool GradientPopup(const char* id, ParticleGradient* g)
	{
		bool changed = false;
		PushPopupStyle();
		if (ImGui::BeginPopup(id))
		{
			changed = GradientEditor(g);
			ImGui::EndPopup();
		}
		PopPopupStyle();
		return changed;
	}
}

namespace ParticleSystemEditor
{
	// ---------------------------------------------------------------- 미리보기
	ParticleSystem* SelectedRoot()
	{
		if (SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT)
			return nullptr;
		GameObject* go = SelectionManager::GetSelectedGameObject();
		ParticleSystem* found = nullptr;
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
		{
			if (ParticleSystem* ps = g->GetComponent<ParticleSystem>())
				found = ps;
			else if (found)
				break;   // 입자 시스템이 끊기면 거기까지가 묶음
		}
		return found;
	}

	float PlaybackSpeedFor(ParticleSystem* ps)
	{
		ParticleSystem* root = SelectedRoot();
		return root && InGroup(ps, root) ? s_Speed : 1.0f;
	}

	void UpdatePreview(float dt)
	{
		ParticleSystem* root = SelectedRoot();
		GameObject* go = root ? root->GetGameObject() : nullptr;
		if (go != s_PreviewObject)
		{
			// 선택이 바뀌면 이전 미리보기를 멈추고 새로 처음부터 (Unity 와 같음)
			for (ParticleSystem* ps : ParticleSystem::All())
				if (ps->IsAlive() && !InGroup(ps, root))
					ps->Stop(false, true);
			s_PreviewObject = go;
			s_Paused = false;
			if (root)
			{
				root->Restart();
				EditorLog::Write("Particles", "preview '%s'", go->GetName().c_str());
			}
		}
		if (root == nullptr || s_Paused)
			return;
		std::vector<ParticleSystem*> group;
		root->CollectHierarchy(group);
		for (ParticleSystem* ps : group)
			if (ps->ActiveInHierarchy())
				ps->Simulate(dt * s_Speed);
	}

	// ---------------------------------------------------------------- Scene 뷰 창
	bool OverlayRect(const ImVec2& viewMin, const ImVec2& viewMax, ImVec2& rectMin, ImVec2& rectMax)
	{
		if (SelectedRoot() == nullptr)
			return false;
		const ImVec2 size(236.0f, 112.0f);
		rectMax = ImVec2(viewMax.x - 10.0f, viewMax.y - 10.0f);
		rectMin = ImVec2(rectMax.x - size.x, rectMax.y - size.y);
		return rectMin.x > viewMin.x && rectMin.y > viewMin.y;
	}

	void DrawOverlay(const ImVec2& viewMin, const ImVec2& viewMax)
	{
		ImVec2 a, b;
		if (!OverlayRect(viewMin, viewMax, a, b))
			return;
		ParticleSystem* root = SelectedRoot();
		std::vector<ParticleSystem*> group;
		root->CollectHierarchy(group);
		int particles = 0;
		bool alive = false;
		for (ParticleSystem* ps : group)
		{
			particles += ps->ParticleCount();
			alive = alive || ps->IsAlive();
		}
		const bool playMode = Application::IsPlaying();
		const bool running = playMode ? root->IsPlaying() : (alive && !s_Paused && !root->IsPaused());

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(a, b, IM_COL32(40, 40, 40, 240), 4.0f);
		dl->AddRect(a, b, IM_COL32(24, 24, 24, 255), 4.0f);
		ImFont* bold = UnityGUI::BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(a.x + 8.0f, a.y + 5.0f), kTextBright, "Particle Effect");

		ImGui::PushID("##particleEffect");
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.42f, 0.42f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.45f, 0.65f, 1.0f));
		const float by = a.y + 26.0f, bw = 70.0f;
		ImGui::SetCursorScreenPos(ImVec2(a.x + 8.0f, by));
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::Button(running ? "Pause" : "Play", ImVec2(bw, 20.0f)))
		{
			if (running)
			{
				if (playMode) root->Pause(true);
				else s_Paused = true;
			}
			else
			{
				s_Paused = false;
				if (root->IsPaused() || alive) root->Play(true);
				else root->Restart();
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(a.x + 8.0f + bw + 5.0f, by));
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::Button("Restart", ImVec2(bw, 20.0f)))
		{
			s_Paused = false;
			root->Restart();
		}
		ImGui::SetCursorScreenPos(ImVec2(a.x + 8.0f + (bw + 5.0f) * 2.0f, by));
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::Button("Stop", ImVec2(bw, 20.0f)))
		{
			s_Paused = false;
			root->Stop(true, true);
		}
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);

		auto row = [&](float y, const char* label) { dl->AddText(ImVec2(a.x + 10.0f, y + 1.0f), kText, label); };
		float y = by + 28.0f;
		row(y, "Playback Speed");
		ImGui::SetNextItemAllowOverlap();
		if (UnityGUI::FloatBox("##speed", &s_Speed, ImVec2(a.x + 128.0f, y - 1.0f), 96.0f))
			s_Speed = std::clamp(s_Speed, 0.0f, 10.0f);
		y += 20.0f;
		row(y, "Playback Time");
		char buf[32];
		snprintf(buf, sizeof(buf), "%.2f", root->GetTime());
		dl->AddText(ImVec2(a.x + 134.0f, y + 1.0f), kTextBright, buf);
		y += 18.0f;
		row(y, "Particles");
		snprintf(buf, sizeof(buf), "%d", particles);
		dl->AddText(ImVec2(a.x + 134.0f, y + 1.0f), kTextBright, buf);
		ImGui::PopID();
	}

	// ---------------------------------------------------------------- Inspector 위젯
	bool ModuleHeader(const char* id, const char* title, bool* enabled, bool defaultOpen, bool supported)
	{
		ImGuiStorage* st = ImGui::GetStateStorage();
		const ImGuiID key = ImGui::GetID(id);
		bool open = st->GetBool(key, defaultOpen);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 20.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImGui::PushID(id);
		ImGui::SetNextItemAllowOverlap();
		const bool clicked = ImGui::InvisibleButton("##band", ImVec2(w, h));
		const bool hovered = ImGui::IsItemHovered();
		if (clicked && supported)
		{
			open = !open;
			st->SetBool(key, open);
		}
		dl->AddRectFilled(ImVec2(p.x + 4.0f, p.y + 1.0f), ImVec2(p.x + w - 4.0f, p.y + h - 1.0f),
			hovered && supported ? IM_COL32(66, 66, 66, 255) : IM_COL32(56, 56, 56, 255), 2.0f);
		dl->AddRect(ImVec2(p.x + 4.0f, p.y + 1.0f), ImVec2(p.x + w - 4.0f, p.y + h - 1.0f), IM_COL32(32, 32, 32, 255), 2.0f);
		float x = p.x + 10.0f;
		if (enabled)
		{
			const ImVec2 cb(x, p.y + 3.0f);
			ImGui::SetCursorScreenPos(cb);
			ImGui::SetNextItemAllowOverlap();
			if (supported && ImGui::InvisibleButton("##on", ImVec2(14, 14)))
				*enabled = !*enabled;
			dl->AddRectFilled(cb, ImVec2(cb.x + 14, cb.y + 14), kFieldBg, 2.0f);
			dl->AddRect(cb, ImVec2(cb.x + 14, cb.y + 14), kFieldBorder, 2.0f);
			if (*enabled && supported)
				UnityGUI::DrawIcon(dl, "check", cb, 14.0f);
			x += 20.0f;
		}
		else
			x += 4.0f;
		dl->AddText(ImVec2(x, p.y + 3.0f), supported ? (enabled && !*enabled ? kText : kTextBright) : kTextDim, title);
		if (!supported && hovered)
			ImGui::SetTooltip("%s is not supported in NOVA yet", title);
		ImGui::PopID();
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 1.0f));
		const bool show = open && supported;
		if (show)
			UnityGUI::Spacing(2.0f);
		return show;
	}

	void ModuleEnd()
	{
		UnityGUI::Spacing(4.0f);
	}

	bool CurveField(const char* label, MinMaxCurve* c, int indent, bool allowCurves)
	{
		bool changed = false;
		UnityGUI::FieldRow row = UnityGUI::BeginFieldRow(label, indent);
		ImGui::PushID(c);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float fieldW = row.fieldW - 18.0f;   // 오른쪽 ▼ 자리 (창 가장자리에 걸리지 않게)
		const ImVec2 f0(row.fieldX, row.p.y);
		switch (c->Mode)
		{
		case ParticleCurveMode::Constant:
			changed |= UnityGUI::FloatBox("##c", &c->ConstantMax, f0, fieldW);
			break;
		case ParticleCurveMode::TwoConstants:
		{
			const float half = floorf((fieldW - 4.0f) * 0.5f);
			changed |= UnityGUI::FloatBox("##min", &c->ConstantMin, f0, half);
			changed |= UnityGUI::FloatBox("##max", &c->ConstantMax, ImVec2(f0.x + half + 4.0f, f0.y), half);
			break;
		}
		default:
		{
			ImGui::SetCursorScreenPos(f0);
			if (ImGui::InvisibleButton("##curve", ImVec2(fieldW, UnityGUI::kRowHeight)))
				ImGui::OpenPopup("##curvePopup");
			DrawCurveSwatch(dl, *c, f0, ImVec2(f0.x + fieldW, f0.y + UnityGUI::kRowHeight));
			break;
		}
		}
		if (ModeButton(ImVec2(row.fieldX + fieldW + 2.0f, row.p.y)))
			ImGui::OpenPopup("##curveMode");
		PushPopupStyle();
		if (ImGui::BeginPopup("##curveMode"))
		{
			static const char* kModes[] = { "Constant", "Curve", "Random Between Two Curves", "Random Between Two Constants" };
			static const int kOrder[] = { 0, 1, 3, 2 };   // Unity 메뉴 순서
			for (int k : kOrder)
			{
				if (!allowCurves && (k == 1 || k == 2))
					continue;
				if (ImGui::MenuItem(kModes[k], nullptr, (int)c->Mode == k))
				{
					const ParticleCurveMode next = (ParticleCurveMode)k;
					// 모드를 바꿔도 값이 크게 달라지지 않게 (Unity 처럼)
					if ((c->Mode == ParticleCurveMode::Constant || c->Mode == ParticleCurveMode::TwoConstants) && (next == ParticleCurveMode::Curve || next == ParticleCurveMode::TwoCurves))
					{
						c->Multiplier = c->ConstantMax != 0.0f ? c->ConstantMax : 1.0f;
						c->CurveMax = ParticleCurve::Constant(1.0f);
						c->CurveMin = ParticleCurve::Constant(0.0f);
					}
					else if ((c->Mode == ParticleCurveMode::Curve || c->Mode == ParticleCurveMode::TwoCurves) && next == ParticleCurveMode::Constant)
						c->ConstantMax = c->CurveMax.Evaluate(0.0f) * c->Multiplier;
					else if (c->Mode == ParticleCurveMode::Constant && next == ParticleCurveMode::TwoConstants)
						c->ConstantMin = c->ConstantMax;
					else if (c->Mode == ParticleCurveMode::Curve && next == ParticleCurveMode::TwoCurves && c->CurveMin.Keys.empty())
						c->CurveMin = ParticleCurve::Constant(0.0f);
					c->Mode = next;
					changed = true;
				}
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("##curvePopup"))
		{
			changed |= CurveEditor(c);
			ImGui::EndPopup();
		}
		PopPopupStyle();
		ImGui::PopID();
		UnityGUI::EndFieldRow(row);
		return changed;
	}

	bool GradientField(const char* label, MinMaxGradient* g, int indent, bool colorModes)
	{
		bool changed = false;
		UnityGUI::FieldRow row = UnityGUI::BeginFieldRow(label, indent);
		ImGui::PushID(g);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float fieldW = row.fieldW - 18.0f;   // 오른쪽 ▼ 자리 (창 가장자리에 걸리지 않게)
		const float h = UnityGUI::kRowHeight;
		const ImVec2 f0(row.fieldX, row.p.y);
		const float half = floorf((fieldW - 4.0f) * 0.5f);
		auto swatchButton = [&](const char* id, ImVec2 pos, float w) {
			ImGui::SetCursorScreenPos(pos);
			return ImGui::InvisibleButton(id, ImVec2(w, h));
		};
		switch (g->Mode)
		{
		case ParticleGradientMode::Color:
			if (swatchButton("##c", f0, fieldW)) ImGui::OpenPopup("##colorMax");
			DrawSwatch(dl, g->ColorMax, ImVec2(f0.x + 1, f0.y + 1), ImVec2(f0.x + fieldW - 1, f0.y + h - 1));
			break;
		case ParticleGradientMode::TwoColors:
			if (swatchButton("##cmin", f0, half)) ImGui::OpenPopup("##colorMin");
			DrawSwatch(dl, g->ColorMin, ImVec2(f0.x + 1, f0.y + 1), ImVec2(f0.x + half - 1, f0.y + h - 1));
			if (swatchButton("##cmax", ImVec2(f0.x + half + 4.0f, f0.y), half)) ImGui::OpenPopup("##colorMax");
			DrawSwatch(dl, g->ColorMax, ImVec2(f0.x + half + 5, f0.y + 1), ImVec2(f0.x + half * 2 + 3, f0.y + h - 1));
			break;
		case ParticleGradientMode::Gradient:
		case ParticleGradientMode::RandomColor:
			if (swatchButton("##g", f0, fieldW)) ImGui::OpenPopup("##gradMax");
			DrawGradientBar(dl, g->GradientMax, ImVec2(f0.x + 1, f0.y + 1), ImVec2(f0.x + fieldW - 1, f0.y + h - 1));
			break;
		case ParticleGradientMode::TwoGradients:
			if (swatchButton("##gmin", f0, half)) ImGui::OpenPopup("##gradMin");
			DrawGradientBar(dl, g->GradientMin, ImVec2(f0.x + 1, f0.y + 1), ImVec2(f0.x + half - 1, f0.y + h - 1));
			if (swatchButton("##gmax", ImVec2(f0.x + half + 4.0f, f0.y), half)) ImGui::OpenPopup("##gradMax");
			DrawGradientBar(dl, g->GradientMax, ImVec2(f0.x + half + 5, f0.y + 1), ImVec2(f0.x + half * 2 + 3, f0.y + h - 1));
			break;
		}
		dl->AddRect(f0, ImVec2(f0.x + fieldW, f0.y + h), kFieldBorder, 2.0f);
		if (ModeButton(ImVec2(row.fieldX + fieldW + 2.0f, row.p.y)))
			ImGui::OpenPopup("##gradMode");
		PushPopupStyle();
		if (ImGui::BeginPopup("##gradMode"))
		{
			static const char* kModes[] = { "Color", "Gradient", "Random Between Two Colors", "Random Between Two Gradients", "Random Color" };
			for (int k = 0; k < 5; ++k)
			{
				if (!colorModes && k != 1 && k != 3)
					continue;
				if (ImGui::MenuItem(kModes[k], nullptr, (int)g->Mode == k))
				{
					const ParticleGradientMode next = (ParticleGradientMode)k;
					if (g->Mode == ParticleGradientMode::Color && (next == ParticleGradientMode::Gradient || next == ParticleGradientMode::RandomColor))
					{
						g->GradientMax.Colors = { { 0, g->ColorMax.x, g->ColorMax.y, g->ColorMax.z }, { 1, g->ColorMax.x, g->ColorMax.y, g->ColorMax.z } };
						g->GradientMax.Alphas = { { 0, g->ColorMax.w }, { 1, g->ColorMax.w } };
					}
					if (next == ParticleGradientMode::TwoColors && g->Mode == ParticleGradientMode::Color)
						g->ColorMin = g->ColorMax;
					if (next == ParticleGradientMode::TwoGradients && g->Mode == ParticleGradientMode::Gradient)
						g->GradientMin = g->GradientMax;
					g->Mode = next;
					changed = true;
				}
			}
			ImGui::EndPopup();
		}
		PopPopupStyle();
		changed |= ColorPopup("##colorMin", &g->ColorMin);
		changed |= ColorPopup("##colorMax", &g->ColorMax);
		changed |= GradientPopup("##gradMin", &g->GradientMin);
		changed |= GradientPopup("##gradMax", &g->GradientMax);
		ImGui::PopID();
		UnityGUI::EndFieldRow(row);
		return changed;
	}
}
