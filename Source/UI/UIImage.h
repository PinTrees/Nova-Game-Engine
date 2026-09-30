#pragma once
#include "UIGraphic.h"

// Unity 의 Image (UI). 이름이 DirectX::Image 와 겹치지 않게 클래스는 UIImage, Inspector/메뉴에는 "Image".
//  - Source Image: 텍스처 파일(Assets) 또는 내장 스프라이트(UISprite, Background, Knob). 없으면 흰색 사각형
//  - Image Type: Simple(Preserve Aspect) / Sliced(9 조각, 내장 스프라이트의 테두리) / Tiled(= Simple) / Filled(가로·세로·원형 채우기)
//  - Filled 는 체력 바, 쿨타임 원 등에 쓴다 (Fill Amount 0..1)
class UIImage : public UIGraphic
{
public:
	enum class Type { Simple = 0, Sliced = 1, Tiled = 2, Filled = 3 };
	enum class FillMethod { Horizontal = 0, Vertical = 1, Radial90 = 2, Radial180 = 3, Radial360 = 4 };

	UIImage();

	const std::string& GetSprite() const { return m_Sprite; }
	void SetSprite(const std::string& path) { m_Sprite = path; }
	Type GetImageType() const { return m_Type; }
	void SetImageType(Type t) { m_Type = t; }
	float GetFillAmount() const { return m_FillAmount; }
	void SetFillAmount(float v) { m_FillAmount = std::clamp(v, 0.0f, 1.0f); }
	FillMethod GetFillMethod() const { return m_FillMethod; }
	void SetFillMethod(FillMethod m) { m_FillMethod = m; }
	int GetFillOrigin() const { return m_FillOrigin; }
	void SetFillOrigin(int o) { m_FillOrigin = o; }
	bool GetPreserveAspect() const { return m_PreserveAspect; }
	void SetPreserveAspect(bool v) { m_PreserveAspect = v; }
	void SetNativeSize();

	virtual void Populate(UIRenderer& renderer, float canvasScale) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_image"; }

	GENERATE_COMPONENT_BODY(UIImage)

private:
	void PopulateSimple(UIRenderer& r, const Matrix& world, Vec2 mn, Vec2 mx, uint32 color, ID3D11ShaderResourceView* tex);
	void PopulateSliced(UIRenderer& r, const Matrix& world, Vec2 mn, Vec2 mx, uint32 color, ID3D11ShaderResourceView* tex, const Vec4& border, const Vec2& texSize);
	void PopulateFilled(UIRenderer& r, const Matrix& world, Vec2 mn, Vec2 mx, uint32 color, ID3D11ShaderResourceView* tex);

	std::string m_Sprite;
	Type m_Type = Type::Simple;
	bool m_PreserveAspect = false;
	bool m_FillCenter = true;
	FillMethod m_FillMethod = FillMethod::Radial360;
	int m_FillOrigin = 0;
	float m_FillAmount = 1.0f;
	bool m_Clockwise = true;
	float m_PixelsPerUnitMultiplier = 1.0f;
};
REGISTER_COMPONENT(UIImage)
