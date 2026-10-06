#pragma once
#include <vector>

// 2D 스프라이트 그리기 (Shaders/51. Sprite.fx): SpriteRenderer 와 패키지의 2D 렌더러 (2D Animator 의 SpriteSkinnedRenderer) 가 쓴다.
//  - 그리는 것은 SpriteSource 를 상속한 컴포넌트. 매 프레임 카메라마다 CollectSprites 로 월드 사각형을 받는다
//  - 순서 = Unity 의 투명 정렬: Sorting Layer (Tags and Layers 의 순서) → Order in Layer 작은 것부터 → 카메라에서 먼 것부터
//    (렌더러 하나 안의 사각형은 넣은 순서 그대로)
//  - 불투명 · 하늘 · 물 다음 (입자와 같은 자리), 깊이 검사만 (쓰지 않음)
//  - 조명: 씬에 켜진 Light 2D 가 없으면 없음 (Sprites-Default), 있으면 2D 빛 · 그림자 · 노멀 맵 (Sprite-Lit-Default — Light2D.h)
class SpriteBatch;

class NOVA_API SpriteSource
{
public:
	SpriteSource();
	virtual ~SpriteSource();
	SpriteSource(const SpriteSource&) = delete;
	SpriteSource& operator=(const SpriteSource&) = delete;

	// 이 렌더러를 가진 GameObject (꺼져 있거나 없으면 그리지 않는다)
	virtual class GameObject* SpriteOwner() const = 0;
	virtual bool SpriteEnabled() const = 0;
	// 보이는 사각형을 넣는다 (batch.Begin 한 번 + Quad 여러 번)
	virtual void CollectSprites(SpriteBatch& batch) = 0;
	// 고르기 · F (Frame Selected) 용 로컬 경계 (GameObject 공간). 없으면 false
	virtual bool SpriteLocalBounds(Vec3& bmin, Vec3& bmax) = 0;

	static const std::vector<SpriteSource*>& All();
	bool ActiveInHierarchy() const;   // 자기 · 부모가 모두 켜졌고 컴포넌트도 켜짐
};

class NOVA_API SpriteBatch
{
public:
	// 렌더러 하나 시작: Sorting Layer (id) → Order in Layer → 카메라 거리를 잴 점 (보통 Transform 위치)
	void Begin(int sortingLayerId, int sortingOrder, const Vec3& pivotWorld);
	// 월드 사각형: 왼쪽 아래 · 오른쪽 아래 · 오른쪽 위 · 왼쪽 위, uv 같은 순서, 색 = R8G8B8A8 (R 이 낮은 바이트), point = 점 필터
	void Quad(const Vec3 p[4], const Vec2 uv[4], uint32 color, GfxShaderResourceView* texture, bool point = false);
	// 이 렌더러의 노멀 맵 (Begin 다음에, 2D 빛이 있을 때만 쓴다). nullptr = 평평
	void SetNormalMap(GfxShaderResourceView* normalMap) { m_Normal = normalMap; }
	// 삼각형 하나 (메시 첨부용)
	void Triangle(const Vec3 p[3], const Vec2 uv[3], const uint32 color[3], GfxShaderResourceView* texture, bool point = false);

	// 모든 SpriteSource 를 이 카메라로 모아 그린다
	static void Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv);
	static int LastDrawCalls();
	static int LastSpriteCount();
	static uint32 PackColor(const float rgba[4]);
	static GfxShaderResourceView* WhiteTexture();

private:
	friend struct SpriteBatchAccess;
	struct Vertex { float X, Y, Z, U, V; uint32 Color; };
	struct Group { int Layer; int Order; float Depth; uint32 Seq; uint32 First, Count; };   // Layer = Sorting Layer 순서, Count = 삼각형 수
	struct Tri { GfxShaderResourceView* Texture; bool Point; GfxShaderResourceView* Normal; };
	GfxShaderResourceView* m_Normal = nullptr;
	std::vector<Vertex> m_Vertices;   // 삼각형마다 정점 3 개
	std::vector<Tri> m_Tris;
	std::vector<Group> m_Groups;
	Matrix m_View;
};
