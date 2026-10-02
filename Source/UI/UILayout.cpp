#include "pch.h"
#include "UILayout.h"
#include "RectTransform.h"
#include "UIText.h"
#include "UIImage.h"
#include "UISprites.h"
#include "UICanvas.h"
#include "UnityGUI.h"

namespace
{
	template <typename T>
	T* Enabled(GameObject* go)
	{
		for (auto& c : go->GetComponents())
			if (T* t = dynamic_cast<T*>(c.get()); t && c->IsEnabled())
				return t;
		return nullptr;
	}

	// 자식 RectTransform 을 부모 사각형으로 다시 (그룹 · 맞춤이 값을 바꾼 뒤)
	void RelayoutSubtree(GameObject* go)
	{
		RectTransform* rt = RectTransform::Of(go);
		if (rt == nullptr)
			return;
		for (GameObject* child : go->GetChildren())
			if (RectTransform* crt = RectTransform::Of(child))
			{
				crt->Layout(rt->GetRectMin(), rt->GetRectSize());
				RelayoutSubtree(child);
			}
	}

	float Axis(const Vec2& v, int axis) { return axis == 0 ? v.x : v.y; }
	void SetAxis(Vec2& v, int axis, float value) { (axis == 0 ? v.x : v.y) = value; }

	// Unity LayoutUtility.GetLayoutProperty: 우선순위가 가장 높은 값 (같으면 큰 값), 정하지 않은(음수) 값은 건너뛴다
	// which: 0 Min, 1 Preferred, 2 Flexible
	float LayoutProperty(RectTransform* rt, int which, int axis, float fallback)
	{
		if (rt == nullptr || rt->GetGameObject() == nullptr)
			return fallback;
		float best = fallback;
		int bestPriority = INT_MIN;
		auto consider = [&](float v, int priority) {
			if (v < 0.0f)
				return;
			if (priority > bestPriority) { bestPriority = priority; best = v; }
			else if (priority == bestPriority && v > best) best = v;
		};
		for (auto& c : rt->GetGameObject()->GetComponents())
		{
			if (!c->IsEnabled())
				continue;
			if (auto* le = dynamic_cast<LayoutElement*>(c.get()))
			{
				const float v = which == 0 ? (axis == 0 ? le->MinWidth : le->MinHeight)
					: (which == 1 ? (axis == 0 ? le->PreferredWidth : le->PreferredHeight) : (axis == 0 ? le->FlexibleWidth : le->FlexibleHeight));
				consider(v, le->LayoutPriority);
			}
			else if (auto* g = dynamic_cast<LayoutGroup*>(c.get()))
			{
				g->CalcAlongAxis(axis);
				consider(which == 0 ? g->TotalMin(axis) : (which == 1 ? g->TotalPreferred(axis) : g->TotalFlexible(axis)), 0);
			}
			else if (auto* t = dynamic_cast<Text*>(c.get()))
			{
				// 글: 가로 = 줄바꿈 없는 폭, 세로 = 지금 폭에서 줄을 나눈 높이 (Unity Text 와 같음)
				if (which == 0) consider(0.0f, 0);
				else if (which == 1) consider(axis == 0 ? t->MeasurePreferred(-1.0f).x : t->MeasurePreferred(rt->GetRectSize().x).y, 0);
			}
			else if (auto* img = dynamic_cast<UIImage*>(c.get()))
			{
				if (which == 0) consider(0.0f, 0);
				else if (which == 1)
				{
					UISprites::Info info;
					consider(!img->GetSprite().empty() && UISprites::Get(img->GetSprite(), info) ? Axis(info.Size, axis) : 0.0f, 0);
				}
			}
		}
		return best;
	}

	void MarkDriven(RectTransform* rt, const char* by)
	{
		if (rt)
			rt->SetDrivenBy(by);
	}
}

namespace UILayout
{
	bool IgnoresLayout(GameObject* go)
	{
		LayoutElement* le = Enabled<LayoutElement>(go);
		return le && le->IgnoreLayout;
	}

