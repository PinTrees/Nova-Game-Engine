#include "pch.h"
#include "FxStates.h"
#include <cfloat>

// .fx 상태 블록 → D3D11 설명 (FxStates.h). 예전 GLState.cpp 에 있던 것을 API 공용으로 옮김
namespace
{
	std::string Upper(std::string s)
	{
		for (char& c : s) c = (char)toupper((unsigned char)c);
		s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
		return s;
	}

	// "blendenable[0]" 또는 "blendenable"
	const std::string* Field(const FxParser::StateBlock& b, const std::string& key, int index = 0)
	{
		auto it = b.Fields.find(key + "[" + std::to_string(index) + "]");
		if (it != b.Fields.end()) return &it->second;
		if (index == 0)
		{
			it = b.Fields.find(key);
			if (it != b.Fields.end()) return &it->second;
		}
		return nullptr;
	}

	bool IsTrue(const std::string& v) { const std::string u = Upper(v); return u == "TRUE" || u == "1"; }

	double Number(const std::string& v) { return strtod(v.c_str(), nullptr); }
	UINT Hex(const std::string& v) { return (UINT)strtoul(v.c_str(), nullptr, 0); }

	std::vector<double> Numbers(const std::string& text)
	{
		std::vector<double> out;
		const char* p = text.c_str();
		while (*p)
		{
			if ((*p >= '0' && *p <= '9') || ((*p == '-' || *p == '.') && (isdigit((unsigned char)p[1]) || p[1] == '.')))
			{
				if (p > text.c_str() && (isalpha((unsigned char)p[-1]) || p[-1] == '_')) { ++p; continue; }
				char* end = nullptr;
				out.push_back(strtod(p, &end));
				p = end;
				while (*p == 'f' || *p == 'F') ++p;
				continue;
			}
			++p;
		}
		return out;
	}

	D3D11_COMPARISON_FUNC FxCompare(const std::string& v, D3D11_COMPARISON_FUNC def)
	{
		const std::string u = Upper(v);
		if (u.find("LESS_EQUAL") != std::string::npos) return D3D11_COMPARISON_LESS_EQUAL;
		if (u.find("GREATER_EQUAL") != std::string::npos) return D3D11_COMPARISON_GREATER_EQUAL;
		if (u.find("NOT_EQUAL") != std::string::npos) return D3D11_COMPARISON_NOT_EQUAL;
		if (u.find("LESS") != std::string::npos) return D3D11_COMPARISON_LESS;
		if (u.find("GREATER") != std::string::npos) return D3D11_COMPARISON_GREATER;
		if (u.find("EQUAL") != std::string::npos) return D3D11_COMPARISON_EQUAL;
		if (u.find("ALWAYS") != std::string::npos) return D3D11_COMPARISON_ALWAYS;
		if (u.find("NEVER") != std::string::npos) return D3D11_COMPARISON_NEVER;
		return def;
	}

	D3D11_TEXTURE_ADDRESS_MODE FxAddress(const std::string& v)
	{
		const std::string u = Upper(v);
		if (u.find("MIRROR_ONCE") != std::string::npos) return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
		if (u.find("MIRROR") != std::string::npos) return D3D11_TEXTURE_ADDRESS_MIRROR;
		if (u.find("WRAP") != std::string::npos) return D3D11_TEXTURE_ADDRESS_WRAP;
		if (u.find("BORDER") != std::string::npos) return D3D11_TEXTURE_ADDRESS_BORDER;
		return D3D11_TEXTURE_ADDRESS_CLAMP;
	}

