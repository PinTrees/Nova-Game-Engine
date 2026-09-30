#pragma once
#include "Component.h"

class UIRenderer;
class RectTransform;

// Unity 의 Graphic (Image, Text 의 공통 부모): 색, Raycast Target(클릭을 받는지), Button 이 주는 색 전환(곱하기).
class UIGraphic : public Component
{
public:
	UIGraphic();
	virtual ~UIGraphic();

	const float* GetColor() const { return m_Color; }
	void SetColor(const float rgba[4]) { memcpy(m_Color, rgba, sizeof(m_Color)); }
	bool IsRaycastTarget() const { return m_RaycastTarget; }
	void SetRaycastTarget(bool v) { m_RaycastTarget = v; }
	// Button 의 Color Tint (Play 중 상태 색, 곱하기)
	void SetTint(const float rgba[4]) { memcpy(m_Tint, rgba, sizeof(m_Tint)); }
	void ResetTint() { m_Tint[0] = m_Tint[1] = m_Tint[2] = m_Tint[3] = 1.0f; }

	// 캔버스 월드 정점을 만든다. canvasScale = 캔버스 단위 1 이 화면 몇 픽셀인지 (글자를 선명한 크기로 굽기 위해)
	virtual void Populate(UIRenderer& renderer, float canvasScale) = 0;

	RectTransform* GetRect();
	static const std::vector<UIGraphic*>& All() { return s_All; }

protected:
	void FinalColor(float out[4]) const;
	uint32 PackedColor() const;
	// 로컬(피벗 원점) 점 → 캔버스 월드
	Vec3 ToWorld(const Matrix& world, float x, float y) const { return Vec3::Transform(Vec3(x, y, 0.0f), world); }
	void GraphicToJson(json& j) const;
	void GraphicFromJson(const json& j);
	void DrawColorAndRaycast();

	float m_Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_Tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool m_RaycastTarget = true;
	bool m_Maskable = true;

private:
	static std::vector<UIGraphic*> s_All;
};
