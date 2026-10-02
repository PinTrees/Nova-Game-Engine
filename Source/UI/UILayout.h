#pragma once
#include "Component.h"

// Unity 의 UI 자동 레이아웃 (UnityEngine.UI 의 Layout 컴포넌트와 같은 계산).
//  - Layout Element: 이 요소의 Min / Preferred / Flexible 크기를 직접 정하거나 (Ignore Layout = 그룹에서 빼기)
//  - Horizontal / Vertical Layout Group: 자식을 한 줄로 (Padding, Spacing, Child Alignment, Control Child Size, Use Child Scale,
//    Child Force Expand, Reverse Arrangement)
//  - Grid Layout Group: 같은 크기 칸 (Cell Size, Spacing, Start Corner, Start Axis, Constraint = Flexible / Fixed Column / Row Count)
//  - Content Size Fitter: 자기 크기를 내용(Min / Preferred) 에 맞춘다 (Scroll View 의 Content 등)
//  - Aspect Ratio Fitter: 가로세로 비율 (Width/Height Controls, Fit In / Envelope Parent)
// 크기 정보는 Text(글 크기) · Image(스프라이트 크기) · 그룹(자식 합) · Layout Element(우선순위) 에서 온다.
// 계산은 UISystem::Update 에서 RectTransform 레이아웃 뒤에: 가로를 먼저 정하고 세로 (글 줄바꿈 높이가 폭에 따르므로).
// 그룹 · 맞춤이 정한 값(자식의 앵커 · 위치 · 크기)은 "driven" — Rect Transform Inspector 에 안내가 뜬다.

class RectTransform;

class LayoutElement : public Component
{
public:
	bool IgnoreLayout = false;
	float MinWidth = -1, MinHeight = -1, PreferredWidth = -1, PreferredHeight = -1, FlexibleWidth = -1, FlexibleHeight = -1;   // -1 = 정하지 않음
	int LayoutPriority = 1;

	LayoutElement() { m_InspectorTitleName = "Layout Element"; }
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rect_transform"; }
	GENERATE_COMPONENT_BODY(LayoutElement)
};
REGISTER_COMPONENT(LayoutElement)

// Horizontal / Vertical / Grid 의 공통 부모
class LayoutGroup : public Component
{
public:
	int PaddingLeft = 0, PaddingRight = 0, PaddingTop = 0, PaddingBottom = 0;
	int ChildAlignment = 0;   // 0 UpperLeft .. 8 LowerRight (TextAnchor)

	// 이 GameObject 의 자식을 배치 (axis 0 가로, 1 세로) / 크기 정보 (자식 합)
	virtual void CalcAlongAxis(int axis) = 0;
	virtual void SetLayoutAlongAxis(int axis) = 0;
	float TotalMin(int axis) const { return m_Total[axis][0]; }
	float TotalPreferred(int axis) const { return m_Total[axis][1]; }
	float TotalFlexible(int axis) const { return m_Total[axis][2]; }

protected:
	float m_Total[2][3] = {};
	std::vector<RectTransform*> Children() const;   // 켜진 자식 (Ignore Layout 제외)
	float AlignmentOnAxis(int axis) const { return axis == 0 ? (ChildAlignment % 3) * 0.5f : (ChildAlignment / 3) * 0.5f; }
	float StartOffset(int axis, float requiredWithoutPadding) const;
	void SetChildAlongAxis(RectTransform* child, int axis, float pos, float size, float scale = 1.0f) const;
	void SetChildAlongAxisKeepSize(RectTransform* child, int axis, float pos, float scale = 1.0f) const;
	void DrawPaddingAndAlignment();
	void PaddingToJson(json& j) const;
	void PaddingFromJson(const json& j);
};

class HorizontalOrVerticalLayoutGroup : public LayoutGroup
{
public:
	float Spacing = 0.0f;
	bool ChildForceExpandWidth = true, ChildForceExpandHeight = true;
	bool ChildControlWidth = true, ChildControlHeight = true;
	bool ChildScaleWidth = false, ChildScaleHeight = false;
	bool ReverseArrangement = false;

