#pragma once
#include <memory>
#include <vector>

class Mesh;
class UMaterial;

// 메시 미리 보기 렌더 (편집기 창 · 패키지): 렌더 타깃에 메시들을 해 · 하늘 빛으로 그린다 (그림자 · 안개 없이).
//  카메라는 상자 (min · max) 를 화면에 맞춘다 — yaw · pitch (도), zoom (1 = 꼭 맞게). 건축물 생성기 · 썸네일이 쓴다
namespace MeshPreview
{
	struct NOVA_API Target
	{
		int W = 0, H = 0;
		ComPtr<GfxTexture2D> Color, Depth;
		ComPtr<GfxRenderTargetView> Rtv;
		ComPtr<GfxShaderResourceView> Srv;
		ComPtr<GfxDepthStencilView> Dsv;
		bool Ensure(int w, int h);   // 크기가 바뀌면 다시 만든다
	};

	struct Item
	{
		std::shared_ptr<Mesh> Model;
		std::vector<std::shared_ptr<UMaterial>> Materials;   // 서브셋의 MaterialIndex 칸 (없으면 기본 재질)
		XMFLOAT4X4 World;
	};

	struct View
	{
		XMFLOAT3 Min = { -1, -1, -1 }, Max = { 1, 1, 1 };
		float Yaw = -32.0f, Pitch = 18.0f, Zoom = 1.0f;
		float Fov = 32.0f;
		float Clear[4] = { 0.5f, 0.6f, 0.7f, 1.0f };
	};

	NOVA_API void Render(Target& target, const std::vector<Item>& items, const View& view);
	NOVA_API XMFLOAT4X4 Identity();
}
