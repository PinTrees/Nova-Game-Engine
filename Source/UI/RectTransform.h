#pragma once
#include "Component.h"

// Unity 의 RectTransform (UI 요소의 위치/크기).
//  - anchorMin/anchorMax: 부모 사각형 안의 기준점 (0..1). 둘이 다르면 그 축은 늘어남(stretch)
//  - anchoredPosition: 기준점에서 피벗까지의 거리, sizeDelta: 기준점 사이 거리에 더할 크기, pivot: 자기 사각형 안의 원점 (0..1)
// 이 엔진에서는 Transform 이 그대로 있고, RectTransform 이 매 프레임 레이아웃을 계산해 Transform 의 로컬 위치(x, y)를 쓴다.
// 그래서 Scene 뷰 이동 도구나 C# 에서 Transform 위치를 바꾸면 그만큼 anchoredPosition 으로 되돌려 반영한다.
// 회전/크기는 Transform 의 값을 그대로 쓴다. 캔버스 월드 단위 = 화면 픽셀 (y 위쪽).
class RectTransform : public Component
{
public:
	RectTransform();
	virtual ~RectTransform();

	Vec2 GetAnchorMin() const { return m_AnchorMin; }
	Vec2 GetAnchorMax() const { return m_AnchorMax; }
	Vec2 GetAnchoredPosition() const { return m_AnchoredPosition; }
	Vec2 GetSizeDelta() const { return m_SizeDelta; }
	Vec2 GetPivot() const { return m_Pivot; }
	void SetAnchorMin(const Vec2& v) { m_AnchorMin = v; }
	void SetAnchorMax(const Vec2& v) { m_AnchorMax = v; }
	void SetAnchoredPosition(const Vec2& v) { m_AnchoredPosition = v; }
	void SetSizeDelta(const Vec2& v) { m_SizeDelta = v; }
	void SetPivot(const Vec2& v) { m_Pivot = v; }
	// 모서리 거리 (Unity offsetMin / offsetMax)
	Vec2 GetOffsetMin() const { return m_AnchoredPosition - m_SizeDelta * m_Pivot; }
	Vec2 GetOffsetMax() const { return m_AnchoredPosition + m_SizeDelta * (Vec2(1, 1) - m_Pivot); }
	void SetOffsets(const Vec2& offsetMin, const Vec2& offsetMax);
	// 사각형이 제자리에 있도록 기준점/피벗을 바꾼다 (Inspector 의 Anchors, Pivot, 프리셋과 같은 동작)
	void SetAnchorsKeepRect(const Vec2& anchorMin, const Vec2& anchorMax);
	void SetPivotKeepRect(const Vec2& pivot);

	// 레이아웃 결과 (자기 로컬 공간, 피벗이 원점)
	Vec2 GetRectMin() const { return m_RectMin; }
	Vec2 GetRectSize() const { return m_RectSize; }
	// 부모 사각형(부모 로컬 공간)으로 위치/크기를 계산해 Transform 에 쓴다
	void Layout(const Vec2& parentMin, const Vec2& parentSize);
	// 지난 레이아웃의 부모 사각형으로 다시 (Layout Group · Content Size Fitter 가 값을 바꾼 뒤)
	void Relayout() { Layout(m_ParentMin, m_ParentSize); }
	// World Space 캔버스 루트: 처음 레이아웃 전에 지금 Transform 위치를 anchoredPosition 으로 (기본값 0,0 이 위치를 덮어쓰지 않게)
	void AdoptTransformPosition();
	Vec2 GetParentSize() const { return m_ParentSize; }
	// Layout Group · Fitter 가 이 값을 정했다 (Inspector 안내, 프레임마다 다시 표시)
	void SetDrivenBy(const std::string& by) { m_DrivenBy = by; m_DrivenFrame = ImGui::GetFrameCount(); }
	// 캔버스 루트: 크기를 직접 정한다 (화면 / 배율)
	void SetDrivenRect(const Vec2& size);
	bool IsDrivenByCanvas() const { return m_Driven; }

	// 월드 모서리 4개: 왼쪽 아래, 왼쪽 위, 오른쪽 위, 오른쪽 아래 (Unity GetWorldCorners)
	void GetWorldCorners(Vec3 out[4]);
	// 캔버스 월드 점(z 무시)이 사각형 안인지
	bool ContainsWorldPoint(const Vec2& p);
	// 3D 점 (World Space 캔버스: 광선이 평면과 만난 점) — 자기 평면으로 되돌려 x, y 만 비교
	bool ContainsWorldPoint(const Vec3& p);

	static RectTransform* Of(GameObject* go);

	virtual void OnDrawGizmos() override;   // Scene 뷰: 선택되면 사각형 (파랑)
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual bool HasEnabledToggle() const override { return false; }
	virtual const char* InspectorIconName() const override { return "rect_transform"; }

	GENERATE_COMPONENT_BODY(RectTransform)

private:
	void DrawAnchorPresetButton(ImVec2 pos, float size);
	void DrawAnchorPresetPopup();

	Vec2 m_AnchorMin = Vec2(0.5f, 0.5f);
	Vec2 m_AnchorMax = Vec2(0.5f, 0.5f);
	Vec2 m_AnchoredPosition = Vec2(0.0f, 0.0f);
	Vec2 m_SizeDelta = Vec2(100.0f, 100.0f);
	Vec2 m_Pivot = Vec2(0.5f, 0.5f);

	// 실행 중 계산 값
	Vec2 m_RectMin = Vec2(-50.0f, -50.0f), m_RectSize = Vec2(100.0f, 100.0f);
	Vec2 m_ParentMin = Vec2(0, 0), m_ParentSize = Vec2(0, 0);
	Vec2 m_Written = Vec2(0, 0);   // 마지막으로 Transform 에 쓴 로컬 위치 (밖에서 바뀌었는지 비교)
	bool m_HasWritten = false;
	bool m_Driven = false;
	bool m_AnchorsOpen = true;
	std::string m_DrivenBy;
	int m_DrivenFrame = -100;
};

REGISTER_COMPONENT(RectTransform)