	float GetMinSize(RectTransform* rt, int axis) { return LayoutProperty(rt, 0, axis, 0.0f); }
	float GetPreferredSize(RectTransform* rt, int axis) { return (std::max)(GetMinSize(rt, axis), LayoutProperty(rt, 1, axis, 0.0f)); }
	float GetFlexibleSize(RectTransform* rt, int axis) { return LayoutProperty(rt, 2, axis, 0.0f); }

	namespace
	{
		void ApplyNode(GameObject* go)
		{
			if (!go->IsActive())
				return;
			if (RectTransform* rt = RectTransform::Of(go))
			{
				ContentSizeFitter* fitter = Enabled<ContentSizeFitter>(go);
				LayoutGroup* group = Enabled<LayoutGroup>(go);
				AspectRatioFitter* aspect = Enabled<AspectRatioFitter>(go);
				// 가로 먼저, 그다음 세로 (글 높이는 폭에 따른다)
				for (int axis = 0; axis < 2; ++axis)
				{
					if (fitter)
					{
						fitter->Apply(axis);
						RelayoutSubtree(go);
					}
					if (group)
					{
						group->CalcAlongAxis(axis);
						group->SetLayoutAlongAxis(axis);
						RelayoutSubtree(go);
					}
				}
				if (aspect)
				{
					aspect->Apply();
					RelayoutSubtree(go);
				}
			}
			for (GameObject* child : go->GetChildren())
				ApplyNode(child);
		}
	}

	void Apply(GameObject* root)
	{
		if (root)
			ApplyNode(root);
	}

	void ForceRebuild(GameObject* go)
	{
		// 맨 위 캔버스 → 그 아래 전체 (RectTransform 은 지난 레이아웃의 부모 사각형으로 다시)
		GameObject* top = go;
		for (GameObject* g = go; g; g = g->GetParent())
			if (g->GetComponent<Canvas>())
				top = g;
		if (RectTransform* rt = RectTransform::Of(top))
		{
			RelayoutSubtree(top);
			Apply(top);
			(void)rt;
		}
	}
}

// ------------------------------------------------------------------ Layout Group 공통
std::vector<RectTransform*> LayoutGroup::Children() const
{
	std::vector<RectTransform*> out;
	if (m_pGameObject == nullptr)
		return out;
	for (GameObject* child : m_pGameObject->GetChildren())
		if (child->IsActive() && !UILayout::IgnoresLayout(child))
			if (RectTransform* rt = RectTransform::Of(child))
				out.push_back(rt);
	return out;
}

float LayoutGroup::StartOffset(int axis, float requiredWithoutPadding) const
{
	const RectTransform* rt = RectTransform::Of(m_pGameObject);
	const float padding = axis == 0 ? (float)(PaddingLeft + PaddingRight) : (float)(PaddingTop + PaddingBottom);
	const float available = rt ? Axis(rt->GetRectSize(), axis) : 0.0f;
	const float surplus = available - (requiredWithoutPadding + padding);
	return (axis == 0 ? (float)PaddingLeft : (float)PaddingTop) + surplus * AlignmentOnAxis(axis);
}

// Unity SetInsetAndSizeFromParentEdge (가로 = 왼쪽 모서리, 세로 = 위 모서리 기준)
void LayoutGroup::SetChildAlongAxis(RectTransform* child, int axis, float pos, float size, float scale) const
{
	Vec2 aMin = child->GetAnchorMin(), aMax = child->GetAnchorMax(), ap = child->GetAnchoredPosition(), sd = child->GetSizeDelta();
	const Vec2 pivot = child->GetPivot();
	if (axis == 0)
	{
		aMin.x = aMax.x = 0.0f;
		sd.x = size;
		ap.x = pos + size * pivot.x * scale;
	}
	else
	{
		aMin.y = aMax.y = 1.0f;
		sd.y = size;
		ap.y = -pos - size * (1.0f - pivot.y) * scale;
	}
	child->SetAnchorMin(aMin);
	child->SetAnchorMax(aMax);
	child->SetSizeDelta(sd);
	child->SetAnchoredPosition(ap);
	MarkDriven(child, m_InspectorTitleName.c_str());
}

void LayoutGroup::SetChildAlongAxisKeepSize(RectTransform* child, int axis, float pos, float scale) const
{
	SetChildAlongAxis(child, axis, pos, Axis(child->GetSizeDelta(), axis), scale);
}

