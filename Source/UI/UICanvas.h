#pragma once
#include "Component.h"

// Unity 의 Canvas (UI 를 그리는 영역).
//  - Screen Space - Overlay: 화면 맨 위에 픽셀 단위. Transform = 화면 가운데·배율, RectTransform 크기 = 화면 / Canvas Scaler 배율
//  - Screen Space - Camera: Render Camera 앞 Plane Distance 에 화면 크기로 놓인다 (3D 물체가 앞에 오면 가린다, 카메라가 없으면 Overlay 로)
//  - World Space: 씬 안의 평면 (Transform 위치 · 회전 · 크기 그대로, 크기 = Rect Transform Width/Height) — 머리 위 체력바, 3D 메뉴
//    입력은 Event Camera(없으면 Game 뷰 카메라) 의 마우스 광선과 평면이 만나는 점, 씬 깊이로 가려진다
class Canvas : public Component
{
public:
	enum class RenderMode { ScreenSpaceOverlay = 0, ScreenSpaceCamera = 1, WorldSpace = 2 };

	Canvas();
	virtual ~Canvas();

	RenderMode GetRenderMode() const { return m_RenderMode; }
	void SetRenderMode(RenderMode m) { m_RenderMode = m; }
	uint64 GetWorldCamera() const { return m_WorldCamera; }
	void SetWorldCamera(uint64 fileID) { m_WorldCamera = fileID; }
	float GetPlaneDistance() const { return m_PlaneDistance; }
	void SetPlaneDistance(float d) { m_PlaneDistance = (std::max)(0.01f, d); }
	// Screen Space - Camera 의 Render Camera / World Space 의 Event Camera (없으면 nullptr)
	class Camera* FindWorldCamera() const;
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	int GetSortOrder() const { return m_SortOrder; }
	void SetSortOrder(int order) { m_SortOrder = order; }
	int GetTargetDisplay() const { return m_TargetDisplay; }
	bool IsPixelPerfect() const { return m_PixelPerfect; }
	float GetScaleFactor() const { return m_ScaleFactor; }   // 마지막 레이아웃에서 쓴 배율
	void SetScaleFactor(float s) { m_ScaleFactor = s; }

	static const std::vector<Canvas*>& All() { return s_All; }

	virtual void OnDrawGizmos() override;   // Scene 뷰: 캔버스 테두리 (흰 선)
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "canvas"; }

	GENERATE_COMPONENT_BODY(Canvas)

private:
	RenderMode m_RenderMode = RenderMode::ScreenSpaceOverlay;
	uint64 m_WorldCamera = 0;      // Render Camera (Camera 모드) / Event Camera (World 모드)
	float m_PlaneDistance = 100.0f;
	bool m_PixelPerfect = false;
	int m_SortOrder = 0;
	int m_TargetDisplay = 0;
	bool m_VertexColorGamma = false;
	float m_ScaleFactor = 1.0f;
	static std::vector<Canvas*> s_All;
};
REGISTER_COMPONENT(Canvas)

// Unity 의 Canvas Scaler: 화면 크기에 따라 UI 배율을 정한다
class CanvasScaler : public Component
{
public:
	enum class ScaleMode { ConstantPixelSize = 0, ScaleWithScreenSize = 1, ConstantPhysicalSize = 2 };
	enum class MatchMode { MatchWidthOrHeight = 0, Expand = 1, Shrink = 2 };

	CanvasScaler();
	float ComputeScale(float screenW, float screenH) const;
	ScaleMode GetMode() const { return m_Mode; }
	void SetMode(ScaleMode m) { m_Mode = m; }
	void SetReferenceResolution(float w, float h) { m_RefW = w; m_RefH = h; }
	void SetMatch(float m) { m_Match = m; }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "canvas_scaler"; }
	virtual bool HasEnabledToggle() const override { return true; }

	GENERATE_COMPONENT_BODY(CanvasScaler)

private:
	ScaleMode m_Mode = ScaleMode::ConstantPixelSize;
	float m_ScaleFactor = 1.0f;
	float m_RefW = 800.0f, m_RefH = 600.0f;
	MatchMode m_MatchMode = MatchMode::MatchWidthOrHeight;
	float m_Match = 0.0f;
	float m_RefPixelsPerUnit = 100.0f;
};
REGISTER_COMPONENT(CanvasScaler)

// Unity 의 Graphic Raycaster: 이 Canvas 의 UI 가 마우스 클릭을 받게 한다 (없으면 버튼이 눌리지 않음)
class GraphicRaycaster : public Component
{
public:
	GraphicRaycaster();
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "graphic_raycaster"; }
	GENERATE_COMPONENT_BODY(GraphicRaycaster)
private:
	bool m_IgnoreReversed = true;
	int m_BlockingObjects = 0;
	uint32 m_BlockingMask = 0xFFFFFFFF;
};
REGISTER_COMPONENT(GraphicRaycaster)

// Unity 의 Event System: 씬에 하나 있어야 UI 가 입력(마우스 오버, 클릭)을 받는다
class EventSystem : public Component
{
public:
	EventSystem();
	virtual ~EventSystem();
	static bool AnyActive();

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "event_system"; }
	GENERATE_COMPONENT_BODY(EventSystem)
private:
	uint64 m_FirstSelected = 0;
	bool m_SendNavigationEvents = true;
	int m_DragThreshold = 10;
	static std::vector<EventSystem*> s_All;
};
REGISTER_COMPONENT(EventSystem)
