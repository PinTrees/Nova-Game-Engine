#include "pch.h"
#include "RenderGraph.h"
#include "Profiler.h"
#include "CliServer.h"
#include <chrono>
#include <map>
#include <unordered_set>

namespace RenderGraph
{
	namespace
	{
		// 임시 텍스처 풀 (모든 그래프가 같이 쓴다 — 한 번에 한 뷰만 실행하니 같은 모양이면 뷰끼리도 다시 쓴다)
		struct PoolEntry
		{
			TextureDesc Desc;
			ComPtr<GfxTexture2D> Tex;
			ComPtr<GfxShaderResourceView> Srv;
			ComPtr<GfxRenderTargetView> Rtv;
			ComPtr<GfxDepthStencilView> Dsv;
			ComPtr<GfxUnorderedAccessView> Uav;
			bool InUse = false;
			uint64 LastFrame = 0;
		};
		std::vector<PoolEntry> s_Pool;
		uint64 s_Frame = 0;
		int s_Created = 0;
		std::map<std::string, nlohmann::json> s_LastInfo;

		// 이름은 프로파일러가 프레임 뒤에도 읽는다 → 오래 사는 문자열로
		const char* Intern(const std::string& name)
		{
			static std::unordered_set<std::string> s_Names;
			return s_Names.insert(name).first->c_str();
		}

		// 깊이 형식 → 텍스처는 TYPELESS, 보기는 DSV · SRV 형식
		void DepthFormats(DXGI_FORMAT f, DXGI_FORMAT& tex, DXGI_FORMAT& dsv, DXGI_FORMAT& srv)
		{
			switch (f)
			{
			case DXGI_FORMAT_D32_FLOAT: tex = DXGI_FORMAT_R32_TYPELESS; dsv = DXGI_FORMAT_D32_FLOAT; srv = DXGI_FORMAT_R32_FLOAT; break;
			case DXGI_FORMAT_D16_UNORM: tex = DXGI_FORMAT_R16_TYPELESS; dsv = DXGI_FORMAT_D16_UNORM; srv = DXGI_FORMAT_R16_UNORM; break;
			default: tex = DXGI_FORMAT_R24G8_TYPELESS; dsv = DXGI_FORMAT_D24_UNORM_S8_UINT; srv = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
			}
		}

		int Acquire(const TextureDesc& desc)
		{
			for (size_t i = 0; i < s_Pool.size(); ++i)
				if (!s_Pool[i].InUse && s_Pool[i].Desc == desc)
				{
					s_Pool[i].InUse = true;
					s_Pool[i].LastFrame = s_Frame;
					return (int)i;
				}
			PoolEntry e;
			e.Desc = desc;
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = (std::max)(desc.Width, 1u);
			d.Height = (std::max)(desc.Height, 1u);
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			DXGI_FORMAT texFormat = desc.Format, dsvFormat = desc.Format, srvFormat = desc.Format;
			if (desc.Depth)
			{
				DepthFormats(desc.Format, texFormat, dsvFormat, srvFormat);
				d.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
			}
			else if (desc.RenderTarget)
				d.BindFlags |= D3D11_BIND_RENDER_TARGET;
			if (desc.UnorderedAccess)
				d.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
			d.Format = texFormat;
			auto device = Gfx::Device();
			if (FAILED(device->CreateTexture2D(&d, nullptr, e.Tex.GetAddressOf())))
				return -1;
			D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
			sd.Format = srvFormat;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(e.Tex.Get(), &sd, e.Srv.GetAddressOf());
			if (desc.Depth)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
				dd.Format = dsvFormat;
				dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				device->CreateDepthStencilView(e.Tex.Get(), &dd, e.Dsv.GetAddressOf());
			}
			else if (desc.RenderTarget)
				device->CreateRenderTargetView(e.Tex.Get(), nullptr, e.Rtv.GetAddressOf());
			if (desc.UnorderedAccess)
				device->CreateUnorderedAccessView(e.Tex.Get(), nullptr, e.Uav.GetAddressOf());
			e.InUse = true;
			e.LastFrame = s_Frame;
			s_Pool.push_back(std::move(e));
			++s_Created;
			return (int)s_Pool.size() - 1;
		}

