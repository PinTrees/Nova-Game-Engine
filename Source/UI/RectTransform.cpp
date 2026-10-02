#include "pch.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

namespace
{
	const ImU32 kText = IM_COL32(196, 196, 196, 255);
	const ImU32 kDim = IM_COL32(150, 150, 150, 255);
	const ImU32 kAnchorRed = IM_COL32(236, 84, 84, 255);

	float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

	// 프리셋 (Unity Anchor Presets): 행 = top, middle, bottom, stretch / 열 = left, center, right, stretch
	void PresetAnchors(int row, int col, Vec2& aMin, Vec2& aMax)
	{
		static const float colMin[4] = { 0.0f, 0.5f, 1.0f, 0.0f }, colMax[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
		static const float rowMin[4] = { 1.0f, 0.5f, 0.0f, 0.0f }, rowMax[4] = { 1.0f, 0.5f, 0.0f, 1.0f };
		aMin = Vec2(colMin[col], rowMin[row]);
		aMax = Vec2(colMax[col], rowMax[row]);
	}

	// 프리셋 칸 그림: 바깥 상자(부모) + 안쪽 사각형 + 빨간 기준점 표시
	void DrawPresetIcon(ImDrawList* dl, ImVec2 p, float s, int row, int col, bool hovered, bool selected)
	{
		const ImU32 box = selected ? IM_COL32(58, 114, 176, 255) : (hovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(58, 58, 58, 255));
		dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), box, 2.0f);
		const float m = s * 0.22f;
		const ImVec2 a(p.x + m, p.y + m), b(p.x + s - m, p.y + s - m);
		dl->AddRect(a, b, IM_COL32(170, 170, 170, 255));
		// 안쪽 작은 사각형 (요소)
		const float cx[4] = { a.x + (b.x - a.x) * 0.25f, (a.x + b.x) * 0.5f, a.x + (b.x - a.x) * 0.75f, (a.x + b.x) * 0.5f };
		const float cy[4] = { a.y + (b.y - a.y) * 0.25f, (a.y + b.y) * 0.5f, a.y + (b.y - a.y) * 0.75f, (a.y + b.y) * 0.5f };
		const float hw = col == 3 ? (b.x - a.x) * 0.5f - 2.0f : 3.5f;
		const float hh = row == 3 ? (b.y - a.y) * 0.5f - 2.0f : 3.5f;
		dl->AddRectFilled(ImVec2(cx[col] - hw, cy[row] - hh), ImVec2(cx[col] + hw, cy[row] + hh), IM_COL32(200, 200, 200, 255));
		// 기준점: 늘어나는 축은 양쪽 선, 아니면 한 줄
		auto vline = [&](float x) { dl->AddLine(ImVec2(x, p.y + 2), ImVec2(x, p.y + s - 2), kAnchorRed, 1.5f); };
		auto hline = [&](float y) { dl->AddLine(ImVec2(p.x + 2, y), ImVec2(p.x + s - 2, y), kAnchorRed, 1.5f); };
		if (col == 3) { vline(a.x); vline(b.x); }
		else vline(col == 0 ? a.x : (col == 1 ? (a.x + b.x) * 0.5f : b.x));
		if (row == 3) { hline(a.y); hline(b.y); }
		else hline(row == 0 ? a.y : (row == 1 ? (a.y + b.y) * 0.5f : b.y));
	}

	// 현재 기준점이 어느 프리셋인지 (없으면 -1)
	bool FindPreset(const Vec2& aMin, const Vec2& aMax, int& row, int& col)
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
			{
				Vec2 mn, mx;
				PresetAnchors(r, c, mn, mx);
				if ((mn - aMin).LengthSquared() < 1e-6f && (mx - aMax).LengthSquared() < 1e-6f)
				{
					row = r;
					col = c;
					return true;
				}
			}
		return false;
	}

	const char* kRowNames[4] = { "top", "middle", "bottom", "stretch" };
	const char* kColNames[4] = { "left", "center", "right", "stretch" };
}

RectTransform::RectTransform()
{
	m_InspectorTitleName = "Rect Transform";
}

RectTransform::~RectTransform() {}

RectTransform* RectTransform::Of(GameObject* go)
{
	return go ? go->GetComponent<RectTransform>() : nullptr;
}

