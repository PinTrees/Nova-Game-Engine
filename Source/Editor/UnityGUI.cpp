#include "pch.h"
#include "UnityGUI.h"
#include "EditorTheme.h"
#include <unordered_map>
#include <algorithm>

namespace
{
	using UnityGUI::kRowHeight;
	using UnityGUI::kRowStep;
	using UnityGUI::kBaseIndent;
	using UnityGUI::kNestIndent;

	// ---- Unity 다크 테마 색 ----
	const ImU32 kText        = IM_COL32(196, 196, 196, 255);
	const ImU32 kTextBright  = IM_COL32(230, 230, 230, 255);
	const ImU32 kTextDim     = IM_COL32(140, 140, 140, 255);
	const ImU32 kFieldBg     = IM_COL32(42, 42, 42, 255);
	const ImU32 kFieldBorder = IM_COL32(26, 26, 26, 255);
	const ImU32 kDropBg      = IM_COL32(81, 81, 81, 255);
	const ImU32 kDropBgHover = IM_COL32(94, 94, 94, 255);
	const ImU32 kDropBorder  = IM_COL32(45, 45, 45, 255);
	const ImU32 kHeaderBg    = IM_COL32(63, 63, 63, 255);
	const ImU32 kSubHeaderBg = IM_COL32(50, 50, 50, 255);
	const ImU32 kSeparator   = IM_COL32(30, 30, 30, 255);
	const ImU32 kFocus       = IM_COL32(62, 125, 190, 255);

	// 행 높이(18px) 안에서 글자를 세로 중앙에 두기 위한 정수 패딩
	float FramePadY() { return floorf((UnityGUI::kRowHeight - ImGui::GetFontSize()) * 0.5f); }
	// 텍스트 그리기 y 위치를 정수 픽셀로 맞춘다
	float TextY(float rowTop, float rowH, float fontSize) { return floorf(rowTop + (rowH - fontSize) * 0.5f + 0.5f); }

	ImVec4 V4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

	struct Row
	{
		ImVec2 p;          // 행 시작(화면 좌표)
		float w;           // 사용 가능한 폭
		float fieldX;      // 필드 열 시작 x (화면 좌표)
		float fieldW;      // 필드 열 폭
	};

	float FieldOffset(float w)
	{
		return std::clamp(w * 0.408f, 90.0f, (std::max)(90.0f, w - 90.0f));
	}

	Row BeginRow(const char* label, int indent, bool bold = false)
	{
		Row r;
		r.p = ImGui::GetCursorScreenPos();
		r.w = ImGui::GetContentRegionAvail().x;
		r.fieldX = r.p.x + FieldOffset(r.w);
		r.fieldW = (std::max)(20.0f, r.w - FieldOffset(r.w) - 12.0f);

		if (label && label[0])
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImFont* font = bold ? UnityGUI::BoldFont() : ImGui::GetFont();
			float fs = font ? font->FontSize : ImGui::GetFontSize();
			const float lx = r.p.x + kBaseIndent + indent * kNestIndent;
			ImVec4 clip(r.p.x, r.p.y, r.fieldX - 6.0f, r.p.y + kRowHeight);   // 레이블은 필드 열 직전까지만 표시
			dl->AddText(font, fs, ImVec2(lx, TextY(r.p.y, kRowHeight, fs)), kText, label, nullptr, 0.0f, &clip);
		}
		ImGui::SetCursorScreenPos(ImVec2(r.fieldX, r.p.y));
		return r;
	}

	void EndRow(const Row& r)
	{
		ImGui::SetCursorScreenPos(r.p);
		ImGui::Dummy(ImVec2(r.w, kRowStep));
		ImGui::SetCursorScreenPos(ImVec2(r.p.x, r.p.y + kRowStep));
	}

	// 필드 공통 스타일 (어두운 입력 박스)
	void PushFieldStyle(float padX = 6.0f)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, FramePadY()));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kFieldBg));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4(IM_COL32(50, 50, 50, 255)));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, V4(IM_COL32(50, 50, 50, 255)));
		ImGui::PushStyleColor(ImGuiCol_Border, V4(kFieldBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(kTextBright));
	}
	void PopFieldStyle() { ImGui::PopStyleColor(5); ImGui::PopStyleVar(3); }

	bool FloatInput(const char* id, float* v, float width, float leftPad = 6.0f)
	{
		PushFieldStyle(leftPad);
		ImGui::SetNextItemWidth(width);
		bool changed = ImGui::InputScalar(id, ImGuiDataType_Float, v, nullptr, nullptr, "%g", ImGuiInputTextFlags_AutoSelectAll);
		PopFieldStyle();
		return changed;
	}

	void CheckBox(const char* id, bool* v, ImVec2 pos, bool* changed)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGui::SetCursorScreenPos(pos);
		if (ImGui::InvisibleButton(id, ImVec2(14, 14)))
		{
			*v = !*v;
			if (changed) *changed = true;
		}
		bool hover = ImGui::IsItemHovered();
		dl->AddRectFilled(pos, ImVec2(pos.x + 14, pos.y + 14), hover ? IM_COL32(52, 52, 52, 255) : kFieldBg, 2.0f);
		dl->AddRect(pos, ImVec2(pos.x + 14, pos.y + 14), hover ? IM_COL32(90, 90, 90, 255) : kFieldBorder, 2.0f);
		if (*v)
			UnityGUI::DrawIcon(dl, "check", ImVec2(pos.x, pos.y), 14.0f);
	}
}

namespace UnityGUI
{
	// ---------- 리소스 ----------
	ImTextureID Icon(const char* name)
	{
		static std::unordered_map<std::string, ComPtr<GfxShaderResourceView>> cache;
		auto it = cache.find(name);
		if (it == cache.end())
		{
			std::wstring path = L"\\ProjectSetting\\icons\\svg\\png\\" + string_to_wstring(name) + L".png";
			it = cache.emplace(name, ResourceManager::GetI()->LoadTexture(path)).first;
		}
		return (ImTextureID)it->second.Get();
	}

	void DrawIcon(ImDrawList* dl, const char* name, ImVec2 pos, float size, ImU32 tint)
	{
		ImTextureID tex = Icon(name);
		if (tex)
			dl->AddImage(tex, pos, ImVec2(pos.x + size, pos.y + size), ImVec2(0, 0), ImVec2(1, 1), tint);
	}

	ImFont* BoldFont()
	{
		ImGuiIO& io = ImGui::GetIO();
		return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
	}

	ImFont* TitleFont()
	{
		ImGuiIO& io = ImGui::GetIO();
		return io.Fonts->Fonts.Size > 3 ? io.Fonts->Fonts[3] : BoldFont();
	}

	ImFont* HeaderFont()
	{
		ImGuiIO& io = ImGui::GetIO();
		return io.Fonts->Fonts.Size > 4 ? io.Fonts->Fonts[4] : BoldFont();
	}

