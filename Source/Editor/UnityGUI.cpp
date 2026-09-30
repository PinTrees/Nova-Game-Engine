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
			dl->AddText(font, fs, ImVec2(r.p.x + kBaseIndent + indent * kNestIndent, r.p.y + (kRowHeight - fs) * 0.5f), kText, label);
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
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, (kRowHeight - ImGui::GetFontSize()) * 0.5f));
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
		static std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> cache;
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

	void Spacing(float height)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(1, height));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
	}

	// ---------- 행 위젯 ----------
	bool Dropdown(const char* label, int* index, const char* const* items, int count, int indent)
	{
		Row r = BeginRow(label, indent);
		bool changed = false;
		ImGui::PushID(label);

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, (kRowHeight - ImGui::GetFontSize()) * 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kDropBg));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4(kDropBgHover));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, V4(kDropBg));
		ImGui::PushStyleColor(ImGuiCol_Border, V4(kDropBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(kTextBright));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(48, 48, 48, 255)));

		int cur = std::clamp(*index, 0, (std::max)(0, count - 1));
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
			ImGui::GetWindowDrawList()->AddText(ImVec2(rmin.x + 6.0f, rmin.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f), kTextDim, innerLabel);
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
		ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 1.0f, p.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f), kText, axis);

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

	void ObjectField(const char* label, const char* text, int indent)
	{
		Row r = BeginRow(label, indent);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p0(r.fieldX, r.p.y);
		ImVec2 p1(r.fieldX + r.fieldW, r.p.y + kRowHeight);
		dl->AddRectFilled(p0, p1, kFieldBg, 3.0f);
		dl->AddRect(p0, p1, kFieldBorder, 3.0f);
		dl->AddText(ImVec2(p0.x + 6.0f, p0.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f), kTextDim, text);
		DrawIcon(dl, "target", ImVec2(p1.x - 18.0f, p0.y + 1.0f), 16.0f);
		EndRow(r);
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
		dl->AddText(bold, bold->FontSize, ImVec2(x + 16.0f, p.y + (h - bold->FontSize) * 0.5f), kTextBright, label);
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
		dl->AddText(bold, bold->FontSize, ImVec2(x, p.y + (h - bold->FontSize) * 0.5f), kTextBright, title);

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
		static const char* kLayers[] = { "Default", "TransparentFX", "Ignore Raycast", "Water", "UI" };

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
		dl->AddText(ImVec2(nameX + nameW + 28.0f, row1 + (kRowHeight - ImGui::GetFontSize()) * 0.5f), kText, "Static");
		DrawIcon(dl, "dropdown", ImVec2(p.x + w - 20.0f, row1 + 3.0f), 12.0f);

		// 2행: Tag [ ▾ ]  Layer [ ▾ ]
		const float row2 = p.y + 32.0f;
		const float half = (w - 68.0f - 12.0f) * 0.5f;
		dl->AddText(ImVec2(p.x + 44.0f, row2 + (kRowHeight - ImGui::GetFontSize()) * 0.5f), kText, "Tag");

		auto combo = [&](const char* id, float x, float width, const char* const* items, int count, int* cur) -> bool
		{
			bool ch = false;
			ImGui::SetCursorScreenPos(ImVec2(x, row2));
			ImGui::PushID(id);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, (kRowHeight - ImGui::GetFontSize()) * 0.5f));
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
		dl->AddText(ImVec2(layerLabelX, row2 + (kRowHeight - ImGui::GetFontSize()) * 0.5f), kText, "Layer");
		int layerIdx = std::clamp(*layer, 0, 4);
		if (combo("layer", layerLabelX + 34.0f, p.x + w - 12.0f - (layerLabelX + 34.0f), kLayers, 5, &layerIdx)) { *layer = layerIdx; changed = true; }

		ImGui::PopStyleVar();
		ImGui::PopID();

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, 58.0f));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 58.0f));
		return changed;
	}
}