void RectTransform::SetOffsets(const Vec2& offsetMin, const Vec2& offsetMax)
{
	m_SizeDelta = offsetMax - offsetMin;
	m_AnchoredPosition = offsetMin + m_SizeDelta * m_Pivot;
}

void RectTransform::SetAnchorsKeepRect(const Vec2& anchorMin, const Vec2& anchorMax)
{
	// 부모 공간에서 지금 사각형의 모서리를 구해 두고, 새 기준점에 대한 거리로 다시 쓴다
	const Vec2 oldMin = m_ParentMin + m_ParentSize * m_AnchorMin + GetOffsetMin();
	const Vec2 oldMax = m_ParentMin + m_ParentSize * m_AnchorMax + GetOffsetMax();
	m_AnchorMin = anchorMin;
	m_AnchorMax = anchorMax;
	SetOffsets(oldMin - (m_ParentMin + m_ParentSize * m_AnchorMin), oldMax - (m_ParentMin + m_ParentSize * m_AnchorMax));
}

void RectTransform::SetPivotKeepRect(const Vec2& pivot)
{
	const Vec2 offMin = GetOffsetMin(), offMax = GetOffsetMax();
	m_Pivot = pivot;
	SetOffsets(offMin, offMax);
}

void RectTransform::SetDrivenRect(const Vec2& size)
{
	m_Driven = true;
	m_AnchorMin = m_AnchorMax = Vec2(0, 0);
	m_Pivot = Vec2(0.5f, 0.5f);
	m_SizeDelta = size;
	m_AnchoredPosition = size * 0.5f;
	m_RectSize = size;
	m_RectMin = -size * 0.5f;
	m_ParentMin = Vec2(0, 0);
	m_ParentSize = size;
}

void RectTransform::Layout(const Vec2& parentMin, const Vec2& parentSize)
{
	m_Driven = false;
	m_ParentMin = parentMin;
	m_ParentSize = parentSize;
	Transform* tr = m_pGameObject ? m_pGameObject->GetTransform() : nullptr;
	if (tr == nullptr)
		return;

	// 이동 도구/스크립트가 Transform 위치를 바꿨으면 그만큼 anchoredPosition 에 반영
	const Vec3 lp = tr->GetLocalPosition();
	if (m_HasWritten && (fabsf(lp.x - m_Written.x) > 1e-3f || fabsf(lp.y - m_Written.y) > 1e-3f))
		m_AnchoredPosition += Vec2(lp.x - m_Written.x, lp.y - m_Written.y);

	const Vec2 aMin = parentMin + parentSize * m_AnchorMin;
	const Vec2 aMax = parentMin + parentSize * m_AnchorMax;
	m_RectSize = (aMax - aMin) + m_SizeDelta;
	m_RectMin = -m_RectSize * m_Pivot;
	const Vec2 pos = aMin + (aMax - aMin) * m_Pivot + m_AnchoredPosition;
	if (fabsf(pos.x - lp.x) > 1e-4f || fabsf(pos.y - lp.y) > 1e-4f)
		tr->SetLocalPosition(Vec3(pos.x, pos.y, lp.z));
	m_Written = pos;
	m_HasWritten = true;
}

void RectTransform::GetWorldCorners(Vec3 out[4])
{
	const Vec2 mn = m_RectMin, mx = m_RectMin + m_RectSize;
	const Vec3 local[4] = { Vec3(mn.x, mn.y, 0), Vec3(mn.x, mx.y, 0), Vec3(mx.x, mx.y, 0), Vec3(mx.x, mn.y, 0) };
	const Matrix world = m_pGameObject ? m_pGameObject->GetTransform()->GetWorldMatrix() : Matrix::Identity;
	for (int i = 0; i < 4; ++i)
		out[i] = Vec3::Transform(local[i], world);
}

bool RectTransform::ContainsWorldPoint(const Vec2& p)
{
	// 회전/크기가 있어도 되도록 사각형 로컬 공간으로 되돌려 비교
	if (m_pGameObject == nullptr)
		return false;
	const Matrix inv = m_pGameObject->GetTransform()->GetWorldMatrix().Invert();
	const Vec3 l = Vec3::Transform(Vec3(p.x, p.y, 0.0f), inv);
	return l.x >= m_RectMin.x && l.x <= m_RectMin.x + m_RectSize.x && l.y >= m_RectMin.y && l.y <= m_RectMin.y + m_RectSize.y;
}