	D3D11_BLEND FxBlendFactor(const std::string& v, D3D11_BLEND def)
	{
		std::string u = Upper(v);
		if (u.rfind("D3D11_BLEND_", 0) == 0) u = u.substr(12);
		static const std::pair<const char*, D3D11_BLEND> map[] = {
			{ "ZERO", D3D11_BLEND_ZERO }, { "ONE", D3D11_BLEND_ONE }, { "SRC_COLOR", D3D11_BLEND_SRC_COLOR }, { "INV_SRC_COLOR", D3D11_BLEND_INV_SRC_COLOR },
			{ "SRC_ALPHA", D3D11_BLEND_SRC_ALPHA }, { "INV_SRC_ALPHA", D3D11_BLEND_INV_SRC_ALPHA }, { "DEST_ALPHA", D3D11_BLEND_DEST_ALPHA },
			{ "INV_DEST_ALPHA", D3D11_BLEND_INV_DEST_ALPHA }, { "DEST_COLOR", D3D11_BLEND_DEST_COLOR }, { "INV_DEST_COLOR", D3D11_BLEND_INV_DEST_COLOR },
			{ "SRC_ALPHA_SAT", D3D11_BLEND_SRC_ALPHA_SAT }, { "BLEND_FACTOR", D3D11_BLEND_BLEND_FACTOR }, { "INV_BLEND_FACTOR", D3D11_BLEND_INV_BLEND_FACTOR },
			{ "SRC1_COLOR", D3D11_BLEND_SRC1_COLOR }, { "INV_SRC1_COLOR", D3D11_BLEND_INV_SRC1_COLOR }, { "SRC1_ALPHA", D3D11_BLEND_SRC1_ALPHA },
			{ "INV_SRC1_ALPHA", D3D11_BLEND_INV_SRC1_ALPHA } };
		for (const auto& [n, b] : map)
			if (u == n) return b;
		return def;
	}

	D3D11_BLEND_OP FxBlendOp(const std::string& v)
	{
		const std::string u = Upper(v);
		if (u.find("REV_SUBTRACT") != std::string::npos) return D3D11_BLEND_OP_REV_SUBTRACT;
		if (u.find("SUBTRACT") != std::string::npos) return D3D11_BLEND_OP_SUBTRACT;
		if (u.find("MIN") != std::string::npos) return D3D11_BLEND_OP_MIN;
		if (u.find("MAX") != std::string::npos) return D3D11_BLEND_OP_MAX;
		return D3D11_BLEND_OP_ADD;
	}

	D3D11_STENCIL_OP FxStencilOp(const std::string& v)
	{
		const std::string u = Upper(v);
		if (u.find("INCR_SAT") != std::string::npos) return D3D11_STENCIL_OP_INCR_SAT;
		if (u.find("DECR_SAT") != std::string::npos) return D3D11_STENCIL_OP_DECR_SAT;
		if (u.find("INCR") != std::string::npos) return D3D11_STENCIL_OP_INCR;
		if (u.find("DECR") != std::string::npos) return D3D11_STENCIL_OP_DECR;
		if (u.find("REPLACE") != std::string::npos) return D3D11_STENCIL_OP_REPLACE;
		if (u.find("INVERT") != std::string::npos) return D3D11_STENCIL_OP_INVERT;
		if (u.find("ZERO") != std::string::npos) return D3D11_STENCIL_OP_ZERO;
		return D3D11_STENCIL_OP_KEEP;
	}

	// MIN_MAG_LINEAR_MIP_POINT 같은 이름 → D3D11_FILTER 비트 (MIP 1, MAG 4, MIN 0x10, 비교 0x80, 비등방 0x55)
	D3D11_FILTER FxFilter(const std::string& v)
	{
		std::string u = Upper(v);
		UINT bits = 0;
		if (u.rfind("COMPARISON_", 0) == 0) { bits |= 0x80; u = u.substr(11); }
		if (u.find("ANISOTROPIC") != std::string::npos)
			return (D3D11_FILTER)(bits | 0x55);
		std::vector<std::string> pending;
		size_t p = 0;
		while (p <= u.size())
		{
			size_t e = u.find('_', p);
			if (e == std::string::npos) e = u.size();
			const std::string tok = u.substr(p, e - p);
			if (tok == "MIN" || tok == "MAG" || tok == "MIP") pending.push_back(tok);
			else if (tok == "LINEAR" || tok == "POINT")
			{
				for (const std::string& n : pending)
					if (tok == "LINEAR")
						bits |= n == "MIN" ? 0x10 : n == "MAG" ? 0x4 : 0x1;
				pending.clear();
			}
			p = e + 1;
		}
		return (D3D11_FILTER)bits;
	}
}

