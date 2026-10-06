#pragma once
#include "Component.h"
#include "SpriteBatch.h"

// Unity 의 Sprite Renderer: 그림 하나를 GameObject 의 XY 평면에 그린다 (2D 게임 · 3D 안의 판).
//  - 크기 = 그림 픽셀 / Pixels Per Unit, 기준점 = Pivot (둘 다 텍스처 가져오기 설정 Sprite (2D and UI))
//  - 내장 스프라이트 "builtin:Square" · "builtin:Circle" 등은 1 × 1 단위
//  - 잘라 놓은 스프라이트 "그림.png#이름" (Sprite Mode = Multiple): 그 사각형 크기 · 기준점 · UV
//  - Color (곱하기), Flip X / Y (기준점을 중심으로 뒤집기), Sorting Layer → Order in Layer (클수록 앞)
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
	int GetSortingLayerId() const { return m_SortingLayerId; }
	void SetSortingLayerId(int id) { m_SortingLayerId = id; }
	// Inspector 의 Sorting Layer (Tags and Layers 목록 + Add Sorting Layer...) · Order in Layer — Line · Trail Renderer 도 쓴다
	static void SortingFields(int& layerId, int& order);
	// 그림 크기 (월드 단위, 크기 조절 전). 그림이 없으면 false
	bool GetSpriteSize(Vec2& size, Vec2& pivot);

	// 스프라이트 경로 → 텍스처 · 원본 픽셀 크기 · Pixels Per Unit · 기준점 · UV · 점 필터 (가져오기 설정을 따른다 — Tilemap 의 타일도 쓴다)
	struct SpriteInfo
	{
		GfxShaderResourceView* Texture = nullptr;
		Vec2 SizePx = Vec2(0, 0);
		float PixelsPerUnit = 100.0f;
		Vec2 Pivot = Vec2(0.5f, 0.5f);
		Vec4 UV = Vec4(0, 0, 1, 1);   // u0, v위, u1, v아래
		bool Point = false;
	};
	static bool ResolveSprite(const std::string& path, SpriteInfo& out);

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
	int m_SortingLayerId = 0;   // Tags and Layers 의 Sorting Layer (0 = Default)

	// 그림 정보 (경로가 바뀌거나 잠깐마다 다시 읽는다 — 가져오기 설정을 고치면 따라온다)
	bool m_Resolved = false;
	unsigned long long m_ResolvedAt = 0;
	GfxShaderResourceView* m_Texture = nullptr;
	Vec2 m_SizePx = Vec2(0, 0);
	float m_PixelsPerUnit = 100.0f;
	Vec2 m_Pivot = Vec2(0.5f, 0.5f);
	Vec4 m_UV = Vec4(0, 0, 1, 1);   // 잘라 놓은 스프라이트면 텍스처의 한 부분
	bool m_Point = false;
	bool Resolve();
	void LocalCorners(Vec3 out[4]);

	GENERATE_COMPONENT_BODY(SpriteRenderer)
};
REGISTER_COMPONENT(SpriteRenderer)