	void CalcAlongAxis(int axis) override;
	void SetLayoutAlongAxis(int axis) override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rect_transform"; }

protected:
	virtual bool IsVertical() const = 0;
	void GetChildSizes(RectTransform* child, int axis, bool controlSize, bool forceExpand, float& min, float& preferred, float& flexible) const;
	void GroupToJson(json& j) const;
	void GroupFromJson(const json& j);
};

class HorizontalLayoutGroup : public HorizontalOrVerticalLayoutGroup
{
public:
	HorizontalLayoutGroup() { m_InspectorTitleName = "Horizontal Layout Group"; }
	GENERATE_COMPONENT_BODY(HorizontalLayoutGroup)
protected:
	bool IsVertical() const override { return false; }
};
REGISTER_COMPONENT(HorizontalLayoutGroup)

class VerticalLayoutGroup : public HorizontalOrVerticalLayoutGroup
{
public:
	VerticalLayoutGroup() { m_InspectorTitleName = "Vertical Layout Group"; }
	GENERATE_COMPONENT_BODY(VerticalLayoutGroup)
protected:
	bool IsVertical() const override { return true; }
};
REGISTER_COMPONENT(VerticalLayoutGroup)

class GridLayoutGroup : public LayoutGroup
{
public:
	enum Corner { UpperLeft = 0, UpperRight = 1, LowerLeft = 2, LowerRight = 3 };
	enum Axis { Horizontal = 0, Vertical = 1 };
	enum Constraint { Flexible = 0, FixedColumnCount = 1, FixedRowCount = 2 };
	Vec2 CellSize = Vec2(100, 100);
	Vec2 Spacing = Vec2(0, 0);
	int StartCorner = UpperLeft, StartAxis = Horizontal;
	int ConstraintMode = Flexible, ConstraintCount = 2;

	GridLayoutGroup() { m_InspectorTitleName = "Grid Layout Group"; }
	void CalcAlongAxis(int axis) override;
	void SetLayoutAlongAxis(int axis) override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rect_transform"; }
	GENERATE_COMPONENT_BODY(GridLayoutGroup)
};
REGISTER_COMPONENT(GridLayoutGroup)

class ContentSizeFitter : public Component
{
public:
	enum FitMode { Unconstrained = 0, MinSize = 1, PreferredSize = 2 };
	int HorizontalFit = Unconstrained, VerticalFit = Unconstrained;

	ContentSizeFitter() { m_InspectorTitleName = "Content Size Fitter"; }
	void Apply(int axis);
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rect_transform"; }
	GENERATE_COMPONENT_BODY(ContentSizeFitter)
};
REGISTER_COMPONENT(ContentSizeFitter)

class AspectRatioFitter : public Component
{
public:
	enum Mode { None = 0, WidthControlsHeight = 1, HeightControlsWidth = 2, FitInParent = 3, EnvelopeParent = 4 };
	int AspectMode = None;
	float AspectRatio = 1.0f;

	AspectRatioFitter() { m_InspectorTitleName = "Aspect Ratio Fitter"; }
	void Apply();
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rect_transform"; }
	GENERATE_COMPONENT_BODY(AspectRatioFitter)
};
REGISTER_COMPONENT(AspectRatioFitter)

namespace UILayout
{
	// Unity LayoutUtility: 이 요소의 크기 정보 (Text · Image · 그룹 · Layout Element 중 우선순위가 높은 값)
	float GetMinSize(RectTransform* rt, int axis);
	float GetPreferredSize(RectTransform* rt, int axis);
	float GetFlexibleSize(RectTransform* rt, int axis);
	bool IgnoresLayout(GameObject* go);

	// 이 GameObject 부터 아래로: 맞춤 · 그룹 계산 (UISystem::Update 가 캔버스마다 RectTransform 레이아웃 뒤에)
	void Apply(GameObject* root);
	// Unity LayoutRebuilder.ForceRebuildLayoutImmediate: 이 요소가 들어 있는 캔버스를 지금 다시
	void ForceRebuild(GameObject* go);
}