namespace FxStates
{
	D3D11_RASTERIZER_DESC DefaultRasterizer()
	{
		D3D11_RASTERIZER_DESC d = {};
		d.FillMode = D3D11_FILL_SOLID;
		d.CullMode = D3D11_CULL_BACK;
		d.DepthClipEnable = TRUE;
		return d;
	}

	D3D11_BLEND_DESC DefaultBlend()
	{
		D3D11_BLEND_DESC d = {};
		for (auto& rt : d.RenderTarget)
		{
			rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
			rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		return d;
	}

	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil()
	{
		D3D11_DEPTH_STENCIL_DESC d = {};
		d.DepthEnable = TRUE;
		d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		d.DepthFunc = D3D11_COMPARISON_LESS;
		d.StencilReadMask = d.StencilWriteMask = 0xFF;
		d.FrontFace = d.BackFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
		return d;
	}

	D3D11_SAMPLER_DESC DefaultSampler()
	{
		D3D11_SAMPLER_DESC d = {};
		d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		d.MaxAnisotropy = 1;
		d.ComparisonFunc = D3D11_COMPARISON_NEVER;
		d.BorderColor[0] = d.BorderColor[1] = d.BorderColor[2] = d.BorderColor[3] = 1.0f;
		d.MinLOD = -FLT_MAX;
		d.MaxLOD = FLT_MAX;
		return d;
	}

	D3D11_RASTERIZER_DESC Rasterizer(const FxParser::StateBlock& b)
	{
		D3D11_RASTERIZER_DESC d = DefaultRasterizer();
		if (auto* v = Field(b, "fillmode")) d.FillMode = Upper(*v).find("WIREFRAME") != std::string::npos ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
		if (auto* v = Field(b, "cullmode"))
		{
			const std::string u = Upper(*v);
			d.CullMode = u.find("NONE") != std::string::npos ? D3D11_CULL_NONE : u.find("FRONT") != std::string::npos ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
		}
		if (auto* v = Field(b, "frontcounterclockwise")) d.FrontCounterClockwise = IsTrue(*v);
		if (auto* v = Field(b, "depthbias")) d.DepthBias = (INT)Number(*v);
		if (auto* v = Field(b, "depthbiasclamp")) d.DepthBiasClamp = (FLOAT)Number(*v);
		if (auto* v = Field(b, "slopescaleddepthbias")) d.SlopeScaledDepthBias = (FLOAT)Number(*v);
		if (auto* v = Field(b, "depthclipenable")) d.DepthClipEnable = IsTrue(*v);
		if (auto* v = Field(b, "scissorenable")) d.ScissorEnable = IsTrue(*v);
		if (auto* v = Field(b, "multisampleenable")) d.MultisampleEnable = IsTrue(*v);
		if (auto* v = Field(b, "antialiasedlineenable")) d.AntialiasedLineEnable = IsTrue(*v);
		return d;
	}

	D3D11_BLEND_DESC Blend(const FxParser::StateBlock& b)
	{
		D3D11_BLEND_DESC d = DefaultBlend();
		if (auto* v = Field(b, "alphatocoverageenable")) d.AlphaToCoverageEnable = IsTrue(*v);
		if (auto* v = Field(b, "independentblendenable")) d.IndependentBlendEnable = IsTrue(*v);
		// Effects11: SrcBlend 같은 배열 아닌 값은 모든 타깃에, BlendEnable[i]·RenderTargetWriteMask[i] 는 타깃마다
		for (int i = 0; i < 8; ++i)
		{
			auto& rt = d.RenderTarget[i];
			auto get = [&](const char* key) { const std::string* v = Field(b, key, i); return v ? v : Field(b, key, 0); };
			if (auto* v = Field(b, "blendenable", i)) rt.BlendEnable = IsTrue(*v);
			if (auto* v = get("srcblend")) rt.SrcBlend = FxBlendFactor(*v, rt.SrcBlend);
			if (auto* v = get("destblend")) rt.DestBlend = FxBlendFactor(*v, rt.DestBlend);
			if (auto* v = get("blendop")) rt.BlendOp = FxBlendOp(*v);
			if (auto* v = get("srcblendalpha")) rt.SrcBlendAlpha = FxBlendFactor(*v, rt.SrcBlendAlpha);
			if (auto* v = get("destblendalpha")) rt.DestBlendAlpha = FxBlendFactor(*v, rt.DestBlendAlpha);
			if (auto* v = get("blendopalpha")) rt.BlendOpAlpha = FxBlendOp(*v);
			if (auto* v = Field(b, "rendertargetwritemask", i)) rt.RenderTargetWriteMask = (UINT8)Hex(*v);
		}
		return d;
	}

	D3D11_DEPTH_STENCIL_DESC DepthStencil(const FxParser::StateBlock& b)
	{
		D3D11_DEPTH_STENCIL_DESC d = DefaultDepthStencil();
		if (auto* v = Field(b, "depthenable")) d.DepthEnable = IsTrue(*v);
		if (auto* v = Field(b, "depthwritemask")) d.DepthWriteMask = Upper(*v).find("ZERO") != std::string::npos ? D3D11_DEPTH_WRITE_MASK_ZERO : D3D11_DEPTH_WRITE_MASK_ALL;
		if (auto* v = Field(b, "depthfunc")) d.DepthFunc = FxCompare(*v, D3D11_COMPARISON_LESS);
		if (auto* v = Field(b, "stencilenable")) d.StencilEnable = IsTrue(*v);
		if (auto* v = Field(b, "stencilreadmask")) d.StencilReadMask = (UINT8)Hex(*v);
		if (auto* v = Field(b, "stencilwritemask")) d.StencilWriteMask = (UINT8)Hex(*v);
		auto face = [&](const char* prefix, D3D11_DEPTH_STENCILOP_DESC& f) {
			const std::string p = prefix;
			if (auto* v = Field(b, p + "stencilfunc")) f.StencilFunc = FxCompare(*v, D3D11_COMPARISON_ALWAYS);
			if (auto* v = Field(b, p + "stencilfailop")) f.StencilFailOp = FxStencilOp(*v);
			if (auto* v = Field(b, p + "stencildepthfailop")) f.StencilDepthFailOp = FxStencilOp(*v);
			if (auto* v = Field(b, p + "stencilpassop")) f.StencilPassOp = FxStencilOp(*v);
		};
		face("frontface", d.FrontFace);
		face("backface", d.BackFace);
		return d;
	}

	D3D11_SAMPLER_DESC Sampler(const FxParser::StateBlock& b)
	{
		D3D11_SAMPLER_DESC d = DefaultSampler();
		if (auto* v = Field(b, "filter")) d.Filter = FxFilter(*v);
		if (auto* v = Field(b, "addressu")) d.AddressU = FxAddress(*v);
		if (auto* v = Field(b, "addressv")) d.AddressV = FxAddress(*v);
		if (auto* v = Field(b, "addressw")) d.AddressW = FxAddress(*v);
		if (auto* v = Field(b, "miplodbias")) d.MipLODBias = (FLOAT)Number(*v);
		if (auto* v = Field(b, "maxanisotropy")) d.MaxAnisotropy = (UINT)Number(*v);
		if (auto* v = Field(b, "comparisonfunc")) d.ComparisonFunc = FxCompare(*v, D3D11_COMPARISON_NEVER);
		if (auto* v = Field(b, "bordercolor"))
		{
			const auto n = Numbers(*v);
			for (size_t i = 0; i < 4 && i < n.size(); ++i) d.BorderColor[i] = (FLOAT)n[i];
		}
		if (auto* v = Field(b, "minlod")) d.MinLOD = (FLOAT)Number(*v);
		if (auto* v = Field(b, "maxlod")) d.MaxLOD = (FLOAT)Number(*v);
		return d;
	}
}