void RectTransform::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || m_Driven || m_pGameObject == nullptr || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	Vec3 k[4];
	GetWorldCorners(k);
	for (int i = 0; i < 4; ++i)
		SceneViewOverlay::DrawLine(k[i], k[(i + 1) % 4], IM_COL32(64, 153, 255, 255), 1.5f);
}

// ------------------------------------------------------------------ Inspector
void RectTransform::DrawAnchorPresetButton(ImVec2 pos, float size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImGui::SetCursorScreenPos(pos);
	const bool clicked = ImGui::InvisibleButton("##anchorPreset", ImVec2(size, size));
	int row = 1, col = 1;
	const bool known = FindPreset(m_AnchorMin, m_AnchorMax, row, col);
	DrawPresetIcon(dl, pos, size, row, col, ImGui::IsItemHovered(), false);
	if (!known)
		dl->AddText(ImVec2(pos.x + 4, pos.y + size - 16), kText, "custom");
	else
	{
		// 늘어나지 않는 축의 이름을 상자 위/왼쪽에 (Unity 처럼)
		const char* top = kColNames[col];
		dl->AddText(ImGui::GetFont(), 10.0f, ImVec2(pos.x + (size - ImGui::GetFont()->CalcTextSizeA(10.0f, FLT_MAX, 0, top).x) * 0.5f, pos.y - 12.0f), kDim, top);
	}
	if (clicked)
		ImGui::OpenPopup("##anchorPresets");
	DrawAnchorPresetPopup();
}

