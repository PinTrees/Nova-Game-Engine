#pragma once

// UGUI 그리기: 사각형/삼각형을 텍스처별로 모아 한 번에 그린다 (Shaders/42. UI.fx).
// 좌표는 캔버스 월드(Game 뷰 화면 픽셀, y 위쪽)이며, Flush 할 때 뷰-투영 행렬로 변환한다.
class UIRenderer
{
public:
	struct Vertex
	{
		float X, Y, Z;
		float U, V;
		uint32 Color;   // R8G8B8A8 (감마 공간)
	};

	static UIRenderer& Get();

	void Begin();
	// Mask / RectMask2D: 이후 그리는 것을 캔버스 월드 사각형(minX, minY, maxX, maxY) 안으로 자른다 (enabled = false 면 풀기)
	void SetClip(bool enabled, const Vec4& worldRect = Vec4(0, 0, 0, 0));
	// 네 점(왼쪽 아래, 왼쪽 위, 오른쪽 위, 오른쪽 아래 — Unity GetWorldCorners 순서)
	void AddQuad(const Vec3 p[4], const Vec2 uv[4], uint32 color, ID3D11ShaderResourceView* texture);
	void AddTriangle(const Vec3 p[3], const Vec2 uv[3], uint32 color, ID3D11ShaderResourceView* texture);
	// 선 (선택 테두리 등): 굵기는 월드 단위
	void AddLine(const Vec3& a, const Vec3& b, float thickness, uint32 color);
	// 모은 것을 그린다 (rtv 에 viewport w x h). dsv 가 있으면 그 깊이로 가려지게 (Scene 뷰)
	void Flush(ID3D11RenderTargetView* rtv, UINT width, UINT height, const Matrix& viewProj, ID3D11DepthStencilView* dsv = nullptr);

	ID3D11ShaderResourceView* WhiteTexture();
	static uint32 PackColor(const float rgba[4]);
	int LastDrawCalls() const { return m_LastDrawCalls; }

private:
	UIRenderer() = default;
	bool Init();
	void Reserve(ID3D11ShaderResourceView* texture);

	struct Command
	{
		ID3D11ShaderResourceView* Texture;
		UINT IndexStart;
		UINT IndexCount;
		bool Clip;
		Vec4 ClipRect;
	};
	bool m_ClipOn = false;
	Vec4 m_ClipRect = Vec4(0, 0, 0, 0);
	std::vector<Vertex> m_Vertices;
	std::vector<uint32> m_Indices;
	std::vector<Command> m_Commands;

	std::unique_ptr<class Effect> m_Effect;
	bool m_Failed = false;
	ComPtr<ID3D11InputLayout> m_Layout;
	ComPtr<ID3D11Buffer> m_VB, m_IB;
	UINT m_VBCapacity = 0, m_IBCapacity = 0;
	ComPtr<ID3D11ShaderResourceView> m_White;
	int m_LastDrawCalls = 0;
};