		std::string FormatName(DXGI_FORMAT f)
		{
			switch (f)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
			case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F";
			case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
			case DXGI_FORMAT_R16G16_FLOAT: return "RG16F";
			case DXGI_FORMAT_R16_FLOAT: return "R16F";
			case DXGI_FORMAT_R32_FLOAT: return "R32F";
			case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
			case DXGI_FORMAT_D32_FLOAT: return "D32F";
			default: return std::to_string((int)f);
			}
		}
	}

	// ---------------------------------------------------------------- Resources
	GfxShaderResourceView* Resources::SRV(Texture t) const
	{
		const auto& r = Owner->m_Resources[t.Id];
		return r.PoolSlot >= 0 ? s_Pool[r.PoolSlot].Srv.Get() : r.Srv;
	}
	GfxRenderTargetView* Resources::RTV(Texture t) const
	{
		const auto& r = Owner->m_Resources[t.Id];
		return r.PoolSlot >= 0 ? s_Pool[r.PoolSlot].Rtv.Get() : r.Rtv;
	}
	GfxDepthStencilView* Resources::DSV(Texture t) const
	{
		const auto& r = Owner->m_Resources[t.Id];
		return r.PoolSlot >= 0 ? s_Pool[r.PoolSlot].Dsv.Get() : r.Dsv;
	}
	GfxUnorderedAccessView* Resources::UAV(Texture t) const
	{
		const auto& r = Owner->m_Resources[t.Id];
		return r.PoolSlot >= 0 ? s_Pool[r.PoolSlot].Uav.Get() : r.Uav;
	}
	GfxTexture2D* Resources::Tex(Texture t) const
	{
		const auto& r = Owner->m_Resources[t.Id];
		return r.PoolSlot >= 0 ? s_Pool[r.PoolSlot].Tex.Get() : r.Tex;
	}
	const TextureDesc& Resources::Desc(Texture t) const { return Owner->m_Resources[t.Id].Desc; }

	// ---------------------------------------------------------------- Builder
	Texture Builder::Read(Texture t)
	{
		if (!t.Valid())
			return t;
		m_Graph->m_Passes[m_Pass].Reads.push_back(t);
		return t;
	}

	Texture Builder::Write(Texture t)
	{
		if (!t.Valid())
			return t;
		auto& r = m_Graph->m_Resources[t.Id];
		const Texture next{ t.Id, ++r.LatestVersion };
		if ((int)r.Producer.size() <= next.Version)
			r.Producer.resize(next.Version + 1, -1);
		r.Producer[next.Version] = m_Pass;
		m_Graph->m_Passes[m_Pass].Writes.push_back(next);
		return next;
	}

	Texture Builder::Create(const char* name, const TextureDesc& desc)
	{
		Graph::ResourceNode r;
		r.Name = name;
		r.Desc = desc;
		r.Producer.assign(1, -1);
		m_Graph->m_Resources.push_back(r);
		const Texture t{ (int)m_Graph->m_Resources.size() - 1, 0 };
		m_Graph->m_Passes[m_Pass].Creates.push_back(t.Id);
		return Write(t);
	}

	void Builder::SideEffect() { m_Graph->m_Passes[m_Pass].SideEffect = true; }
	void Builder::AsyncCompute() { m_Graph->m_Passes[m_Pass].Async = true; }

	// ---------------------------------------------------------------- Graph
	Graph::Graph(const char* name) : m_Name(name) {}

	Texture Graph::Import(const char* name, GfxShaderResourceView* srv, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv, GfxUnorderedAccessView* uav, GfxTexture2D* tex)
	{
		ResourceNode r;
		r.Name = name;
		r.Imported = true;
		r.Srv = srv; r.Rtv = rtv; r.Dsv = dsv; r.Uav = uav; r.Tex = tex;
		r.Producer.assign(1, -1);
		m_Resources.push_back(r);
		return Texture{ (int)m_Resources.size() - 1, 0 };
	}

	void Graph::AddPass(const char* name, const std::function<void(Builder&)>& setup, ExecuteFn execute)
	{
		PassNode p;
		p.Name = name;
		p.Execute = std::move(execute);
		m_Passes.push_back(std::move(p));
		Builder b;
		b.m_Graph = this;
		b.m_Pass = (int)m_Passes.size() - 1;
		if (setup)
			setup(b);
	}

	void Graph::MarkOutput(Texture t)
	{
		if (!t.Valid())
			return;
		m_Resources[t.Id].Output = true;
		m_Resources[t.Id].OutputVersion = t.Version;
	}

	void Graph::Compile()
	{
		// 판마다 읽는 패스 수 (결과 판은 그래프 밖에서 읽는 것으로 하나 더)
		for (ResourceNode& r : m_Resources)
		{
			r.Readers.assign(r.LatestVersion + 1, 0);
			if (r.Producer.size() < r.Readers.size())
				r.Producer.resize(r.Readers.size(), -1);
			if (r.Output && r.OutputVersion >= 0 && r.OutputVersion <= r.LatestVersion)
				++r.Readers[r.OutputVersion];
		}
		for (PassNode& p : m_Passes)
			for (const Texture& t : p.Reads)
				++m_Resources[t.Id].Readers[t.Version];
		// 패스의 참조 = 쓴 판을 읽는 수의 합
		std::vector<int> stack;
		for (size_t i = 0; i < m_Passes.size(); ++i)
		{
			PassNode& p = m_Passes[i];
			p.RefCount = 0;
			for (const Texture& t : p.Writes)
				p.RefCount += m_Resources[t.Id].Readers[t.Version];
			if (p.RefCount == 0 && !p.SideEffect)
				stack.push_back((int)i);
		}
		// 아무도 쓰지 않는 패스를 빼고, 그 패스가 읽던 판의 만든 패스도 차례로
		while (!stack.empty())
		{
			const int pi = stack.back();
			stack.pop_back();
			PassNode& p = m_Passes[pi];
			if (p.Culled)
				continue;
			p.Culled = true;
			for (const Texture& t : p.Reads)
			{
				ResourceNode& r = m_Resources[t.Id];
				if (--r.Readers[t.Version] > 0)
					continue;
				const int producer = r.Producer[t.Version];
				if (producer < 0)
					continue;
				PassNode& q = m_Passes[producer];
				if (--q.RefCount <= 0 && !q.SideEffect && !q.Culled)
					stack.push_back(producer);
			}
		}
		// 임시 텍스처의 처음 · 마지막 쓰임 (남은 패스 기준)
		for (size_t i = 0; i < m_Passes.size(); ++i)
		{
			const PassNode& p = m_Passes[i];
			if (p.Culled)
				continue;
			auto touch = [&](int id) {
				ResourceNode& r = m_Resources[id];
				if (r.Imported)
					return;
				if (r.FirstPass < 0)
					r.FirstPass = (int)i;
				r.LastPass = (int)i;
			};
			for (const Texture& t : p.Reads) touch(t.Id);
			for (const Texture& t : p.Writes) touch(t.Id);
		}
		m_Compiled = true;
	}

	void Graph::Execute()
	{
		if (!m_Compiled)
			Compile();
		Resources res;
		res.Owner = this;
		Profiler::Phases phase;
		for (size_t i = 0; i < m_Passes.size(); ++i)
		{
			PassNode& p = m_Passes[i];
			if (p.Culled)
				continue;
			for (ResourceNode& r : m_Resources)
				if (!r.Imported && r.FirstPass == (int)i)
					r.PoolSlot = Acquire(r.Desc);
			phase.Next(Intern(p.Name));
			const auto t0 = std::chrono::steady_clock::now();
			if (p.Execute)
				p.Execute(res);
			p.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			for (ResourceNode& r : m_Resources)
				if (!r.Imported && r.LastPass == (int)i && r.PoolSlot >= 0)
					s_Pool[r.PoolSlot].InUse = false;   // 뒤 패스가 같은 모양으로 다시 받는다
		}
		phase.Close();
		for (ResourceNode& r : m_Resources)
			if (r.PoolSlot >= 0)
			{
				s_Pool[r.PoolSlot].InUse = false;
				r.PoolSlot = -1;
			}
		Publish(*this);
	}

	nlohmann::json Graph::Info() const
	{
		auto label = [this](const Texture& t) { return m_Resources[t.Id].Name + "@" + std::to_string(t.Version); };
		nlohmann::json passes = nlohmann::json::array(), resources = nlohmann::json::array();
		int culled = 0;
		for (const PassNode& p : m_Passes)
		{
			nlohmann::json reads = nlohmann::json::array(), writes = nlohmann::json::array();
			for (const Texture& t : p.Reads) reads.push_back(label(t));
			for (const Texture& t : p.Writes) writes.push_back(label(t));
			passes.push_back({ { "name", p.Name }, { "culled", p.Culled }, { "sideEffect", p.SideEffect }, { "async", p.Async },
				{ "reads", reads }, { "writes", writes }, { "cpuMs", p.Ms } });
			culled += p.Culled ? 1 : 0;
		}
		int transient = 0;
		for (const ResourceNode& r : m_Resources)
		{
			nlohmann::json j = { { "name", r.Name }, { "imported", r.Imported }, { "versions", r.LatestVersion }, { "output", r.Output } };
			if (!r.Imported)
			{
				++transient;
				j["size"] = { r.Desc.Width, r.Desc.Height };
				j["format"] = FormatName(r.Desc.Format);
				j["firstPass"] = r.FirstPass >= 0 ? m_Passes[r.FirstPass].Name : std::string();
				j["lastPass"] = r.LastPass >= 0 ? m_Passes[r.LastPass].Name : std::string();
			}
			resources.push_back(j);
		}
		return { { "name", m_Name }, { "passes", passes }, { "resources", resources }, { "passCount", (int)m_Passes.size() },
			{ "culledCount", culled }, { "transientCount", transient } };
	}

	void Publish(const Graph& graph)
	{
		s_LastInfo[graph.Name()] = graph.Info();
	}

	nlohmann::json LastInfo(const std::string& name)
	{
		auto it = s_LastInfo.find(name);
		return it != s_LastInfo.end() ? it->second : nlohmann::json();
	}

	std::vector<std::string> GraphNames()
	{
		std::vector<std::string> out;
		for (auto& kv : s_LastInfo)
			out.push_back(kv.first);
		return out;
	}

	nlohmann::json PoolInfo()
	{
		nlohmann::json list = nlohmann::json::array();
		uint64 bytes = 0;
		for (const PoolEntry& e : s_Pool)
		{
			list.push_back({ { "size", { e.Desc.Width, e.Desc.Height } }, { "format", FormatName(e.Desc.Format) }, { "inUse", e.InUse },
				{ "idleFrames", s_Frame - e.LastFrame } });
			const uint64 bpp = e.Desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : e.Desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16 : 4;
			bytes += (uint64)e.Desc.Width * e.Desc.Height * bpp;
		}
		return { { "textures", list }, { "count", (int)s_Pool.size() }, { "created", s_Created }, { "megabytes", (double)bytes / (1024.0 * 1024.0) } };
	}

	void TrimPool(int unusedFrames)
	{
		++s_Frame;
		s_Pool.erase(std::remove_if(s_Pool.begin(), s_Pool.end(), [unusedFrames](const PoolEntry& e) {
			return !e.InUse && s_Frame - e.LastFrame > (uint64)unusedFrames;
		}), s_Pool.end());
	}

	void RegisterEditor()
	{
		CliServer::Register("rendergraph", "Render Graph: {op: info, view?: Game|Scene} — passes (reads, writes, culled), transient textures, pool",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string view = args.value("view", args.value("path", std::string()));
				if (!view.empty())
				{
					result = LastInfo(view);
					if (result.is_null())
					{
						error = "no graph named '" + view + "' yet (views: Game, Scene)";
						return false;
					}
				}
				else
				{
					result = nlohmann::json::object();
					for (const std::string& n : GraphNames())
						result[n] = LastInfo(n);
				}
				result["pool"] = PoolInfo();
				return true;
			});
	}
}
