#pragma once
#include "ShaderGraph.h"
#include <chrono>
#include <map>
#include <memory>

class Effect;

// Shader Graph 창의 미리보기 (Unity 의 노드 미리보기 + Main Preview).
//  - 그래프 전체를 담은 작은 독립 셰이더 (GeneratePreview — 엔진 셰이더를 포함하지 않아 빨리 컴파일).
//    그래프가 바뀌고 잠시 뒤 (끌어 바꾸는 중에는 기다림) 백그라운드에서 컴파일, 그동안은 예전 미리보기
//  - 노드: 첫 출력을 색으로 (UV 사각형) — 칸 96 px 의 아틀라스 한 장, 매 프레임 (Time 이 움직인다)
//  - Main: 픽셀 셰이더가 광선으로 구 / 상자를 그린다 (고정 빛 — 씬 그림자 · 빛과 무관), Yaw / Pitch 로 돌린다
class ShaderGraphPreview
{
public:
	ShaderGraphPreview();
	~ShaderGraphPreview();

	// 매 프레임 (창이 보일 때): revision 이 바뀌면 다시 만들기 예약, 끝난 컴파일 반영, 그리기
	void Update(const ShaderGraph::Graph& g, uint64 revision, bool nodes, bool main);

	// 노드 미리보기 (아직 없으면 nullptr): uv0 / uv1 = 아틀라스 안 칸
	ImTextureID NodeTexture(int nodeId, ImVec2& uv0, ImVec2& uv1) const;
	ImTextureID MainTexture() const;

	const std::string& Error() const { return m_Error; }
	bool Compiling() const { return m_Waiting || m_Dirty; }

	float Yaw = 0.6f, Pitch = 0.35f;
	int Shape = 1;   // 1 구, 2 상자
	static constexpr int kTile = 96;
	static constexpr int kMainSize = 256;

private:
	struct Target
	{
		ComPtr<GfxTexture2D> Color;
		ComPtr<GfxRenderTargetView> Rtv;
		ComPtr<GfxShaderResourceView> Srv;
		int W = 0, H = 0;
	};

	std::unique_ptr<Effect> m_Fx;
	ShaderGraph::Graph m_Graph;       // m_Fx 가 만든 그래프 (속성 기본값 · 노드 그림)
	ShaderGraph::Graph m_Pending;     // 컴파일 중인 그래프
	std::map<int, int> m_Tiles;       // 노드 id → 아틀라스 칸
	std::map<std::string, ComPtr<GfxShaderResourceView>> m_Textures;   // 경로 → 그림
	Target m_Atlas, m_Main;
	uint64 m_Revision = 0;
	bool m_Dirty = false, m_Waiting = false;
	std::chrono::steady_clock::time_point m_ChangeTime;
	std::wstring m_Path;
	std::string m_Error;
	std::string m_LastHlsl;

	bool EnsureTarget(Target& t, int w, int h);
	void Render(bool nodes, bool main);
	GfxShaderResourceView* Texture(const std::string& path);
};