	void Spacing(float height)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(1, height));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
	}

	FieldRow BeginFieldRow(const char* label, int indent)
	{
		const Row r = BeginRow(label, indent);
		return FieldRow{ r.p, r.w, r.fieldX, r.fieldW };
	}

	void EndFieldRow(const FieldRow& row, float height)
	{
		ImGui::SetCursorScreenPos(row.p);
		ImGui::Dummy(ImVec2(row.w, height));
		ImGui::SetCursorScreenPos(ImVec2(row.p.x, row.p.y + height));
	}

	bool FloatBox(const char* id, float* value, ImVec2 pos, float width)
	{
		ImGui::SetCursorScreenPos(pos);
		return FloatInput(id, value, width);
	}

	// ---------- 행 위젯 ----------
	bool Dropdown(const char* label, int* index, const char* const* items, int count, int indent, bool disabled)
	{
		Row r = BeginRow(label, indent);
		bool changed = false;
		ImGui::PushID(label);

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, FramePadY()));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kDropBg));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4(kDropBgHover));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, V4(kDropBg));
		ImGui::PushStyleColor(ImGuiCol_Border, V4(kDropBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(kTextBright));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(48, 48, 48, 255)));

		int cur = std::clamp(*index, 0, (std::max)(0, count - 1));
		if (disabled) ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(r.fieldW);
		if (ImGui::BeginCombo("##dd", items[cur], ImGuiComboFlags_NoArrowButton))
		{
			for (int i = 0; i < count; ++i)
			{
				if (ImGui::Selectable(items[i], i == cur))
				{
					*index = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
		DrawIcon(ImGui::GetWindowDrawList(), "dropdown", ImVec2(rmax.x - 18.0f, rmin.y + 3.0f), 12.0f);
		if (disabled) ImGui::EndDisabled();

		ImGui::PopStyleColor(6);
		ImGui::PopStyleVar(5);
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	const char* const* LayerNames(int* count)
	{
		static const char* kLayers[] = { "Default", "TransparentFX", "Ignore Raycast", "Water", "UI" };
		if (count) *count = 5;
		return kLayers;
	}

	bool MaskField(const char* label, uint32* mask, int indent)
	{
		int count = 0;
		const char* const* names = LayerNames(&count);
		const uint32 all = (1u << count) - 1u;
		const uint32 m = *mask & all;

		// 미리보기 글자: Unity 와 같이 Nothing / Everything / 레이어 하나의 이름 / Mixed...
		std::string preview;
		if (m == 0) preview = "Nothing";
		else if (m == all) preview = "Everything";
		else
		{
			int n = 0, last = 0;
			for (int i = 0; i < count; ++i) if (m & (1u << i)) { ++n; last = i; }
			preview = n == 1 ? names[last] : "Mixed...";
		}

		Row r = BeginRow(label, indent);
		bool changed = false;
		ImGui::PushID(label);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, FramePadY()));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kDropBg));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4(kDropBgHover));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, V4(kDropBg));
		ImGui::PushStyleColor(ImGuiCol_Border, V4(kDropBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(kTextBright));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(48, 48, 48, 255)));

		ImGui::SetNextItemWidth(r.fieldW);
		if (ImGui::BeginCombo("##mask", preview.c_str(), ImGuiComboFlags_NoArrowButton))
		{
			if (ImGui::MenuItem("Nothing", nullptr, m == 0)) { *mask = 0; changed = true; }
			if (ImGui::MenuItem("Everything", nullptr, m == all)) { *mask = all; changed = true; }
			for (int i = 0; i < count; ++i)
			{
				const bool on = (m & (1u << i)) != 0;
				if (ImGui::MenuItem(names[i], nullptr, on))
				{
					*mask = on ? (m & ~(1u << i)) : (m | (1u << i));
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
		DrawIcon(ImGui::GetWindowDrawList(), "dropdown", ImVec2(rmax.x - 18.0f, rmin.y + 3.0f), 12.0f);

		ImGui::PopStyleColor(6);
		ImGui::PopStyleVar(5);
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool Toggle(const char* label, bool* value, int indent)
	{
		Row r = BeginRow(label, indent);
		bool changed = false;
		ImGui::PushID(label);
		CheckBox("##tg", value, ImVec2(r.fieldX, r.p.y + 2.0f), &changed);
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool Toggle3(const char* label, bool* xyz, int indent)
	{
		Row r = BeginRow(label, indent);
		bool changed = false;
		ImGui::PushID(label);
		static const char* names[3] = { "X", "Y", "Z" };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float fs = ImGui::GetFontSize();
		for (int i = 0; i < 3; ++i)
		{
			const float x = r.fieldX + i * 34.0f;
			ImGui::PushID(i);
			bool c = false;
			CheckBox("##t3", &xyz[i], ImVec2(x, r.p.y + 2.0f), &c);
			changed |= c;
			ImGui::PopID();
			dl->AddText(ImVec2(floorf(x + 18.0f), TextY(r.p.y, kRowHeight, fs)), kText, names[i]);
		}
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	void ValueLabel(const char* label, const char* value, int indent)
	{
		Row r = BeginRow(label, indent);
		const float fs = ImGui::GetFontSize();
		ImVec4 clip(r.fieldX, r.p.y, r.p.x + r.w, r.p.y + kRowHeight);
		ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fs, ImVec2(floorf(r.fieldX + 2.0f), TextY(r.p.y, kRowHeight, fs)), kTextDim, value, nullptr, 0.0f, &clip);
		EndRow(r);
	}

	bool Float(const char* label, float* value, int indent, const char* innerLabel)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImGui::PushID(innerLabel ? innerLabel : "f");
		float pad = innerLabel ? 40.0f : 6.0f;
		bool changed = FloatInput("##f", value, r.fieldW, pad);
		if (innerLabel)
		{
			ImVec2 rmin = ImGui::GetItemRectMin();
			ImGui::GetWindowDrawList()->AddText(ImVec2(rmin.x + 6.0f, TextY(rmin.y, kRowHeight, ImGui::GetFontSize())), kTextDim, innerLabel);
		}
		ImGui::PopID();
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool Int(const char* label, int* value, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		PushFieldStyle();
		ImGui::SetNextItemWidth(r.fieldW);
		bool changed = ImGui::InputScalar("##i", ImGuiDataType_S32, value, nullptr, nullptr, "%d", ImGuiInputTextFlags_AutoSelectAll);
		PopFieldStyle();
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool ToggleLeft(const char* text, bool* value, int indent, bool disabled, bool withTargetIcon)
	{
		Row r;
		r.p = ImGui::GetCursorScreenPos();
		r.w = ImGui::GetContentRegionAvail().x;
		bool changed = false;
		ImGui::PushID(text);
		float x = r.p.x + kBaseIndent + indent * kNestIndent;
		if (disabled) ImGui::BeginDisabled();
		CheckBox("##tl", value, ImVec2(x, r.p.y + 2.0f), &changed);
		if (disabled) ImGui::EndDisabled();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float tx = x + 20.0f;
		ImU32 col = disabled ? kTextDim : kText;
		if (withTargetIcon)
		{
			DrawIcon(dl, "target", ImVec2(tx - 2.0f, r.p.y + 3.0f), 12.0f, col);
			tx += 14.0f;
		}
		dl->AddText(ImVec2(tx, TextY(r.p.y, kRowHeight, ImGui::GetFontSize())), col, text);
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	void HelpBox(const char* text, bool warning, int indent)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float x0 = p.x + kBaseIndent - 2.0f + indent * kNestIndent;
		const float x1 = p.x + w - 12.0f;
		const float iconW = 34.0f;
		float wrap = x1 - x0 - iconW - 10.0f;
		ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrap);
		float h = (std::max)(ts.y + 14.0f, 38.0f);
		dl->AddRectFilled(ImVec2(x0, p.y + 2.0f), ImVec2(x1, p.y + 2.0f + h), IM_COL32(58, 58, 58, 255), 3.0f);
		dl->AddRect(ImVec2(x0, p.y + 2.0f), ImVec2(x1, p.y + 2.0f + h), IM_COL32(30, 30, 30, 255), 3.0f);
		DrawIcon(dl, warning ? "warning" : "info_box", ImVec2(x0 + 6.0f, p.y + 2.0f + (h - 26.0f) * 0.5f), 26.0f);
		ImGui::PushTextWrapPos(0.0f);
		dl->AddText(nullptr, 0.0f, ImVec2(x0 + iconW + 6.0f, floorf(p.y + 2.0f + (h - ts.y) * 0.5f)), kTextBright, text, nullptr, wrap);
		ImGui::PopTextWrapPos();
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h + 6.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 6.0f));
	}

	bool TemperatureBar(const char* label, float* kelvin, float minK, float maxK, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		bool changed = false;

		// 그라디언트 바 (주황 → 흰색 → 하늘색)
		ImVec2 p0(r.fieldX, r.p.y);
		ImVec2 p1(r.fieldX + r.fieldW, r.p.y + kRowHeight);
		ImGui::SetCursorScreenPos(p0);
		ImGui::InvisibleButton("##bar", ImVec2(r.fieldW, kRowHeight));
		if (ImGui::IsItemActive())
		{
			float t = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / r.fieldW, 0.0f, 1.0f);
			*kelvin = minK + (maxK - minK) * t;
			changed = true;
		}
		const ImU32 cOrange = IM_COL32(255, 118, 0, 255), cWhite = IM_COL32(255, 255, 255, 255), cBlue = IM_COL32(150, 190, 255, 255);
		float mid = p0.x + r.fieldW * 0.5f;
		dl->AddRectFilledMultiColor(p0, ImVec2(mid, p1.y), cOrange, cWhite, cWhite, cOrange);
		dl->AddRectFilledMultiColor(ImVec2(mid, p0.y), p1, cWhite, cBlue, cBlue, cWhite);
		dl->AddRect(p0, p1, kFieldBorder);
		float t = std::clamp((*kelvin - minK) / (maxK - minK), 0.0f, 1.0f);
		float mx = floorf(p0.x + r.fieldW * t);
		dl->AddRectFilled(ImVec2(mx - 1.0f, p0.y - 1.0f), ImVec2(mx + 1.0f, p1.y + 1.0f), IM_COL32(60, 60, 60, 255));
		dl->AddRect(ImVec2(mx - 2.0f, p0.y - 1.0f), ImVec2(mx + 2.0f, p1.y + 1.0f), IM_COL32(230, 230, 230, 255));
		EndRow(r);

		// 두 번째 행: Kelvin 숫자 입력 + "Kelvin" 텍스트
		ImVec2 q = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(r.fieldX, q.y));
		float inputW = r.fieldW - 58.0f;
		ImGui::PushID("kelvin");
		changed |= FloatInput("##k", kelvin, inputW);
		ImGui::PopID();
		dl->AddText(ImVec2(r.fieldX + inputW + 8.0f, TextY(q.y, kRowHeight, ImGui::GetFontSize())), kText, "Kelvin");
		ImGui::SetCursorScreenPos(q);
		ImGui::Dummy(ImVec2(r.w, kRowStep));
		ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + kRowStep));

		ImGui::PopID();
		return changed;
	}

	bool FoldoutPlain(const char* label, int indent, bool defaultOpen)
	{
		ImGuiStorage* st = ImGui::GetStateStorage();
		ImGuiID id = ImGui::GetID(label);
		bool open = st->GetBool(id, defaultOpen);
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		const float h = kRowStep;

		ImGui::PushID(label);
		if (ImGui::InvisibleButton("##fold", ImVec2(w, h)))
		{
			open = !open;
			st->SetBool(id, open);
		}
		ImGui::PopID();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		float x = p.x + 6.0f + indent * kNestIndent;
		DrawIcon(dl, open ? "arrow_down" : "arrow_right", ImVec2(x, p.y + 5.0f), 10.0f);
		ImFont* bold = BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(x + 15.0f, TextY(p.y, h, bold->FontSize)), kTextBright, label);
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		return open;
	}

	bool MaterialsHeader(const char* label, int* count)
	{
		ImGuiStorage* st = ImGui::GetStateStorage();
		ImGuiID id = ImGui::GetID(label);
		bool open = st->GetBool(id, true);
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		const float h = kRowStep;
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImGui::PushID(label);
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::InvisibleButton("##fold", ImVec2(w, h)))
		{
			open = !open;
			st->SetBool(id, open);
		}
		float x = p.x + 6.0f;
		DrawIcon(dl, open ? "arrow_down" : "arrow_right", ImVec2(x, p.y + 5.0f), 10.0f);
		ImFont* bold = BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(x + 15.0f, TextY(p.y, h, bold->FontSize)), kTextBright, label);

		// 오른쪽 크기 입력 (Unity: Materials  [1])
		ImGui::SetCursorScreenPos(ImVec2(p.x + w - 12.0f - 62.0f, p.y + 1.0f));
		PushFieldStyle();
		ImGui::SetNextItemWidth(62.0f);
		ImGui::InputScalar("##count", ImGuiDataType_S32, count, nullptr, nullptr, "%d", ImGuiInputTextFlags_AutoSelectAll);
		PopFieldStyle();
		ImGui::PopID();
		if (*count < 0) *count = 0;
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		return open;
	}

	bool ElementRow(const char* label, const char* text, const char* iconName)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float h = 26.0f;
		const float x0 = p.x + 14.0f, x1 = p.x + w - 12.0f;
		dl->AddRectFilled(ImVec2(x0, p.y + 1.0f), ImVec2(x1, p.y + h), IM_COL32(50, 50, 50, 255), 2.0f);
		dl->AddRect(ImVec2(x0, p.y + 1.0f), ImVec2(x1, p.y + h), kFieldBorder, 2.0f);
		DrawIcon(dl, "handle", ImVec2(x0 + 2.0f, p.y + 5.0f), 16.0f);
		dl->AddText(ImVec2(x0 + 22.0f, TextY(p.y + 1.0f, h - 1.0f, ImGui::GetFontSize())), kText, label);

		// 오브젝트 필드
		float fx0 = x0 + (x1 - x0) * 0.40f;
		float fx1 = x1 - 6.0f;
		ImVec2 f0(fx0, p.y + 4.0f), f1(fx1, p.y + h - 3.0f);
		dl->AddRectFilled(f0, f1, kFieldBg, 3.0f);
		dl->AddRect(f0, f1, kFieldBorder, 3.0f);
		float tx = f0.x + 6.0f;
		if (iconName)
		{
			DrawIcon(dl, iconName, ImVec2(f0.x + 4.0f, f0.y + 1.0f), 16.0f);
			tx += 20.0f;
		}
		bool hasValue = text && strncmp(text, "None", 4) != 0;
		dl->AddText(ImVec2(tx, TextY(f0.y, f1.y - f0.y, ImGui::GetFontSize())), hasValue ? kTextBright : kTextDim, text);
		ImGui::PushID(label);
		ImGui::SetCursorScreenPos(ImVec2(f1.x - 20.0f, f0.y));
		bool clicked = ImGui::InvisibleButton("##pick", ImVec2(20.0f, f1.y - f0.y));
		DrawIcon(dl, "target", ImVec2(f1.x - 18.0f, f0.y + 1.0f), 16.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(196, 196, 196, 255));
		ImGui::PopID();
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h + 2.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 2.0f));
		return clicked;
	}

	void PlusMinus(bool* plus, bool* minus)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float x1 = p.x + w - 12.0f;
		const float bw = 28.0f, bh = 22.0f;
		ImGui::PushID("plusminus");
		for (int i = 0; i < 2; ++i)
		{
			ImVec2 b0(x1 - bw * (2 - i), p.y + 1.0f), b1(b0.x + bw, b0.y + bh);
			ImGui::SetCursorScreenPos(b0);
			bool clicked = ImGui::InvisibleButton(i == 0 ? "##plus" : "##minus", ImVec2(bw, bh));
			bool hover = ImGui::IsItemHovered();
			dl->AddRectFilled(b0, b1, hover ? IM_COL32(72, 72, 72, 255) : IM_COL32(58, 58, 58, 255), 2.0f);
			dl->AddRect(b0, b1, kFieldBorder, 2.0f);
			DrawIcon(dl, i == 0 ? "plus" : "minus", ImVec2(b0.x + 6.0f, b0.y + 3.0f), 16.0f);
			if (clicked)
			{
				if (i == 0 && plus) *plus = true;
				if (i == 1 && minus) *minus = true;
			}
		}
		ImGui::PopID();
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, bh + 6.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + bh + 6.0f));
	}

	bool IconButtonRow(const char* label, const char* iconName, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImVec2 b0(r.fieldX, r.p.y);
		ImVec2 b1(b0.x + 34.0f, b0.y + 24.0f);
		ImGui::SetCursorScreenPos(b0);
		bool clicked = ImGui::InvisibleButton("##btn", ImVec2(34.0f, 24.0f));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(b0, b1, ImGui::IsItemHovered() ? IM_COL32(30, 30, 30, 255) : IM_COL32(24, 24, 24, 255), 3.0f);
		dl->AddRect(b0, b1, IM_COL32(20, 20, 20, 255), 3.0f);
		DrawIcon(dl, iconName, ImVec2(b0.x + 9.0f, b0.y + 4.0f), 16.0f);
		ImGui::PopID();
		ImGui::SetCursorScreenPos(r.p);
		ImGui::Dummy(ImVec2(r.w, 28.0f));
		ImGui::SetCursorScreenPos(ImVec2(r.p.x, r.p.y + 28.0f));
		return clicked;
	}

	void MaterialPanel(const char* name, const char* shaderName)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float headerH = 44.0f;
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + headerH), kHeaderBg);
		dl->AddLine(p, ImVec2(p.x + w, p.y), kSeparator);
		DrawIcon(dl, "material_ball", ImVec2(p.x + 12.0f, p.y + 6.0f), 32.0f);
		ImFont* bold = BoldFont();
		std::string title = std::string(name) + " (Material)";
		dl->AddText(bold, bold->FontSize, ImVec2(p.x + 52.0f, TextY(p.y, headerH - 8.0f, bold->FontSize)), kTextBright, title.c_str());
		DrawIcon(dl, "help", ImVec2(p.x + w - 66.0f, p.y + 5.0f), 16.0f);
		DrawIcon(dl, "presets", ImVec2(p.x + w - 44.0f, p.y + 5.0f), 16.0f);
		DrawIcon(dl, "kebab", ImVec2(p.x + w - 22.0f, p.y + 5.0f), 16.0f);

		// Shader 행: [Shader] [ 드롭다운 ] [Edit...] [≡]
		const float rowY = p.y + 24.0f;
		dl->AddText(ImVec2(p.x + 52.0f, TextY(rowY, kRowHeight, ImGui::GetFontSize())), kText, "Shader");
		float fx0 = p.x + 104.0f;
		float fx1 = p.x + w - 96.0f;
		dl->AddRectFilled(ImVec2(fx0, rowY), ImVec2(fx1, rowY + kRowHeight), IM_COL32(64, 64, 64, 255), 3.0f);
		dl->AddRect(ImVec2(fx0, rowY), ImVec2(fx1, rowY + kRowHeight), kDropBorder, 3.0f);
		dl->AddText(ImVec2(fx0 + 6.0f, TextY(rowY, kRowHeight, ImGui::GetFontSize())), kTextDim, shaderName);
		DrawIcon(dl, "dropdown", ImVec2(fx1 - 16.0f, rowY + 3.0f), 12.0f, IM_COL32(150, 150, 150, 255));
		ImVec2 e0(fx1 + 6.0f, rowY), e1(e0.x + 44.0f, rowY + kRowHeight);
		dl->AddRectFilled(e0, e1, IM_COL32(72, 72, 72, 255), 3.0f);
		dl->AddRect(e0, e1, kDropBorder, 3.0f);
		dl->AddText(ImVec2(e0.x + 6.0f, TextY(rowY, kRowHeight, ImGui::GetFontSize())), kTextBright, "Edit...");
		ImVec2 l0(e1.x + 4.0f, rowY), l1(l0.x + 30.0f, rowY + kRowHeight);
		dl->AddRectFilled(l0, l1, IM_COL32(72, 72, 72, 255), 3.0f);
		dl->AddRect(l0, l1, kDropBorder, 3.0f);
		DrawIcon(dl, "list", ImVec2(l0.x + 4.0f, rowY + 1.0f), 16.0f);
		DrawIcon(dl, "dropdown", ImVec2(l0.x + 18.0f, rowY + 3.0f), 10.0f, IM_COL32(150, 150, 150, 255));

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, headerH + 34.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headerH + 34.0f));
	}

	bool Slider(const char* label, float* value, float minV, float maxV, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		bool changed = false;
		const float inputW = 48.0f;
		const float sliderW = r.fieldW - inputW - 6.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// 트랙 + 손잡이
		ImVec2 p0(r.fieldX, r.p.y);
		ImGui::SetCursorScreenPos(p0);
		ImGui::InvisibleButton("##slider", ImVec2(sliderW, kRowHeight));
		if (ImGui::IsItemActive())
		{
			float t = std::clamp((ImGui::GetIO().MousePos.x - p0.x - 5.0f) / (sliderW - 10.0f), 0.0f, 1.0f);
			*value = minV + (maxV - minV) * t;
			changed = true;
		}
		float t = std::clamp((*value - minV) / (maxV - minV), 0.0f, 1.0f);
		float cy = p0.y + kRowHeight * 0.5f;
		dl->AddRectFilled(ImVec2(p0.x + 2, cy - 1.5f), ImVec2(p0.x + sliderW - 2, cy + 1.5f), kFieldBg, 1.5f);
		dl->AddRect(ImVec2(p0.x + 2, cy - 1.5f), ImVec2(p0.x + sliderW - 2, cy + 1.5f), kFieldBorder, 1.5f);
		float kx = p0.x + 5.0f + (sliderW - 10.0f) * t;
		dl->AddCircleFilled(ImVec2(kx, cy), 5.0f, IM_COL32(170, 170, 170, 255));
		dl->AddCircle(ImVec2(kx, cy), 5.0f, kFieldBorder);

		ImGui::SetCursorScreenPos(ImVec2(p0.x + sliderW + 6.0f, p0.y));
		changed |= FloatInput("##v", value, inputW);
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool TextField(const char* label, std::string* value, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		char buf[1024];
		strncpy_s(buf, value->c_str(), _TRUNCATE);
		PushFieldStyle();
		ImGui::SetNextItemWidth(r.fieldW);
		const bool changed = ImGui::InputText("##text", buf, sizeof(buf));
		PopFieldStyle();
		if (changed)
			*value = buf;
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool TextArea(const char* label, std::string* value, float height, int indent)
	{
		// Unity 의 여러 줄 텍스트 (Text 컴포넌트의 Text): 레이블 행 아래에 폭 전체 입력 상자
		if (label && label[0])
		{
			Row r = BeginRow(label, indent);
			EndRow(r);
		}
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		ImGui::PushID(label ? label : "##area");
		static std::string s_Buffer;
		s_Buffer = *value;
		s_Buffer.reserve(s_Buffer.size() + 4096);
		s_Buffer.resize(s_Buffer.size() + 4096, '\0');
		ImGui::SetCursorScreenPos(ImVec2(p.x + kBaseIndent + indent * kNestIndent, p.y));
		PushFieldStyle();
		const bool changed = ImGui::InputTextMultiline("##area", s_Buffer.data(), s_Buffer.size(),
			ImVec2(w - kBaseIndent - indent * kNestIndent - 12.0f, height));
		PopFieldStyle();
		if (changed)
			*value = std::string(s_Buffer.c_str());
		ImGui::PopID();
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height + 4.0f));
		ImGui::Dummy(ImVec2(w, 0.0f));
		return changed;
	}

	bool FloatCell(const char* id, float* value, ImVec2 pos, float width)
	{
		ImGui::SetCursorScreenPos(pos);
		ImGui::PushID(id);
		const bool changed = FloatInput("##cell", value, width);
		ImGui::PopID();
		return changed;
	}

	void SliderCaptions(const char* left, const char* right)
	{
		// 바로 위 Slider 행의 트랙 양 끝 아래에 작은 글자 (Unity 의 Priority High/Low, Stereo Pan Left/Right)
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float fieldX = p.x + FieldOffset(w);
		const float fieldW = (std::max)(20.0f, w - FieldOffset(w) - 12.0f);
		const float sliderW = fieldW - 48.0f - 6.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImFont* font = ImGui::GetFont();
		const float fs = floorf(ImGui::GetFontSize() * 0.85f);
		const float y = p.y - 5.0f;
		dl->AddText(font, fs, ImVec2(fieldX + 2.0f, y), kTextDim, left);
		const float rw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, right).x;
		dl->AddText(font, fs, ImVec2(floorf(fieldX + sliderW - 2.0f - rw), y), kTextDim, right);
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, 9.0f));
	}

	void EmptyListBox(const char* header, const char* emptyText)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float x0 = p.x + 20.0f, x1 = p.x + w - 14.0f;
		const float y0 = p.y + 4.0f;
		const float headerH = 20.0f, bodyH = 28.0f, footH = 22.0f;

		dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + headerH + bodyH), kFieldBg, 2.0f);
		dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + headerH), IM_COL32(58, 58, 58, 255), 2.0f);
		dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y0 + headerH + bodyH), kFieldBorder, 2.0f);

		ImVec2 hs = ImGui::CalcTextSize(header);
		dl->AddText(ImVec2((x0 + x1 - hs.x) * 0.5f, y0 + (headerH - hs.y) * 0.5f), kText, header);
		ImVec2 es = ImGui::CalcTextSize(emptyText);
		dl->AddText(ImVec2(x0 + 8.0f, y0 + headerH + (bodyH - es.y) * 0.5f), kTextBright, emptyText);

		// +/- 버튼 (오른쪽 하단)
		float fy = y0 + headerH + bodyH;
		dl->AddRectFilled(ImVec2(x1 - 60.0f, fy), ImVec2(x1, fy + footH), IM_COL32(58, 58, 58, 255), 2.0f);
		dl->AddRect(ImVec2(x1 - 60.0f, fy), ImVec2(x1, fy + footH), kFieldBorder, 2.0f);
		DrawIcon(dl, "plus", ImVec2(x1 - 52.0f, fy + 3.0f), 16.0f);
		DrawIcon(dl, "minus", ImVec2(x1 - 26.0f, fy + 3.0f), 16.0f, IM_COL32(120, 120, 120, 255));

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, headerH + bodyH + footH + 8.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headerH + bodyH + footH + 8.0f));
	}

	static bool AxisField(const char* axis, float* v, float cellW)
	{
		const float labelW = 13.0f;
		ImVec2 p = ImGui::GetCursorScreenPos();
		bool changed = false;

		// 축 레이블: 좌우로 드래그하면 값이 바뀐다 (Unity 와 동일)
		ImGui::InvisibleButton("##lbl", ImVec2(labelW, kRowHeight));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			float speed = ImGui::GetIO().KeyShift ? 0.5f : 0.05f;
			*v += ImGui::GetIO().MouseDelta.x * speed;
			changed = true;
		}
		ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 1.0f, TextY(p.y, kRowHeight, ImGui::GetFontSize())), kText, axis);

		ImGui::SetCursorScreenPos(ImVec2(p.x + labelW + 1.0f, p.y));
		ImGui::PushID(axis);
		changed |= FloatInput("##v", v, cellW - labelW - 6.0f);
		ImGui::PopID();
		return changed;
	}

	bool Vector3(const char* label, float* xyz, bool showLinkIcon, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		bool changed = false;

		if (showLinkIcon)
			DrawIcon(ImGui::GetWindowDrawList(), "link_off", ImVec2(r.fieldX - 21.0f, r.p.y + 1.0f), 16.0f, IM_COL32(150, 150, 150, 255));

		const float gap = 4.0f;
		const float cell = (r.fieldW + gap) / 3.0f;
		const char* axes[3] = { "X", "Y", "Z" };
		for (int i = 0; i < 3; ++i)
		{
			ImGui::SetCursorScreenPos(ImVec2(r.fieldX + cell * i, r.p.y));
			changed |= AxisField(axes[i], &xyz[i], cell - gap);
		}
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool Vector2Pair(const char* label, const char* n0, float* a, const char* n1, float* b, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		bool changed = false;
		const float gap = 4.0f;
		const float cell = (r.fieldW + gap) / 3.0f;
		ImGui::SetCursorScreenPos(ImVec2(r.fieldX, r.p.y));
		changed |= AxisField(n0, a, cell - gap);
		ImGui::SetCursorScreenPos(ImVec2(r.fieldX + cell, r.p.y));
		changed |= AxisField(n1, b, cell - gap);
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool Color(const char* label, float* rgba, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p0(r.fieldX, r.p.y);
		ImVec2 p1(r.fieldX + r.fieldW, r.p.y + kRowHeight);

		ImGui::SetCursorScreenPos(p0);
		bool open = ImGui::InvisibleButton("##swatch", ImVec2(r.fieldW, kRowHeight));
		dl->AddRectFilled(p0, p1, kFieldBg, 3.0f);
		dl->AddRectFilled(ImVec2(p0.x + 2, p0.y + 2), ImVec2(p1.x - 22.0f, p1.y - 2), ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.0f)), 2.0f);
		dl->AddRect(p0, p1, kFieldBorder, 3.0f);
		DrawIcon(dl, "eyedropper", ImVec2(p1.x - 19.0f, p0.y + 1.0f), 16.0f);

		bool changed = false;
		if (open)
			ImGui::OpenPopup("##colorpopup");
		if (ImGui::BeginPopup("##colorpopup"))
		{
			changed = ImGui::ColorPicker4("##picker", rgba, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
			ImGui::EndPopup();
		}
		ImGui::PopID();
		EndRow(r);
		return changed;
	}

	bool ObjectField(const char* label, const char* text, int indent, const char* iconName)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p0(r.fieldX, r.p.y);
		ImVec2 p1(r.fieldX + r.fieldW, r.p.y + kRowHeight);
		dl->AddRectFilled(p0, p1, kFieldBg, 3.0f);
		dl->AddRect(p0, p1, kFieldBorder, 3.0f);
		float tx = p0.x + 6.0f;
		if (iconName)
		{
			DrawIcon(dl, iconName, ImVec2(p0.x + 4.0f, p0.y + 1.0f), 16.0f);
			tx += 20.0f;
		}
		bool hasValue = text && strncmp(text, "None", 4) != 0;
		dl->AddText(ImVec2(tx, TextY(p0.y, kRowHeight, ImGui::GetFontSize())), hasValue ? kTextBright : kTextDim, text);

		ImGui::SetCursorScreenPos(ImVec2(p1.x - 20.0f, p0.y));
		bool clicked = ImGui::InvisibleButton("##pick", ImVec2(20.0f, kRowHeight));
		DrawIcon(dl, "target", ImVec2(p1.x - 18.0f, p0.y + 1.0f), 16.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(196, 196, 196, 255));
		ImGui::PopID();
		EndRow(r);
		return clicked;
	}

	bool GameObjectField(const char* label, uint64* fileID, int indent)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		GameObject* go = (fileID && *fileID != 0 && scene) ? scene->FindByFileID(*fileID) : nullptr;
		const bool missing = fileID && *fileID != 0 && go == nullptr;
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p0(r.fieldX, r.p.y);
		ImVec2 p1(r.fieldX + r.fieldW, r.p.y + kRowHeight);
		dl->AddRectFilled(p0, p1, kFieldBg, 3.0f);
		dl->AddRect(p0, p1, kFieldBorder, 3.0f);
		DrawIcon(dl, "gameobject", ImVec2(p0.x + 4.0f, p0.y + 1.0f), 16.0f);
		const std::string text = go ? go->GetName() : (missing ? std::string("Missing (GameObject)") : std::string("None (GameObject)"));
		dl->AddText(ImVec2(p0.x + 26.0f, TextY(p0.y, kRowHeight, ImGui::GetFontSize())), go ? kTextBright : (missing ? IM_COL32(220, 110, 110, 255) : kTextDim), text.c_str());
		bool changed = false;
		// Hierarchy 에서 끌어다 놓기
		ImGui::SetCursorScreenPos(p0);
		ImGui::InvisibleButton("##slot", ImVec2((std::max)(1.0f, r.fieldW - 20.0f), kRowHeight));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				if (GameObject* dropped = *static_cast<GameObject* const*>(pl->Data))
					if (fileID) { *fileID = dropped->GetFileID(); changed = true; }
			ImGui::EndDragDropTarget();
		}
		// x: 비우기
		ImGui::SetCursorScreenPos(ImVec2(p1.x - 20.0f, p0.y));
		if (ImGui::InvisibleButton("##clear", ImVec2(20.0f, kRowHeight)) && fileID && *fileID != 0)
		{
			*fileID = 0;
			changed = true;
		}
		dl->AddText(ImVec2(p1.x - 14.0f, TextY(p0.y, kRowHeight, ImGui::GetFontSize())), ImGui::IsItemHovered() ? IM_COL32_WHITE : kTextDim, "x");
		ImGui::PopID();
		EndRow(r);
		return changed;
	}
	void Label(const char* label, int indent, bool bold)
	{
		Row r = BeginRow(label, indent, bold);
		EndRow(r);
	}

	// ---------- 섹션 ----------
	bool Foldout(const char* label, int indent, bool defaultOpen, bool help)
	{
		ImGuiStorage* st = ImGui::GetStateStorage();
		ImGuiID id = ImGui::GetID(label);
		bool open = st->GetBool(id, defaultOpen);

		Spacing(5.0f);
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		const float h = 20.0f;

		ImGui::PushID(label);
		if (ImGui::InvisibleButton("##fold", ImVec2(w, h)))
		{
			open = !open;
			st->SetBool(id, open);
		}
		ImGui::PopID();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kSubHeaderBg);
		dl->AddLine(p, ImVec2(p.x + w, p.y), kSeparator);

		float x = p.x + 8.0f + indent * kNestIndent + 6.0f;
		DrawIcon(dl, open ? "arrow_down" : "arrow_right", ImVec2(x, p.y + 5.0f), 10.0f);
		ImFont* bold = BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(x + 16.0f, TextY(p.y, h, bold->FontSize)), kTextBright, label);
		if (help)
			DrawIcon(dl, "help", ImVec2(p.x + w - 24.0f, p.y + 2.0f), 16.0f);

		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		return open;
	}

	HeaderResult ComponentHeader(const char* id, const char* title, const char* iconName, bool* open, bool* enabled, bool canRemove)
	{
		HeaderResult res;
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		const float h = 22.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGui::PushID(id);

		// 전체 영역: 클릭 시 접기/펴기
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::InvisibleButton("##band", ImVec2(w, h)))
			*open = !*open;

		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHeaderBg);
		dl->AddLine(p, ImVec2(p.x + w, p.y), kSeparator);
		dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + w, p.y + h - 1), kSeparator);

		DrawIcon(dl, *open ? "arrow_down" : "arrow_right", ImVec2(p.x + 8.0f, p.y + 6.0f), 10.0f);
		float x = p.x + 30.0f;
		DrawIcon(dl, iconName, ImVec2(x, p.y + 3.0f), 16.0f);
		x += 20.0f;
		if (enabled)
		{
			ImGui::SetNextItemAllowOverlap();
			bool ch = false;
			CheckBox("##enabled", enabled, ImVec2(x, p.y + 4.0f), &ch);
			x += 20.0f;
		}
		else
			x += 12.0f;

		ImFont* bold = BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(x, TextY(p.y, h, bold->FontSize)), kTextBright, title);

		// 우측 아이콘: ? / 프리셋 / ⋮
		auto iconButton = [&](const char* name, float offsetFromRight, const char* btnId) -> bool
		{
			ImVec2 ip(p.x + w - offsetFromRight, p.y + 3.0f);
			ImGui::SetCursorScreenPos(ip);
			ImGui::SetNextItemAllowOverlap();
			bool clicked = ImGui::InvisibleButton(btnId, ImVec2(16, 16));
			DrawIcon(dl, name, ip, 16.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(196, 196, 196, 255));
			return clicked;
		};
		iconButton("help", 70.0f, "##help");
		iconButton("presets", 46.0f, "##presets");
		if (iconButton("kebab", 22.0f, "##kebab"))
			ImGui::OpenPopup("##compmenu");

		ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(48, 48, 48, 255)));
		if (ImGui::BeginPopup("##compmenu"))
		{
			if (ImGui::MenuItem("Reset"))
				res.action = HeaderAction::Reset;
			ImGui::Separator();
			if (ImGui::MenuItem("Remove Component", nullptr, false, canRemove))
				res.action = HeaderAction::Remove;
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();

		ImGui::PopID();
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
		ImGui::Dummy(ImVec2(w, h));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		res.open = *open;
		return res;
	}

	// ---------- GameObject 헤더 ----------
	bool GameObjectHeader(bool* active, std::string* name, bool* isStatic, std::string* tag, int* layer)
	{
		static const char* kTags[] = { "Untagged", "Respawn", "Finish", "EditorOnly", "MainCamera", "Player", "GameController" };
		int layerCount = 0;
		const char* const* kLayers = LayerNames(&layerCount);

		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		bool changed = false;

		ImGui::PushID("##gohdr");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

		// 큰 큐브 아이콘 + 드롭다운 화살표
		DrawIcon(dl, "gameobject", ImVec2(p.x + 8.0f, p.y + 8.0f), 32.0f);
		DrawIcon(dl, "dropdown", ImVec2(p.x + 19.0f, p.y + 42.0f), 10.0f, IM_COL32(150, 150, 150, 255));

		// 1행: [✓] 이름 ........ [ ] Static ▾
		const float nameX = p.x + 68.0f;
		const float row1 = p.y + 8.0f;
		bool chg = false;
		CheckBox("##active", active, ImVec2(p.x + 50.0f, row1 + 2.0f), &chg);
		changed |= chg;

		const float staticW = 78.0f;
		float nameW = w - 68.0f - staticW - 10.0f;
		ImGui::SetCursorScreenPos(ImVec2(nameX, row1));
		PushFieldStyle();
		ImGui::SetNextItemWidth(nameW);
		char buf[128];
		strncpy_s(buf, name->c_str(), _TRUNCATE);
		if (ImGui::InputText("##name", buf, sizeof(buf), ImGuiInputTextFlags_AutoSelectAll))
		{
			*name = buf;
			changed = true;
		}
		PopFieldStyle();

		ImGui::SetCursorScreenPos(ImVec2(nameX + nameW + 8.0f, row1 + 2.0f));
		CheckBox("##static", isStatic, ImVec2(nameX + nameW + 8.0f, row1 + 2.0f), &chg);
		changed |= chg;
		dl->AddText(ImVec2(nameX + nameW + 28.0f, TextY(row1, kRowHeight, ImGui::GetFontSize())), kText, "Static");
		DrawIcon(dl, "dropdown", ImVec2(p.x + w - 20.0f, row1 + 3.0f), 12.0f);

		// 2행: Tag [ ▾ ]  Layer [ ▾ ]
		const float row2 = p.y + 32.0f;
		const float half = (w - 68.0f - 12.0f) * 0.5f;
		dl->AddText(ImVec2(p.x + 44.0f, TextY(row2, kRowHeight, ImGui::GetFontSize())), kText, "Tag");

		auto combo = [&](const char* id, float x, float width, const char* const* items, int count, int* cur) -> bool
		{
			bool ch = false;
			ImGui::SetCursorScreenPos(ImVec2(x, row2));
			ImGui::PushID(id);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, FramePadY()));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 3.0f));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kDropBg));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4(kDropBgHover));
			ImGui::PushStyleColor(ImGuiCol_Border, V4(kDropBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, V4(kTextBright));
			ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(48, 48, 48, 255)));
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo("##c", items[*cur], ImGuiComboFlags_NoArrowButton))
			{
				for (int i = 0; i < count; ++i)
					if (ImGui::Selectable(items[i], i == *cur)) { *cur = i; ch = true; }
				ImGui::EndCombo();
			}
			ImVec2 rmax = ImGui::GetItemRectMax(), rmin = ImGui::GetItemRectMin();
			DrawIcon(ImGui::GetWindowDrawList(), "dropdown", ImVec2(rmax.x - 18.0f, rmin.y + 3.0f), 12.0f);
			ImGui::PopStyleColor(5);
			ImGui::PopStyleVar(4);
			ImGui::PopID();
			return ch;
		};

		int tagIdx = 0;
		for (int i = 0; i < 7; ++i) if (*tag == kTags[i]) tagIdx = i;
		if (combo("tag", nameX, half, kTags, 7, &tagIdx)) { *tag = kTags[tagIdx]; changed = true; }

		const float layerLabelX = nameX + half + 12.0f;
		dl->AddText(ImVec2(layerLabelX, TextY(row2, kRowHeight, ImGui::GetFontSize())), kText, "Layer");
		int layerIdx = std::clamp(*layer, 0, 4);
		if (combo("layer", layerLabelX + 34.0f, p.x + w - 12.0f - (layerLabelX + 34.0f), kLayers, layerCount, &layerIdx)) { *layer = layerIdx; changed = true; }

		ImGui::PopStyleVar();
		ImGui::PopID();

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, 58.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 58.0f));
		return changed;
	}
}