void LayoutGroup::DrawPaddingAndAlignment()
{
	if (UnityGUI::FoldoutPlain("Padding", 0, true))
	{
		UnityGUI::Int("Left", &PaddingLeft, 1);
		UnityGUI::Int("Right", &PaddingRight, 1);
		UnityGUI::Int("Top", &PaddingTop, 1);
		UnityGUI::Int("Bottom", &PaddingBottom, 1);
	}
}

void LayoutGroup::PaddingToJson(json& j) const
{
	j["padding"] = { PaddingLeft, PaddingRight, PaddingTop, PaddingBottom };
	j["childAlignment"] = ChildAlignment;
}

void LayoutGroup::PaddingFromJson(const json& j)
{
	if (j.contains("padding") && j["padding"].is_array() && j["padding"].size() == 4)
	{
		PaddingLeft = j["padding"][0]; PaddingRight = j["padding"][1];
		PaddingTop = j["padding"][2]; PaddingBottom = j["padding"][3];
	}
	ChildAlignment = std::clamp(j.value("childAlignment", 0), 0, 8);
}

namespace
{
	const char* kAlignments[] = { "Upper Left", "Upper Center", "Upper Right", "Middle Left", "Middle Center", "Middle Right", "Lower Left", "Lower Center", "Lower Right" };
}

// ------------------------------------------------------------------ Horizontal / Vertical (Unity HorizontalOrVerticalLayoutGroup)
void HorizontalOrVerticalLayoutGroup::GetChildSizes(RectTransform* child, int axis, bool controlSize, bool forceExpand, float& min, float& preferred, float& flexible) const
{
	if (!controlSize)
	{
		min = Axis(child->GetSizeDelta(), axis);
		preferred = min;
		flexible = 0.0f;
	}
	else
	{
		min = UILayout::GetMinSize(child, axis);
		preferred = UILayout::GetPreferredSize(child, axis);
		flexible = UILayout::GetFlexibleSize(child, axis);
	}
	if (forceExpand)
		flexible = (std::max)(flexible, 1.0f);
}

void HorizontalOrVerticalLayoutGroup::CalcAlongAxis(int axis)
{
	const float padding = axis == 0 ? (float)(PaddingLeft + PaddingRight) : (float)(PaddingTop + PaddingBottom);
	const bool controlSize = axis == 0 ? ChildControlWidth : ChildControlHeight;
	const bool useScale = axis == 0 ? ChildScaleWidth : ChildScaleHeight;
	const bool forceExpand = axis == 0 ? ChildForceExpandWidth : ChildForceExpandHeight;
	float totalMin = padding, totalPreferred = padding, totalFlexible = 0.0f;
	const bool alongOther = IsVertical() ^ (axis == 1);
	const std::vector<RectTransform*> children = Children();
	for (RectTransform* child : children)
	{
		float min, preferred, flexible;
		GetChildSizes(child, axis, controlSize, forceExpand, min, preferred, flexible);
		if (useScale)
		{
			const Vec3 s = child->GetGameObject()->GetTransform()->GetLocalScale();
			const float k = axis == 0 ? s.x : s.y;
			min *= k; preferred *= k; flexible *= k;
		}
		if (alongOther)
		{
			totalMin = (std::max)(min + padding, totalMin);
			totalPreferred = (std::max)(preferred + padding, totalPreferred);
			totalFlexible = (std::max)(flexible, totalFlexible);
		}
		else
		{
			totalMin += min + Spacing;
			totalPreferred += preferred + Spacing;
			totalFlexible += flexible;
		}
	}
	if (!alongOther && !children.empty())
	{
		totalMin -= Spacing;
		totalPreferred -= Spacing;
	}
	totalPreferred = (std::max)(totalMin, totalPreferred);
	m_Total[axis][0] = totalMin;
	m_Total[axis][1] = totalPreferred;
	m_Total[axis][2] = totalFlexible;
}

