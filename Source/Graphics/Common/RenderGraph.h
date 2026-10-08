#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Render Graph (Unity URP 의 Render Graph · Frostbite Frame Graph 와 같은 생각): 한 뷰의 그리기를 패스 노드로 적는다.
//  1) 쌓기 (Setup): 패스마다 읽는 · 쓰는 텍스처를 선언 (쓰면 그 텍스처의 새 판 — 같은 텍스처를 여러 패스가 이어 써도 순서가 맞다)
//  2) 정리 (Compile): 결과 (출력 · 다음 프레임 히스토리 = Side Effect) 에 닿지 않는 패스를 뺀다 (예: 아무도 읽지 않는 모션 벡터),
//     임시 텍스처의 처음 · 마지막 쓰임 → 같은 모양의 텍스처를 앞 패스가 다 쓴 뒤 뒤 패스가 다시 쓴다 (풀)
//  3) 실행 (Execute): 남은 패스를 차례로 — 임시 텍스처는 처음 쓸 때 풀에서 받고 마지막 뒤 돌려준다
//  비동기 컴퓨트 (AsyncCompute 표시) 는 큐가 둘인 백엔드 (DirectX 12 · Vulkan) 에서 다른 큐로 — 다른 백엔드는 같은 자리에서 그대로
namespace RenderGraph
{
	struct TextureDesc
	{
		UINT Width = 0, Height = 0;
		DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bool RenderTarget = true;     // RTV
		bool Depth = false;           // DSV (Format = D24 · D32 계열)
		bool UnorderedAccess = false; // UAV (컴퓨트)
		bool operator==(const TextureDesc& o) const
		{
			return Width == o.Width && Height == o.Height && Format == o.Format && RenderTarget == o.RenderTarget && Depth == o.Depth && UnorderedAccess == o.UnorderedAccess;
		}
	};

	// 텍스처 손잡이: 자원 번호 + 판 (쓸 때마다 하나 늘어난 손잡이를 돌려받는다)
	struct Texture
	{
		int Id = -1;
		int Version = 0;
		bool Valid() const { return Id >= 0; }
	};

	// 실행 중 패스가 실제 GPU 객체를 얻는 곳
	struct Resources
	{
		GfxShaderResourceView* SRV(Texture t) const;
		GfxRenderTargetView* RTV(Texture t) const;
		GfxDepthStencilView* DSV(Texture t) const;
		GfxUnorderedAccessView* UAV(Texture t) const;
		GfxTexture2D* Tex(Texture t) const;
		const TextureDesc& Desc(Texture t) const;
		class Graph* Owner = nullptr;
	};

	class Graph;
	class Builder
	{
	public:
		Texture Read(Texture t);
		Texture Write(Texture t);              // 새 판을 돌려준다
		Texture Create(const char* name, const TextureDesc& desc);   // 이 패스가 처음 쓰는 임시 텍스처 (Write 포함)
		void SideEffect();                     // 결과가 그래프 밖으로 나간다 (빼지 않는다)
		void AsyncCompute();                   // 컴퓨트 패스 — 큐가 둘인 백엔드에서 다른 큐로
	private:
		friend class Graph;
		Graph* m_Graph = nullptr;
		int m_Pass = -1;
	};

	using ExecuteFn = std::function<void(const Resources&)>;

	class Graph
	{
	public:
		explicit Graph(const char* name);

		// 그래프 밖의 텍스처 (뷰 타깃 · 모듈이 가진 타깃 · 히스토리). 읽기만 하면 nullptr 인 뷰는 그냥 nullptr
		Texture Import(const char* name, GfxShaderResourceView* srv, GfxRenderTargetView* rtv = nullptr, GfxDepthStencilView* dsv = nullptr,
			GfxUnorderedAccessView* uav = nullptr, GfxTexture2D* tex = nullptr);
		// setup(builder) 에서 읽기 · 쓰기를 선언하고, 실행은 execute(resources)
		void AddPass(const char* name, const std::function<void(Builder&)>& setup, ExecuteFn execute);
		// 결과로 표시 (이 텍스처의 마지막 판을 만드는 패스는 빼지 않는다)
		void MarkOutput(Texture t);

		void Compile();
		void Execute();

		nlohmann::json Info() const;   // 패스 (읽기 · 쓰기 · 빠짐 · 시간) · 자원 (수명 · 풀)
		const std::string& Name() const { return m_Name; }

	private:
		friend class Builder;
		friend struct Resources;
		struct ResourceNode
		{
			std::string Name;
			bool Imported = false;
			TextureDesc Desc;
			GfxShaderResourceView* Srv = nullptr;
			GfxRenderTargetView* Rtv = nullptr;
			GfxDepthStencilView* Dsv = nullptr;
			GfxUnorderedAccessView* Uav = nullptr;
			GfxTexture2D* Tex = nullptr;
			int LatestVersion = 0;
			bool Output = false;
			int OutputVersion = -1;
			// 임시: 풀에서 받은 칸, 처음 · 마지막 패스
			int PoolSlot = -1;
			int FirstPass = -1, LastPass = -1;
			std::vector<int> Producer;   // 판마다 그 판을 쓴 패스 (판 0 = 그래프 밖 · 없음 → -1)
			std::vector<int> Readers;    // 판마다 읽는 패스 수 (정리할 때)
		};
		struct PassNode
		{
			std::string Name;
			std::vector<Texture> Reads, Writes;
			std::vector<int> Creates;
			bool SideEffect = false, Async = false, Culled = false;
			int RefCount = 0;
			ExecuteFn Execute;
			double Ms = 0.0;
		};
		std::string m_Name;
		std::vector<ResourceNode> m_Resources;
		std::vector<PassNode> m_Passes;
		bool m_Compiled = false;
	};

	// 뷰마다 마지막으로 실행한 그래프 정보 (CLI · Render Graph Viewer)
	void Publish(const Graph& graph);
	nlohmann::json LastInfo(const std::string& name);
	std::vector<std::string> GraphNames();
	nlohmann::json PoolInfo();
	void TrimPool(int unusedFrames = 120);   // 오래 쓰이지 않은 임시 텍스처를 놓는다 (프레임마다 한 번)
	void RegisterEditor();   // CLI: nova rendergraph info [--view Game|Scene]
}