namespace UnityGUI
{
	// ---------- Volume ----------
	bool LeadingCheckbox(const char* id, bool* value, int indent)
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		bool changed = false;
		CheckBox(id, value, ImVec2(p.x + kBaseIndent + indent * kNestIndent - 20.0f, p.y + 2.0f), &changed);
		ImGui::SetCursorScreenPos(p);
		return changed;
	}

	int ObjectFieldButtons(const char* label, const char* text, const char* iconName, const char* const* buttons, int buttonCount, ImVec2* fieldMin, ImVec2* fieldMax, int indent)
	{
		Row r = BeginRow(label, indent);
		ImGui::PushID(label);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		int result = -2;

		// 오른쪽 버튼들의 폭을 먼저 잰다
		float buttonsW = 0.0f;
		std::vector<float> widths;
		for (int i = 0; i < buttonCount; ++i)
		{
			widths.push_back(ImGui::CalcTextSize(buttons[i]).x + 16.0f);
			buttonsW += widths.back() + 4.0f;
		}
		const ImVec2 p0(r.fieldX, r.p.y);
		const ImVec2 p1((std::max)(r.fieldX + 60.0f, r.fieldX + r.fieldW - buttonsW), r.p.y + kRowHeight);
		dl->AddRectFilled(p0, p1, kFieldBg, 3.0f);
		dl->AddRect(p0, p1, kFieldBorder, 3.0f);
		float tx = p0.x + 6.0f;
		if (iconName)
		{
			DrawIcon(dl, iconName, ImVec2(p0.x + 4.0f, p0.y + 1.0f), 16.0f);
			tx += 20.0f;
		}
		const bool hasValue = text && strncmp(text, "None", 4) != 0;
		ImVec4 clip(p0.x, p0.y, p1.x - 22.0f, p1.y);
		dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, TextY(p0.y, kRowHeight, ImGui::GetFontSize())), hasValue ? kTextBright : kTextDim, text, nullptr, 0.0f, &clip);
		ImGui::SetCursorScreenPos(ImVec2(p1.x - 20.0f, p0.y));
		if (ImGui::InvisibleButton("##pick", ImVec2(20.0f, kRowHeight)))
			result = -1;
		DrawIcon(dl, "target", ImVec2(p1.x - 18.0f, p0.y + 1.0f), 16.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(196, 196, 196, 255));

		float bx = p1.x + 4.0f;
		for (int i = 0; i < buttonCount; ++i)
		{
			ImGui::PushID(i);
			ImGui::SetCursorScreenPos(ImVec2(bx, p0.y));
			if (ImGui::InvisibleButton("##btn", ImVec2(widths[i], kRowHeight)))
				result = i;
			const bool hovered = ImGui::IsItemHovered();
			dl->AddRectFilled(ImVec2(bx, p0.y), ImVec2(bx + widths[i], p1.y), hovered ? kDropBgHover : kDropBg, 3.0f);
			dl->AddRect(ImVec2(bx, p0.y), ImVec2(bx + widths[i], p1.y), kDropBorder, 3.0f);
			dl->AddText(ImVec2(floorf(bx + 8.0f), TextY(p0.y, kRowHeight, ImGui::GetFontSize())), kTextBright, buttons[i]);
			bx += widths[i] + 4.0f;
			ImGui::PopID();
		}
		if (fieldMin) *fieldMin = p0;
		if (fieldMax) *fieldMax = p1;
		ImGui::PopID();
		EndRow(r);
		return result;
	}

	HeaderAction VolumeEffectHeader(const char* id, const char* title, bool* open, bool* active, bool* all, bool* none)
	{
		HeaderAction action = HeaderAction::None;
		if (all) *all = false;
		if (none) *none = false;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 22.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGui::PushID(id);

		ImGui::SetNextItemAllowOverlap();
		if (ImGui::InvisibleButton("##band", ImVec2(w, h)))
			*open = !*open;
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHeaderBg);
		dl->AddLine(p, ImVec2(p.x + w, p.y), kSeparator);
		dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + w, p.y + h - 1), kSeparator);
		DrawIcon(dl, *open ? "arrow_down" : "arrow_right", ImVec2(p.x + 8.0f, p.y + 6.0f), 10.0f);
		bool ch = false;
		ImGui::SetNextItemAllowOverlap();
		CheckBox("##active", active, ImVec2(p.x + 24.0f, p.y + 4.0f), &ch);
		ImFont* bold = BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(p.x + 44.0f, TextY(p.y, h, bold->FontSize)), *active ? kTextBright : kTextDim, title);

		// 오른쪽: ALL  NONE  ⋮  (Unity 의 Volume 효과 헤더)
		auto textButton = [&](const char* text, float right, const char* bid) {
			const ImVec2 ts = ImGui::CalcTextSize(text);
			const ImVec2 bp(p.x + w - right - ts.x, p.y + 3.0f);
			ImGui::SetCursorScreenPos(bp);
			ImGui::SetNextItemAllowOverlap();
			const bool clicked = ImGui::InvisibleButton(bid, ImVec2(ts.x, 16.0f));
			dl->AddText(ImVec2(bp.x, TextY(p.y, h, ImGui::GetFontSize())), ImGui::IsItemHovered() ? kTextBright : kTextDim, text);
			return clicked;
		};
		if (none && all)   // 기본 프로파일은 Override 가 없으므로 ALL/NONE 을 숨긴다 (nullptr)
		{
			*none = textButton("NONE", 30.0f, "##none");
			*all = textButton("ALL", 30.0f + ImGui::CalcTextSize("NONE").x + 10.0f, "##all");
		}
		{
			const ImVec2 ip(p.x + w - 22.0f, p.y + 3.0f);
			ImGui::SetCursorScreenPos(ip);
			ImGui::SetNextItemAllowOverlap();
			if (ImGui::InvisibleButton("##kebab", ImVec2(16, 16)))
				ImGui::OpenPopup("##fxmenu");
			DrawIcon(dl, "kebab", ip, 16.0f, ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(196, 196, 196, 255));
		}
		ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(48, 48, 48, 255)));
		if (ImGui::BeginPopup("##fxmenu"))
		{
			if (ImGui::MenuItem("Reset"))
				action = HeaderAction::Reset;
			ImGui::Separator();
			if (ImGui::MenuItem("Remove"))
				action = HeaderAction::Remove;
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();

		ImGui::PopID();
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
		return action;
	}

	bool CenterButton(const char* label, float width)
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 22.0f;
		const float x = p.x + floorf((w - width) * 0.5f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGui::SetCursorScreenPos(ImVec2(x, p.y));
		const bool clicked = ImGui::InvisibleButton(label, ImVec2(width, h));
		const bool hovered = ImGui::IsItemHovered();
		dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + width, p.y + h), hovered ? kDropBgHover : kDropBg, 3.0f);
		dl->AddRect(ImVec2(x, p.y), ImVec2(x + width, p.y + h), kDropBorder, 3.0f);
		const char* end = strstr(label, "##");   // ## 뒤는 ID 용
		const ImVec2 ts = ImGui::CalcTextSize(label, end);
		dl->AddText(ImVec2(floorf(x + (width - ts.x) * 0.5f), TextY(p.y, h, ImGui::GetFontSize())), kTextBright, label, end);
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h + 4.0f));
		return clicked;
	}

	void PrefabInstanceRow(GameObject* root)
	{
		if (root == nullptr)
			return;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 22.0f;
		dl->AddText(ImVec2(p.x + 44.0f, TextY(p.y, h, ImGui::GetFontSize())), kText, "Prefab");

		auto button = [&](const char* id, const char* text, float x, float bw, bool enabled) {
			ImGui::SetCursorScreenPos(ImVec2(x, p.y + 1.0f));
			bool pressed = ImGui::InvisibleButton(id, ImVec2(bw, h - 2.0f)) && enabled;
			const bool hovered = enabled && ImGui::IsItemHovered();
			dl->AddRectFilled(ImVec2(x, p.y + 1.0f), ImVec2(x + bw, p.y + h - 1.0f), hovered ? kDropBgHover : kDropBg, 3.0f);
			dl->AddRect(ImVec2(x, p.y + 1.0f), ImVec2(x + bw, p.y + h - 1.0f), kDropBorder, 3.0f);
			const ImVec2 ts = ImGui::CalcTextSize(text);
			dl->AddText(ImVec2(x + (bw - ts.x) * 0.5f, TextY(p.y, h, ImGui::GetFontSize())), enabled ? kTextBright : kTextDim, text);
			return pressed;
		};
		const float x0 = p.x + 110.0f;
		const float bw = (std::max)(50.0f, (w - 120.0f) / 3.0f - 4.0f);
		button("##prefabOpen", "Open", x0, bw, false);   // 프리팹 모드(에셋 단독 편집)는 아직 없음
		if (button("##prefabSelect", "Select", x0 + bw + 4.0f, bw, true))
			SelectionManager::SetSelectedFile(PathManager::GetI()->GetMovePathW(string_to_wstring(root->GetPrefabLink().Asset)));
		if (button("##prefabOverrides", "Overrides", x0 + (bw + 4.0f) * 2.0f, bw, true))
			ImGui::OpenPopup("##prefabOverridesPopup");
		DrawIcon(dl, "dropdown", ImVec2(x0 + (bw + 4.0f) * 2.0f + bw - 16.0f, p.y + 6.0f), 10.0f);

		ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(520, 420));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		if (ImGui::BeginPopup("##prefabOverridesPopup"))
		{
			const std::vector<std::string> overrides = PrefabUtility::GetOverrideDescriptions(root);
			ImGui::TextUnformatted(("Review, Revert or Apply Overrides on '" + root->GetName() + "'").c_str());
			ImGui::Separator();
			if (overrides.empty())
				ImGui::TextDisabled("No Overrides");
			for (size_t i = 0; i < overrides.size() && i < 40; ++i)
				ImGui::BulletText("%s", overrides[i].c_str());
			if (overrides.size() > 40)
				ImGui::TextDisabled("... and %zu more", overrides.size() - 40);
			ImGui::Separator();
			// 씬을 다시 만드는 작업이라 이번 프레임의 Inspector 가 끝난 뒤 실행
			const uint64 id = root->GetFileID();
			if (ImGui::Button("Revert All", ImVec2(120, 0)))
			{
				SceneManager::GetI()->AddLastUpdate([id]() {
					if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
						PrefabUtility::RevertAll(scene->FindByFileID(id));
				});
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Apply All", ImVec2(120, 0)))
			{
				SceneManager::GetI()->AddLastUpdate([id]() {
					if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
						PrefabUtility::ApplyAll(scene->FindByFileID(id));
				});
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, h + 4.0f));
	}
}