void HorizontalOrVerticalLayoutGroup::SetLayoutAlongAxis(int axis)
{
	RectTransform* rt = RectTransform::Of(m_pGameObject);
	if (rt == nullptr)
		return;
	const float size = Axis(rt->GetRectSize(), axis);
	const bool controlSize = axis == 0 ? ChildControlWidth : ChildControlHeight;
	const bool useScale = axis == 0 ? ChildScaleWidth : ChildScaleHeight;
	const bool forceExpand = axis == 0 ? ChildForceExpandWidth : ChildForceExpandHeight;
	const float alignment = AlignmentOnAxis(axis);
	const bool alongOther = IsVertical() ^ (axis == 1);
	const float padding = axis == 0 ? (float)(PaddingLeft + PaddingRight) : (float)(PaddingTop + PaddingBottom);
	std::vector<RectTransform*> children = Children();
	if (ReverseArrangement)
		std::reverse(children.begin(), children.end());
	auto scaleOf = [&](RectTransform* c) {
		if (!useScale) return 1.0f;
		const Vec3 s = c->GetGameObject()->GetTransform()->GetLocalScale();
		return axis == 0 ? s.x : s.y;
	};
	if (alongOther)
	{
		const float inner = size - padding;
		for (RectTransform* child : children)
		{
			float min, preferred, flexible;
			GetChildSizes(child, axis, controlSize, forceExpand, min, preferred, flexible);
			const float k = scaleOf(child);
			const float required = std::clamp(inner, min, flexible > 0.0f ? size : preferred);
			const float start = StartOffset(axis, required * k);
			if (controlSize)
				SetChildAlongAxis(child, axis, start, required, k);
			else
			{
				const float offsetInCell = (required - Axis(child->GetSizeDelta(), axis)) * alignment;
				SetChildAlongAxisKeepSize(child, axis, start + offsetInCell, k);
			}
		}
		return;
	}
	float pos = axis == 0 ? (float)PaddingLeft : (float)PaddingTop;
	float flexibleMultiplier = 0.0f;
	const float surplus = size - TotalPreferred(axis);
	if (surplus > 0.0f)
	{
		if (TotalFlexible(axis) == 0.0f)
			pos = StartOffset(axis, TotalPreferred(axis) - padding);
		else if (TotalFlexible(axis) > 0.0f)
			flexibleMultiplier = surplus / TotalFlexible(axis);
	}
	float minMaxLerp = 0.0f;
	if (TotalMin(axis) != TotalPreferred(axis))
		minMaxLerp = std::clamp((size - TotalMin(axis)) / (TotalPreferred(axis) - TotalMin(axis)), 0.0f, 1.0f);
	for (RectTransform* child : children)
	{
		float min, preferred, flexible;
		GetChildSizes(child, axis, controlSize, forceExpand, min, preferred, flexible);
		const float k = scaleOf(child);
		float childSize = min + (preferred - min) * minMaxLerp;
		childSize += flexible * flexibleMultiplier;
		if (controlSize)
			SetChildAlongAxis(child, axis, pos, childSize, k);
		else
		{
			const float offsetInCell = (childSize - Axis(child->GetSizeDelta(), axis)) * alignment;
			SetChildAlongAxisKeepSize(child, axis, pos + offsetInCell, k);
		}
		pos += childSize * k + Spacing;
	}
}

void HorizontalOrVerticalLayoutGroup::OnInspectorGUI()
{
	DrawPaddingAndAlignment();
	UnityGUI::Float("Spacing", &Spacing);
	UnityGUI::Dropdown("Child Alignment", &ChildAlignment, kAlignments, 9);
	UnityGUI::Toggle("Reverse Arrangement", &ReverseArrangement);
	UnityGUI::Label("Control Child Size", 0);
	UnityGUI::Toggle("Width##control", &ChildControlWidth, 1);
	UnityGUI::Toggle("Height##control", &ChildControlHeight, 1);
	UnityGUI::Label("Use Child Scale", 0);
	UnityGUI::Toggle("Width##scale", &ChildScaleWidth, 1);
	UnityGUI::Toggle("Height##scale", &ChildScaleHeight, 1);
	UnityGUI::Label("Child Force Expand", 0);
	UnityGUI::Toggle("Width##expand", &ChildForceExpandWidth, 1);
	UnityGUI::Toggle("Height##expand", &ChildForceExpandHeight, 1);
}