void RectTransform::DrawAnchorPresetPopup()
{
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
	if (ImGui::BeginPopup("##anchorPresets"))
	{
		ImGuiIO& io = ImGui::GetIO();
		ImGui::TextUnformatted("Anchor Presets");
		ImGui::TextDisabled("Shift: Also set pivot     Alt: Also set position");
		ImGui::Dummy(ImVec2(0, 4));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float cell = 44.0f, gap = 6.0f, labelW = 56.0f, labelH = 16.0f;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		int curRow = -1, curCol = -1;
		FindPreset(m_AnchorMin, m_AnchorMax, curRow, curCol);
		for (int c = 0; c < 4; ++c)
			dl->AddText(ImVec2(origin.x + labelW + c * (cell + gap) + 4, origin.y), kDim, kColNames[c]);
		for (int r = 0; r < 4; ++r)
		{
			const float y = origin.y + labelH + r * (cell + gap);
			dl->AddText(ImVec2(origin.x, y + cell * 0.5f - 7), kDim, kRowNames[r]);
			for (int c = 0; c < 4; ++c)
			{
				const ImVec2 p(origin.x + labelW + c * (cell + gap), y);
				ImGui::SetCursorScreenPos(p);
				ImGui::PushID(r * 4 + c);
				const bool clicked = ImGui::InvisibleButton("##preset", ImVec2(cell, cell));
				DrawPresetIcon(dl, p, cell, r, c, ImGui::IsItemHovered(), r == curRow && c == curCol);
				if (clicked)
				{
					Vec2 mn, mx;
					PresetAnchors(r, c, mn, mx);
					SetAnchorsKeepRect(mn, mx);
					if (io.KeyShift)
					{
						// 피벗: 늘어나는 축은 가운데
						const Vec2 pivot(c == 3 ? 0.5f : mn.x, r == 3 ? 0.5f : mn.y);
						SetPivotKeepRect(pivot);
					}
					if (io.KeyAlt)
					{
						// 위치도 기준점으로: 늘어나는 축은 여백 0, 아니면 위치 0
						Vec2 offMin = GetOffsetMin(), offMax = GetOffsetMax();
						const Vec2 size = m_SizeDelta;
						for (int axis = 0; axis < 2; ++axis)
						{
							const bool stretch = axis == 0 ? c == 3 : r == 3;
							float& lo = axis == 0 ? offMin.x : offMin.y;
							float& hi = axis == 0 ? offMax.x : offMax.y;
							const float s = axis == 0 ? size.x : size.y;
							const float pv = axis == 0 ? m_Pivot.x : m_Pivot.y;
							if (stretch) { lo = 0.0f; hi = 0.0f; }
							else { lo = -s * pv; hi = s * (1.0f - pv); }
						}
						SetOffsets(offMin, offMax);
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + labelH + 4 * (cell + gap)));
		ImGui::Dummy(ImVec2(labelW + 4 * (cell + gap), 0));
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void RectTransform::OnInspectorGUI()
{
	// Layout Group · Fitter 가 값을 정하고 있으면 (Unity: "Some values driven by ...")
	if (!m_DrivenBy.empty() && ImGui::GetFrameCount() - m_DrivenFrame <= 2)
		UnityGUI::HelpBox(("Some values driven by " + m_DrivenBy + ".").c_str(), false);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;
	const float fieldX = p.x + std::clamp(w * 0.408f, 90.0f, (std::max)(90.0f, w - 90.0f));
	const float fieldW = (std::max)(60.0f, p.x + w - 12.0f - fieldX);
	const float cellW = (fieldW - 8.0f) / 3.0f;
	const float fs = ImGui::GetFontSize();

	if (m_Driven)
	{
		UnityGUI::HelpBox("Some values driven by Canvas.", false);
		ImGui::BeginDisabled();
	}

	// 왼쪽: 기준점 프리셋 상자
	const float boxSize = 52.0f;
	const float top = ImGui::GetCursorScreenPos().y;   // (Canvas 가 정하는 값이면 안내 상자 아래)
	DrawAnchorPresetButton(ImVec2(p.x + UnityGUI::kBaseIndent, top + 14.0f), boxSize);

	const bool stretchX = fabsf(m_AnchorMin.x - m_AnchorMax.x) > 1e-6f;
	const bool stretchY = fabsf(m_AnchorMin.y - m_AnchorMax.y) > 1e-6f;
	Transform* tr = m_pGameObject ? m_pGameObject->GetTransform() : nullptr;

	auto caption = [&](float x, float y, const char* t) { dl->AddText(ImVec2(floorf(x + 2.0f), floorf(y)), kDim, t); };
	bool changed = false;
	Vec2 offMin = GetOffsetMin(), offMax = GetOffsetMax();

	// 1 행: Pos X(또는 Left) | Pos Y(또는 Top) | Pos Z
	float y = top;
	caption(fieldX, y, stretchX ? "Left" : "Pos X");
	caption(fieldX + cellW + 4.0f, y, stretchY ? "Top" : "Pos Y");
	caption(fieldX + (cellW + 4.0f) * 2.0f, y, "Pos Z");
	y += fs + 2.0f;
	{
		float v0 = stretchX ? offMin.x : m_AnchoredPosition.x;
		float v1 = stretchY ? -offMax.y : m_AnchoredPosition.y;
		float z = tr ? tr->GetLocalPosition().z : 0.0f;
		if (UnityGUI::FloatCell("px", &v0, ImVec2(fieldX, y), cellW))
		{
			if (stretchX) { offMin.x = v0; SetOffsets(offMin, offMax); }
			else m_AnchoredPosition.x = v0;
			changed = true;
		}
		if (UnityGUI::FloatCell("py", &v1, ImVec2(fieldX + cellW + 4.0f, y), cellW))
		{
			if (stretchY) { offMax.y = -v1; SetOffsets(offMin, offMax); }
			else m_AnchoredPosition.y = v1;
			changed = true;
		}
		if (UnityGUI::FloatCell("pz", &z, ImVec2(fieldX + (cellW + 4.0f) * 2.0f, y), cellW) && tr)
		{
			Vec3 lp = tr->GetLocalPosition();
			lp.z = z;
			tr->SetLocalPosition(lp);
		}
	}
	// 2 행: Width(또는 Right) | Height(또는 Bottom)
	y += UnityGUI::kRowStep + 2.0f;
	offMin = GetOffsetMin();
	offMax = GetOffsetMax();
	caption(fieldX, y, stretchX ? "Right" : "Width");
	caption(fieldX + cellW + 4.0f, y, stretchY ? "Bottom" : "Height");
	y += fs + 2.0f;
	{
		float v0 = stretchX ? -offMax.x : m_SizeDelta.x;
		float v1 = stretchY ? offMin.y : m_SizeDelta.y;
		if (UnityGUI::FloatCell("sw", &v0, ImVec2(fieldX, y), cellW))
		{
			if (stretchX) { offMax.x = -v0; SetOffsets(offMin, offMax); }
			else SetOffsets(Vec2(m_AnchoredPosition.x - v0 * m_Pivot.x, offMin.y), Vec2(m_AnchoredPosition.x + v0 * (1.0f - m_Pivot.x), offMax.y));
			changed = true;
		}
		if (UnityGUI::FloatCell("sh", &v1, ImVec2(fieldX + cellW + 4.0f, y), cellW))
		{
			offMin = GetOffsetMin();
			offMax = GetOffsetMax();
			if (stretchY) { offMin.y = v1; SetOffsets(offMin, offMax); }
			else SetOffsets(Vec2(offMin.x, m_AnchoredPosition.y - v1 * m_Pivot.y), Vec2(offMax.x, m_AnchoredPosition.y + v1 * (1.0f - m_Pivot.y)));
			changed = true;
		}
	}
	y += UnityGUI::kRowStep + 6.0f;
	ImGui::SetCursorScreenPos(ImVec2(p.x, (std::max)(y, top + boxSize + 22.0f)));
	ImGui::Dummy(ImVec2(w, 0));

	// Anchors (Min / Max), Pivot, Rotation, Scale
	if (UnityGUI::FoldoutPlain("Anchors", 0, true))
	{
		Vec2 mn = m_AnchorMin, mx = m_AnchorMax;
		if (UnityGUI::Vector2Pair("Min", "X", &mn.x, "Y", &mn.y, 1) | UnityGUI::Vector2Pair("Max", "X", &mx.x, "Y", &mx.y, 1))
		{
			SetAnchorsKeepRect(Vec2(Clamp01(mn.x), Clamp01(mn.y)), Vec2(Clamp01(mx.x), Clamp01(mx.y)));
			changed = true;
		}
	}
	Vec2 pivot = m_Pivot;
	if (UnityGUI::Vector2Pair("Pivot", "X", &pivot.x, "Y", &pivot.y))
	{
		SetPivotKeepRect(pivot);
		changed = true;
	}
	if (m_Driven)
		ImGui::EndDisabled();
	UnityGUI::Spacing(4.0f);
	if (tr)
	{
		Vec3 euler = tr->GetLocalEulerAngles();
		if (UnityGUI::Vector3("Rotation", &euler.x))
			tr->SetLocalEulerAngles(euler);
		Vec3 scale = tr->GetLocalScale();
		if (UnityGUI::Vector3("Scale", &scale.x, true))
			tr->SetLocalScale(scale);
	}
	(void)changed;
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(RectTransform)
{
	json j;
	SERIALIZE_TYPE(j, RectTransform);
	j["anchorMin"] = { m_AnchorMin.x, m_AnchorMin.y };
	j["anchorMax"] = { m_AnchorMax.x, m_AnchorMax.y };
	// 루트 캔버스는 화면 크기로 정해지는 값이라 저장하지 않는다 (창 크기만 바뀌어도 씬이 바뀐 것으로 보이지 않게)
	j["anchoredPosition"] = m_Driven ? json{ 0.0f, 0.0f } : json{ m_AnchoredPosition.x, m_AnchoredPosition.y };
	j["sizeDelta"] = m_Driven ? json{ 0.0f, 0.0f } : json{ m_SizeDelta.x, m_SizeDelta.y };
	j["pivot"] = { m_Pivot.x, m_Pivot.y };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(RectTransform)
{
	auto v2 = [&](const char* key, Vec2 def) {
		if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
			return Vec2(j[key][0].get<float>(), j[key][1].get<float>());
		return def;
	};
	m_AnchorMin = v2("anchorMin", Vec2(0.5f, 0.5f));
	m_AnchorMax = v2("anchorMax", Vec2(0.5f, 0.5f));
	m_AnchoredPosition = v2("anchoredPosition", Vec2(0, 0));
	m_SizeDelta = v2("sizeDelta", Vec2(100, 100));
	m_Pivot = v2("pivot", Vec2(0.5f, 0.5f));
	m_HasWritten = false;   // Transform 도 함께 복원되므로 비교 기준을 새로
}
