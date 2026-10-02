#pragma once
#include "Component.h"
#include "SpriteBatch.h"

// Unity 의 Sprite Renderer: 그림 하나를 GameObject 의 XY 평면에 그린다 (2D 게임 · 3D 안의 판).
//  - 크기 = 그림 픽셀 / Pixels Per Unit, 기준점 = Pivot (둘 다 텍스처 가져오기 설정 Sprite (2D and UI))
//  - 내장 스프라이트 "builtin:Square" · "builtin:Circle" 등은 1 × 1 단위
//  - Color (곱하기), Flip X / Y (기준점을 중심으로 뒤집기), Order in Layer (클수록 앞)
//  - 그리기는 SpriteBatch (조명 없음, 투명 정렬)
class NOVA_API SpriteRenderer : public Component, public SpriteSource
{
public:
	SpriteRenderer();

	const std::string& GetSprite() const { return m_Sprite; }
	void SetSprite(const std::string& path) { m_Sprite = path; m_Resolved = false; }
	const float* GetColor() const { return m_Color; }
	void SetColor(const float rgba[4]) { for (int i = 0; i < 4; ++i) m_Color[i] = rgba[i]; }
	bool GetFlipX() const { return m_FlipX; }
	bool GetFlipY() const { return m_FlipY; }
	void SetFlipX(bool v) { m_FlipX = v; }
	void SetFlipY(bool v) { m_FlipY = v; }
	int GetSortingOrder() const { return m_SortingOrder; }
	void SetSortingOrder(int v) { m_SortingOrder = v; }
	// 그림 크기 (월드 단위, 크기 조절 전). 그림이 없으면 false
	bool GetSpriteSize(Vec2& size, Vec2& pivot);

	// SpriteSource
	GameObject* SpriteOwner() const override { return m_pGameObject; }
	bool SpriteEnabled() const override { return m_Enabled; }
	void CollectSprites(SpriteBatch& batch) override;
	bool SpriteLocalBounds(Vec3& bmin, Vec3& bmax) override;

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "sprite_renderer"; }

private:
	std::string m_Sprite;
	float m_Color[4] = { 1, 1, 1, 1 };
	bool m_FlipX = false, m_FlipY = false;
	int m_SortingOrder = 0;

	// 그림 정보 (경로가 바뀌거나 잠깐마다 다시 읽는다 — 가져오기 설정을 고치면 따라온다)
	bool m_Resolved = false;
	unsigned long long m_ResolvedAt = 0;
	GfxShaderResourceView* m_Texture = nullptr;
	Vec2 m_SizePx = Vec2(0, 0);
	float m_PixelsPerUnit = 100.0f;
	Vec2 m_Pivot = Vec2(0.5f, 0.5f);
	bool m_Point = false;
	bool Resolve();
	void LocalCorners(Vec3 out[4]);

	GENERATE_COMPONENT_BODY(SpriteRenderer)
};
REGISTER_COMPONENT(SpriteRenderer)