void HorizontalOrVerticalLayoutGroup::GroupToJson(json& j) const
{
	PaddingToJson(j);
	j["spacing"] = Spacing;
	j["childForceExpand"] = { ChildForceExpandWidth, ChildForceExpandHeight };
	j["childControlSize"] = { ChildControlWidth, ChildControlHeight };
	j["childScale"] = { ChildScaleWidth, ChildScaleHeight };
	j["reverseArrangement"] = ReverseArrangement;
}

void HorizontalOrVerticalLayoutGroup::GroupFromJson(const json& j)
{
	PaddingFromJson(j);
	Spacing = j.value("spacing", 0.0f);
	auto pair = [&](const char* key, bool& a, bool& b, bool da, bool db) {
		a = da; b = db;
		if (j.contains(key) && j[key].is_array() && j[key].size() == 2) { a = j[key][0]; b = j[key][1]; }
	};
	pair("childForceExpand", ChildForceExpandWidth, ChildForceExpandHeight, true, true);
	pair("childControlSize", ChildControlWidth, ChildControlHeight, true, true);
	pair("childScale", ChildScaleWidth, ChildScaleHeight, false, false);
	ReverseArrangement = j.value("reverseArrangement", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(HorizontalLayoutGroup)
{
	json j;
	j["type"] = "HorizontalLayoutGroup";
	j["enabled"] = m_Enabled;
	GroupToJson(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(HorizontalLayoutGroup)
{
	m_Enabled = j.value("enabled", true);
	GroupFromJson(j);
}

GENERATE_COMPONENT_FUNC_TOJSON(VerticalLayoutGroup)
{
	json j;
	j["type"] = "VerticalLayoutGroup";
	j["enabled"] = m_Enabled;
	GroupToJson(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(VerticalLayoutGroup)
{
	m_Enabled = j.value("enabled", true);
	GroupFromJson(j);
}

// ------------------------------------------------------------------ Grid (Unity GridLayoutGroup)
void GridLayoutGroup::CalcAlongAxis(int axis)
{
	const int count = (int)Children().size();
	RectTransform* rt = RectTransform::Of(m_pGameObject);
	if (axis == 0)
	{
		int minColumns = 0, preferredColumns = 0;
		if (ConstraintMode == FixedColumnCount) minColumns = preferredColumns = ConstraintCount;
		else if (ConstraintMode == FixedRowCount) minColumns = preferredColumns = (int)ceilf(count / (float)(std::max)(1, ConstraintCount) - 0.001f);
		else { minColumns = 1; preferredColumns = (int)ceilf(sqrtf((float)count)); }
		const float padding = (float)(PaddingLeft + PaddingRight);
		m_Total[0][0] = padding + (CellSize.x + Spacing.x) * minColumns - Spacing.x;
		m_Total[0][1] = padding + (CellSize.x + Spacing.x) * preferredColumns - Spacing.x;
		m_Total[0][2] = -1.0f;
	}
	else
	{
		int minRows = 0;
		if (ConstraintMode == FixedColumnCount) minRows = (int)ceilf(count / (float)(std::max)(1, ConstraintCount) - 0.001f);
		else if (ConstraintMode == FixedRowCount) minRows = ConstraintCount;
		else
		{
			const float width = rt ? rt->GetRectSize().x : 0.0f;
			const int cellCountX = (std::max)(1, (int)floorf((width - (PaddingLeft + PaddingRight) + Spacing.x + 0.001f) / (CellSize.x + Spacing.x)));
			minRows = (int)ceilf(count / (float)cellCountX);
		}
		const float padding = (float)(PaddingTop + PaddingBottom);
		m_Total[1][0] = m_Total[1][1] = padding + (CellSize.y + Spacing.y) * minRows - Spacing.y;
		m_Total[1][2] = -1.0f;
	}
}

void GridLayoutGroup::SetLayoutAlongAxis(int axis)
{
	// Unity 는 두 축을 세로 단계에서 한 번에 놓는다 (가로 단계에서는 칸 크기만)
	const std::vector<RectTransform*> children = Children();
	RectTransform* rt = RectTransform::Of(m_pGameObject);
	if (rt == nullptr)
		return;
	if (axis == 0)
		return;
	const int count = (int)children.size();
	const Vec2 size = rt->GetRectSize();
	int cellCountX = 1, cellCountY = 1;
	if (ConstraintMode == FixedColumnCount)
	{
		cellCountX = (std::max)(1, ConstraintCount);
		cellCountY = (int)ceilf(count / (float)cellCountX - 0.001f);
	}
	else if (ConstraintMode == FixedRowCount)
	{
		cellCountY = (std::max)(1, ConstraintCount);
		cellCountX = (int)ceilf(count / (float)cellCountY - 0.001f);
	}
	else
	{
		cellCountX = CellSize.x + Spacing.x <= 0 ? INT_MAX : (std::max)(1, (int)floorf((size.x - (PaddingLeft + PaddingRight) + Spacing.x + 0.001f) / (CellSize.x + Spacing.x)));
		cellCountY = CellSize.y + Spacing.y <= 0 ? INT_MAX : (std::max)(1, (int)floorf((size.y - (PaddingTop + PaddingBottom) + Spacing.y + 0.001f) / (CellSize.y + Spacing.y)));
	}
	const int cornerX = StartCorner % 2, cornerY = StartCorner / 2;
	int cellsPerMainAxis, actualX, actualY;
	if (StartAxis == Horizontal)
	{
		cellsPerMainAxis = cellCountX;
		actualX = std::clamp(cellCountX, 1, (std::max)(1, count));
		actualY = std::clamp(cellCountY, 1, (int)ceilf(count / (float)cellsPerMainAxis));
	}
	else
	{
		cellsPerMainAxis = cellCountY;
		actualY = std::clamp(cellCountY, 1, (std::max)(1, count));
		actualX = std::clamp(cellCountX, 1, (int)ceilf(count / (float)cellsPerMainAxis));
	}
	const Vec2 required(actualX * CellSize.x + (actualX - 1) * Spacing.x, actualY * CellSize.y + (actualY - 1) * Spacing.y);
	const Vec2 start(StartOffset(0, required.x), StartOffset(1, required.y));
	for (int i = 0; i < count; ++i)
	{
		int px, py;
		if (StartAxis == Horizontal) { px = i % cellsPerMainAxis; py = i / cellsPerMainAxis; }
		else { px = i / cellsPerMainAxis; py = i % cellsPerMainAxis; }
		if (cornerX == 1) px = actualX - 1 - px;
		if (cornerY == 1) py = actualY - 1 - py;
		SetChildAlongAxis(children[i], 0, start.x + (CellSize.x + Spacing.x) * px, CellSize.x);
		SetChildAlongAxis(children[i], 1, start.y + (CellSize.y + Spacing.y) * py, CellSize.y);
	}
}

void GridLayoutGroup::OnInspectorGUI()
{
	DrawPaddingAndAlignment();
	UnityGUI::Vector2Pair("Cell Size", "X", &CellSize.x, "Y", &CellSize.y);
	UnityGUI::Vector2Pair("Spacing", "X", &Spacing.x, "Y", &Spacing.y);
	static const char* kCorners[] = { "Upper Left", "Upper Right", "Lower Left", "Lower Right" };
	static const char* kAxes[] = { "Horizontal", "Vertical" };
	static const char* kConstraints[] = { "Flexible", "Fixed Column Count", "Fixed Row Count" };
	UnityGUI::Dropdown("Start Corner", &StartCorner, kCorners, 4);
	UnityGUI::Dropdown("Start Axis", &StartAxis, kAxes, 2);
	UnityGUI::Dropdown("Child Alignment", &ChildAlignment, kAlignments, 9);
	UnityGUI::Dropdown("Constraint", &ConstraintMode, kConstraints, 3);
	if (ConstraintMode != Flexible && UnityGUI::Int("Constraint Count", &ConstraintCount, 1))
		ConstraintCount = (std::max)(1, ConstraintCount);
}

GENERATE_COMPONENT_FUNC_TOJSON(GridLayoutGroup)
{
	json j;
	j["type"] = "GridLayoutGroup";
	j["enabled"] = m_Enabled;
	PaddingToJson(j);
	j["cellSize"] = { CellSize.x, CellSize.y };
	j["spacing"] = { Spacing.x, Spacing.y };
	j["startCorner"] = StartCorner;
	j["startAxis"] = StartAxis;
	j["constraint"] = ConstraintMode;
	j["constraintCount"] = ConstraintCount;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(GridLayoutGroup)
{
	m_Enabled = j.value("enabled", true);
	PaddingFromJson(j);
	auto vec2 = [&](const char* key, Vec2 d) {
		return j.contains(key) && j[key].is_array() && j[key].size() == 2 ? Vec2(j[key][0].get<float>(), j[key][1].get<float>()) : d;
	};
	CellSize = vec2("cellSize", Vec2(100, 100));
	Spacing = vec2("spacing", Vec2(0, 0));
	StartCorner = std::clamp(j.value("startCorner", 0), 0, 3);
	StartAxis = std::clamp(j.value("startAxis", 0), 0, 1);
	ConstraintMode = std::clamp(j.value("constraint", 0), 0, 2);
	ConstraintCount = (std::max)(1, j.value("constraintCount", 2));
}

// ------------------------------------------------------------------ Content Size Fitter
void ContentSizeFitter::Apply(int axis)
{
	const int fit = axis == 0 ? HorizontalFit : VerticalFit;
	RectTransform* rt = RectTransform::Of(m_pGameObject);
	if (fit == Unconstrained || rt == nullptr)
		return;
	const float size = fit == MinSize ? UILayout::GetMinSize(rt, axis) : UILayout::GetPreferredSize(rt, axis);
	// Unity SetSizeWithCurrentAnchors: 앵커 사이 거리를 빼고 sizeDelta 로
	const Vec2 parentSize = rt->GetParentSize();
	const float anchorSpan = Axis(parentSize, axis) * (Axis(rt->GetAnchorMax(), axis) - Axis(rt->GetAnchorMin(), axis));
	Vec2 sd = rt->GetSizeDelta();
	SetAxis(sd, axis, size - anchorSpan);
	rt->SetSizeDelta(sd);
	rt->Relayout();
	rt->SetDrivenBy("Content Size Fitter");
}

void ContentSizeFitter::OnInspectorGUI()
{
	static const char* kModes[] = { "Unconstrained", "Min Size", "Preferred Size" };
	UnityGUI::Dropdown("Horizontal Fit", &HorizontalFit, kModes, 3);
	UnityGUI::Dropdown("Vertical Fit", &VerticalFit, kModes, 3);
	if (m_pGameObject && m_pGameObject->GetParent() && Enabled<LayoutGroup>(m_pGameObject->GetParent()))
		UnityGUI::HelpBox("Parent has a layout group: the parent controls this size (put Content Size Fitter on the group's object instead).", true);
}

GENERATE_COMPONENT_FUNC_TOJSON(ContentSizeFitter)
{
	json j;
	j["type"] = "ContentSizeFitter";
	j["enabled"] = m_Enabled;
	j["horizontalFit"] = HorizontalFit;
	j["verticalFit"] = VerticalFit;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(ContentSizeFitter)
{
	m_Enabled = j.value("enabled", true);
	HorizontalFit = std::clamp(j.value("horizontalFit", 0), 0, 2);
	VerticalFit = std::clamp(j.value("verticalFit", 0), 0, 2);
}

// ------------------------------------------------------------------ Aspect Ratio Fitter
void AspectRatioFitter::Apply()
{
	RectTransform* rt = RectTransform::Of(m_pGameObject);
	if (rt == nullptr || AspectMode == None || AspectRatio <= 0.0f)
		return;
	const Vec2 parent = rt->GetParentSize();
	const Vec2 span = parent * (rt->GetAnchorMax() - rt->GetAnchorMin());
	Vec2 sd = rt->GetSizeDelta();
	const Vec2 size = rt->GetRectSize();
	switch (AspectMode)
	{
	case WidthControlsHeight: sd.y = size.x / AspectRatio - span.y; break;
	case HeightControlsWidth: sd.x = size.y * AspectRatio - span.x; break;
	case FitInParent:
	case EnvelopeParent:
	{
		rt->SetAnchorMin(Vec2(0, 0));
		rt->SetAnchorMax(Vec2(1, 1));
		rt->SetAnchoredPosition(Vec2(0, 0));
		Vec2 s = parent;
		const bool parentWider = parent.y > 0.0f && parent.x / parent.y > AspectRatio;
		if (parentWider == (AspectMode == FitInParent)) s.x = parent.y * AspectRatio;
		else s.y = parent.x / AspectRatio;
		sd = s - parent;
		break;
	}
	}
	rt->SetSizeDelta(sd);
	rt->Relayout();
	rt->SetDrivenBy("Aspect Ratio Fitter");
}

void AspectRatioFitter::OnInspectorGUI()
{
	static const char* kModes[] = { "None", "Width Controls Height", "Height Controls Width", "Fit In Parent", "Envelope Parent" };
	UnityGUI::Dropdown("Aspect Mode", &AspectMode, kModes, 5);
	if (UnityGUI::Float("Aspect Ratio", &AspectRatio))
		AspectRatio = (std::max)(0.001f, AspectRatio);
}

GENERATE_COMPONENT_FUNC_TOJSON(AspectRatioFitter)
{
	json j;
	j["type"] = "AspectRatioFitter";
	j["enabled"] = m_Enabled;
	j["aspectMode"] = AspectMode;
	j["aspectRatio"] = AspectRatio;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(AspectRatioFitter)
{
	m_Enabled = j.value("enabled", true);
	AspectMode = std::clamp(j.value("aspectMode", 0), 0, 4);
	AspectRatio = (std::max)(0.001f, j.value("aspectRatio", 1.0f));
}

// ------------------------------------------------------------------ Layout Element
void LayoutElement::OnInspectorGUI()
{
	UnityGUI::Toggle("Ignore Layout", &IgnoreLayout);
	if (IgnoreLayout)
		return;
	// Unity: 체크하면 값, 끄면 -1 (정하지 않음)
	auto row = [&](const char* label, float* v, float onValue) {
		bool on = *v >= 0.0f;
		ImGui::PushID(label);
		if (UnityGUI::Toggle(label, &on))
			*v = on ? onValue : -1.0f;
		if (on && UnityGUI::Float("##value", v, 1))
			*v = (std::max)(0.0f, *v);
		ImGui::PopID();
	};
	RectTransform* rt = RectTransform::Of(m_pGameObject);
	const Vec2 sz = rt ? rt->GetRectSize() : Vec2(100, 100);
	row("Min Width", &MinWidth, 0.0f);
	row("Min Height", &MinHeight, 0.0f);
	row("Preferred Width", &PreferredWidth, sz.x);
	row("Preferred Height", &PreferredHeight, sz.y);
	row("Flexible Width", &FlexibleWidth, 1.0f);
	row("Flexible Height", &FlexibleHeight, 1.0f);
	UnityGUI::Int("Layout Priority", &LayoutPriority);
}

GENERATE_COMPONENT_FUNC_TOJSON(LayoutElement)
{
	json j;
	j["type"] = "LayoutElement";
	j["enabled"] = m_Enabled;
	j["ignoreLayout"] = IgnoreLayout;
	j["min"] = { MinWidth, MinHeight };
	j["preferred"] = { PreferredWidth, PreferredHeight };
	j["flexible"] = { FlexibleWidth, FlexibleHeight };
	j["layoutPriority"] = LayoutPriority;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(LayoutElement)
{
	m_Enabled = j.value("enabled", true);
	IgnoreLayout = j.value("ignoreLayout", false);
	auto pair = [&](const char* key, float& a, float& b) {
		a = b = -1.0f;
		if (j.contains(key) && j[key].is_array() && j[key].size() == 2) { a = j[key][0].get<float>(); b = j[key][1].get<float>(); }
	};
	pair("min", MinWidth, MinHeight);
	pair("preferred", PreferredWidth, PreferredHeight);
	pair("flexible", FlexibleWidth, FlexibleHeight);
	LayoutPriority = j.value("layoutPriority", 1);
}
