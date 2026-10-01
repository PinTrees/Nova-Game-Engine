#include "pch.h"
#include "GLState.h"
#include <cfloat>

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

	GLenum GLCompare(D3D11_COMPARISON_FUNC f)
	{
		switch (f)
		{
		case D3D11_COMPARISON_NEVER: return GL_NEVER;
		case D3D11_COMPARISON_LESS: return GL_LESS;
		case D3D11_COMPARISON_EQUAL: return GL_EQUAL;
		case D3D11_COMPARISON_LESS_EQUAL: return GL_LEQUAL;
		case D3D11_COMPARISON_GREATER: return GL_GREATER;
		case D3D11_COMPARISON_NOT_EQUAL: return GL_NOTEQUAL;
		case D3D11_COMPARISON_GREATER_EQUAL: return GL_GEQUAL;
		default: return GL_ALWAYS;
		}
	}

	GLenum GLBlend(D3D11_BLEND b)
	{
		switch (b)
		{
		case D3D11_BLEND_ZERO: return GL_ZERO;
		case D3D11_BLEND_ONE: return GL_ONE;
		case D3D11_BLEND_SRC_COLOR: return GL_SRC_COLOR;
		case D3D11_BLEND_INV_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
		case D3D11_BLEND_SRC_ALPHA: return GL_SRC_ALPHA;
		case D3D11_BLEND_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
		case D3D11_BLEND_DEST_ALPHA: return GL_DST_ALPHA;
		case D3D11_BLEND_INV_DEST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
		case D3D11_BLEND_DEST_COLOR: return GL_DST_COLOR;
		case D3D11_BLEND_INV_DEST_COLOR: return GL_ONE_MINUS_DST_COLOR;
		case D3D11_BLEND_SRC_ALPHA_SAT: return GL_SRC_ALPHA_SATURATE;
		case D3D11_BLEND_BLEND_FACTOR: return GL_CONSTANT_COLOR;
		case D3D11_BLEND_INV_BLEND_FACTOR: return GL_ONE_MINUS_CONSTANT_COLOR;
		case D3D11_BLEND_SRC1_COLOR: return 0x88F9;           // GL_SRC1_COLOR
		case D3D11_BLEND_INV_SRC1_COLOR: return 0x88FA;       // GL_ONE_MINUS_SRC1_COLOR
		case D3D11_BLEND_SRC1_ALPHA: return 0x8589;           // GL_SRC1_ALPHA
		case D3D11_BLEND_INV_SRC1_ALPHA: return 0x88FB;       // GL_ONE_MINUS_SRC1_ALPHA
		default: return GL_ONE;
		}
	}

	GLenum GLBlendOp(D3D11_BLEND_OP o)
	{
		switch (o)
		{
		case D3D11_BLEND_OP_SUBTRACT: return GL_FUNC_SUBTRACT;
		case D3D11_BLEND_OP_REV_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
		case D3D11_BLEND_OP_MIN: return GL_MIN;
		case D3D11_BLEND_OP_MAX: return GL_MAX;
		default: return GL_FUNC_ADD;
		}
	}

	GLenum GLStencilOp(D3D11_STENCIL_OP o)
	{
		switch (o)
		{
		case D3D11_STENCIL_OP_ZERO: return GL_ZERO;
		case D3D11_STENCIL_OP_REPLACE: return GL_REPLACE;
		case D3D11_STENCIL_OP_INCR_SAT: return GL_INCR;
		case D3D11_STENCIL_OP_DECR_SAT: return GL_DECR;
		case D3D11_STENCIL_OP_INVERT: return GL_INVERT;
		case D3D11_STENCIL_OP_INCR: return GL_INCR_WRAP;
		case D3D11_STENCIL_OP_DECR: return GL_DECR_WRAP;
		default: return GL_KEEP;
		}
	}

	GLenum GLAddress(D3D11_TEXTURE_ADDRESS_MODE m)
	{
		switch (m)
		{
		case D3D11_TEXTURE_ADDRESS_WRAP: return GL_REPEAT;
		case D3D11_TEXTURE_ADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
		case D3D11_TEXTURE_ADDRESS_BORDER: return GL_CLAMP_TO_BORDER;
		case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return GL_MIRROR_CLAMP_TO_EDGE;
		default: return GL_CLAMP_TO_EDGE;
		}
	}

	void APIENTRY DebugCallback(GLenum, GLenum, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
	{
		static std::mutex m;
		static std::set<std::string> seen;   // 같은 메시지는 한 번만 (프레임마다 수백 줄이 되지 않게)
		if (severity != GL_DEBUG_SEVERITY_HIGH && severity != GL_DEBUG_SEVERITY_MEDIUM)
			return;
		std::lock_guard<std::mutex> lock(m);
		if (seen.size() < 300 && seen.insert(message).second)
			EditorLog::Write("OpenGL", "%s", message);
	}
}

namespace GLState
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

	D3D11_RASTERIZER_DESC FxRasterizer(const FxParser::StateBlock& b)
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

	D3D11_BLEND_DESC FxBlend(const FxParser::StateBlock& b)
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

	D3D11_DEPTH_STENCIL_DESC FxDepthStencil(const FxParser::StateBlock& b)
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

	D3D11_SAMPLER_DESC FxSampler(const FxParser::StateBlock& b)
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

	void ApplyRasterizer(const D3D11_RASTERIZER_DESC& d)
	{
		glPolygonMode(GL_FRONT_AND_BACK, d.FillMode == D3D11_FILL_WIREFRAME ? GL_LINE : GL_FILL);
		if (d.CullMode == D3D11_CULL_NONE)
			glDisable(GL_CULL_FACE);
		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(d.CullMode == D3D11_CULL_FRONT ? GL_FRONT : GL_BACK);
		}
		glFrontFace(d.FrontCounterClockwise ? GL_CW : GL_CCW);   // 창 y 가 D3D 행 번호라 시계/반시계가 뒤집혀 보인다
		if (d.DepthBias != 0 || d.SlopeScaledDepthBias != 0.0f)
		{
			glEnable(GL_POLYGON_OFFSET_FILL);
			glEnable(GL_POLYGON_OFFSET_LINE);
			glPolygonOffset(d.SlopeScaledDepthBias, (GLfloat)d.DepthBias);
		}
		else
		{
			glDisable(GL_POLYGON_OFFSET_FILL);
			glDisable(GL_POLYGON_OFFSET_LINE);
		}
		if (d.DepthClipEnable) glDisable(GL_DEPTH_CLAMP); else glEnable(GL_DEPTH_CLAMP);
		if (d.ScissorEnable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	}

	void ApplyBlend(const D3D11_BLEND_DESC& d, const float factor[4], UINT sampleMask)
	{
		if (d.AlphaToCoverageEnable) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		for (GLuint i = 0; i < 8; ++i)
		{
			const auto& rt = d.RenderTarget[d.IndependentBlendEnable ? i : 0];
			if (rt.BlendEnable)
			{
				glEnablei(GL_BLEND, i);
				glBlendFuncSeparatei(i, GLBlend(rt.SrcBlend), GLBlend(rt.DestBlend), GLBlend(rt.SrcBlendAlpha), GLBlend(rt.DestBlendAlpha));
				glBlendEquationSeparatei(i, GLBlendOp(rt.BlendOp), GLBlendOp(rt.BlendOpAlpha));
			}
			else
				glDisablei(GL_BLEND, i);
			const UINT8 m = rt.RenderTargetWriteMask;
			glColorMaski(i, (m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0);
		}
		const float one[4] = { 1, 1, 1, 1 };
		const float* f = factor ? factor : one;
		glBlendColor(f[0], f[1], f[2], f[3]);
		glSampleMaski(0, sampleMask);
	}

	void ApplyDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d, UINT ref)
	{
		if (d.DepthEnable)
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GLCompare(d.DepthFunc));
			glDepthMask(d.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? GL_TRUE : GL_FALSE);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);   // D3D: 깊이 끔 = 비교도 쓰기도 없음
			glDepthMask(GL_FALSE);
		}
		if (d.StencilEnable)
		{
			glEnable(GL_STENCIL_TEST);
			glStencilMask(d.StencilWriteMask);
			glStencilFuncSeparate(GL_FRONT, GLCompare(d.FrontFace.StencilFunc), (GLint)ref, d.StencilReadMask);
			glStencilOpSeparate(GL_FRONT, GLStencilOp(d.FrontFace.StencilFailOp), GLStencilOp(d.FrontFace.StencilDepthFailOp), GLStencilOp(d.FrontFace.StencilPassOp));
			glStencilFuncSeparate(GL_BACK, GLCompare(d.BackFace.StencilFunc), (GLint)ref, d.StencilReadMask);
			glStencilOpSeparate(GL_BACK, GLStencilOp(d.BackFace.StencilFailOp), GLStencilOp(d.BackFace.StencilDepthFailOp), GLStencilOp(d.BackFace.StencilPassOp));
		}
		else
			glDisable(GL_STENCIL_TEST);
	}

	GLuint CreateSampler(const D3D11_SAMPLER_DESC& d)
	{
		GLuint s = 0;
		glCreateSamplers(1, &s);
		const UINT f = (UINT)d.Filter;
		const bool aniso = (f & 0x7F) == 0x55;
		const bool minLinear = aniso || (f & 0x10), magLinear = aniso || (f & 0x4), mipLinear = aniso || (f & 0x1);
		glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, minLinear ? (mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : (mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST));
		glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, magLinear ? GL_LINEAR : GL_NEAREST);
		if (aniso) glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY, (GLfloat)(std::max)(1u, d.MaxAnisotropy));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GLAddress(d.AddressU));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GLAddress(d.AddressV));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_R, GLAddress(d.AddressW));
		glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, d.BorderColor);
		glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, d.MipLODBias);
		glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (std::max)(d.MinLOD, -1000.0f));
		glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, (std::min)(d.MaxLOD, 1000.0f));
		if (f & 0x80)
		{
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, GLCompare(d.ComparisonFunc));
		}
		return s;
	}

	void ApplyDefaults()
	{
		const D3D11_RASTERIZER_DESC r = DefaultRasterizer();
		const D3D11_BLEND_DESC b = DefaultBlend();
		const D3D11_DEPTH_STENCIL_DESC ds = DefaultDepthStencil();
		ApplyRasterizer(r);
		ApplyBlend(b, nullptr, 0xFFFFFFFF);
		ApplyDepthStencil(ds, 0);
		glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);   // D3D: nointerpolation 값은 첫 정점
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);          // D3D 큐브 샘플링은 면 경계가 이어진다
		glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);      // D3D: 띠 그리기에서 0xFFFF / 0xFFFFFFFF = 끊기
		glEnable(GL_FRAMEBUFFER_SRGB);                   // sRGB 형식 타깃에 쓸 때만 바꿈 (D3D _SRGB RTV 와 같음)
	}

	Format FromDxgi(DXGI_FORMAT f, bool depthBind)
	{
		Format r;
		auto set = [&](GLenum i, GLenum u, GLenum t, UINT bits) { r.Internal = i; r.Upload = u; r.Type = t; r.Bits = bits; };
		auto bc = [&](GLenum i, UINT block) { r.Internal = i; r.BlockBytes = block; };
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM: set(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: set(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM: set(GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: set(GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_R8G8B8A8_UINT: set(GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 32); r.Integer = true; break;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT: set(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 64); break;
		case DXGI_FORMAT_R16G16B16A16_UNORM: set(GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 64); break;
		case DXGI_FORMAT_R16G16B16A16_UINT: set(GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 64); r.Integer = true; break;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT: set(GL_RGBA32F, GL_RGBA, GL_FLOAT, 128); break;
		case DXGI_FORMAT_R32G32B32A32_UINT: set(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 128); r.Integer = true; break;
		case DXGI_FORMAT_R32G32B32_FLOAT: set(GL_RGB32F, GL_RGB, GL_FLOAT, 96); break;
		case DXGI_FORMAT_R32G32_FLOAT: set(GL_RG32F, GL_RG, GL_FLOAT, 64); break;
		case DXGI_FORMAT_R32G32_UINT: set(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, 64); r.Integer = true; break;
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT: set(GL_RG16F, GL_RG, GL_HALF_FLOAT, 32); break;
		case DXGI_FORMAT_R16G16_UNORM: set(GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 32); break;
		case DXGI_FORMAT_R16_FLOAT: set(GL_R16F, GL_RED, GL_HALF_FLOAT, 16); break;
		case DXGI_FORMAT_R16_UNORM: set(GL_R16, GL_RED, GL_UNSIGNED_SHORT, 16); break;
		case DXGI_FORMAT_R16_UINT: set(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 16); r.Integer = true; break;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM: set(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 16); break;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM: set(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8); break;
		case DXGI_FORMAT_R8_UINT: set(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 8); r.Integer = true; break;
		case DXGI_FORMAT_R11G11B10_FLOAT: set(GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, 32); break;
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: set(0x8C3D /* GL_RGB9_E5 */, GL_RGB, 0x8C3E /* GL_UNSIGNED_INT_5_9_9_9_REV */, 32); break;   // HDR 하늘 큐브맵
		case DXGI_FORMAT_R8G8B8A8_SNORM: set(0x8F97 /* GL_RGBA8_SNORM */, GL_RGBA, GL_BYTE, 32); break;
		case DXGI_FORMAT_R8G8_SNORM: set(0x8F95 /* GL_RG8_SNORM */, GL_RG, GL_BYTE, 16); break;
		case DXGI_FORMAT_R16G16B16A16_SNORM: set(0x8F9B /* GL_RGBA16_SNORM */, GL_RGBA, GL_SHORT, 64); break;
		case DXGI_FORMAT_R32G32_TYPELESS: set(GL_RG32F, GL_RG, GL_FLOAT, 64); break;
		case DXGI_FORMAT_R16G16_SNORM: set(0x8F99 /* GL_RG16_SNORM */, GL_RG, GL_SHORT, 32); break;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM: set(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 32); break;
		case DXGI_FORMAT_R32_UINT: set(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 32); r.Integer = true; break;
		case DXGI_FORMAT_R32_SINT: set(GL_R32I, GL_RED_INTEGER, GL_INT, 32); r.Integer = true; break;
		case DXGI_FORMAT_R32_FLOAT: set(GL_R32F, GL_RED, GL_FLOAT, 32); break;
		case DXGI_FORMAT_R32_TYPELESS:
			if (depthBind) { set(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 32); r.Depth = true; }
			else set(GL_R32F, GL_RED, GL_FLOAT, 32);
			break;
		case DXGI_FORMAT_D32_FLOAT: set(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 32); r.Depth = true; break;
		case DXGI_FORMAT_R16_TYPELESS:
			if (depthBind) { set(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16); r.Depth = true; }
			else set(GL_R16, GL_RED, GL_UNSIGNED_SHORT, 16);
			break;
		case DXGI_FORMAT_D16_UNORM: set(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16); r.Depth = true; break;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: set(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32); r.Depth = r.Stencil = true; break;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: set(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64); r.Depth = r.Stencil = true; break;
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM: bc(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 8); break;
		case DXGI_FORMAT_BC1_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, 8); break;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM: bc(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 16); break;
		case DXGI_FORMAT_BC2_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, 16); break;
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM: bc(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 16); break;
		case DXGI_FORMAT_BC3_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 16); break;
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM: bc(GL_COMPRESSED_RED_RGTC1, 8); break;
		case DXGI_FORMAT_BC4_SNORM: bc(GL_COMPRESSED_SIGNED_RED_RGTC1, 8); break;
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM: bc(GL_COMPRESSED_RG_RGTC2, 16); break;
		case DXGI_FORMAT_BC5_SNORM: bc(GL_COMPRESSED_SIGNED_RG_RGTC2, 16); break;
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16: bc(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, 16); break;
		case DXGI_FORMAT_BC6H_SF16: bc(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT, 16); break;
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM: bc(GL_COMPRESSED_RGBA_BPTC_UNORM, 16); break;
		case DXGI_FORMAT_BC7_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, 16); break;
		default: break;
		}
		return r;
	}

	UINT64 RowBytes(const Format& f, UINT width)
	{
		if (f.BlockBytes) return (UINT64)(std::max)(1u, (width + 3) / 4) * f.BlockBytes;
		return (UINT64)width * f.Bits / 8;
	}

	UINT64 SliceBytes(const Format& f, UINT width, UINT height)
	{
		if (f.BlockBytes) return RowBytes(f, width) * (std::max)(1u, (height + 3) / 4);
		return RowBytes(f, width) * height;
	}

	bool VertexFormat(DXGI_FORMAT f, GLint& size, GLenum& type, GLboolean& normalized, bool& integer, UINT& bytes)
	{
		normalized = GL_FALSE;
		integer = false;
		switch (f)
		{
		case DXGI_FORMAT_R32_FLOAT: size = 1; type = GL_FLOAT; bytes = 4; return true;
		case DXGI_FORMAT_R32G32_FLOAT: size = 2; type = GL_FLOAT; bytes = 8; return true;
		case DXGI_FORMAT_R32G32B32_FLOAT: size = 3; type = GL_FLOAT; bytes = 12; return true;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: size = 4; type = GL_FLOAT; bytes = 16; return true;
		case DXGI_FORMAT_R16G16_FLOAT: size = 2; type = GL_HALF_FLOAT; bytes = 4; return true;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: size = 4; type = GL_HALF_FLOAT; bytes = 8; return true;
		case DXGI_FORMAT_R8G8B8A8_UNORM: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; bytes = 4; return true;
		case DXGI_FORMAT_B8G8R8A8_UNORM: size = GL_BGRA; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; bytes = 4; return true;   // GL 이 B,G,R,A 순서로 읽음
		case DXGI_FORMAT_R8G8B8A8_UINT: size = 4; type = GL_UNSIGNED_BYTE; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R16G16_SINT: size = 2; type = GL_SHORT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32_UINT: size = 1; type = GL_UNSIGNED_INT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32_SINT: size = 1; type = GL_INT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32G32_UINT: size = 2; type = GL_UNSIGNED_INT; integer = true; bytes = 8; return true;
		case DXGI_FORMAT_R32G32B32A32_UINT: size = 4; type = GL_UNSIGNED_INT; integer = true; bytes = 16; return true;
		case DXGI_FORMAT_R32G32B32A32_SINT: size = 4; type = GL_INT; integer = true; bytes = 16; return true;
		case DXGI_FORMAT_R16G16B16A16_UINT: size = 4; type = GL_UNSIGNED_SHORT; integer = true; bytes = 8; return true;
		default: return false;
		}
	}

	GLenum Topology(D3D11_PRIMITIVE_TOPOLOGY t, GLint& patchVertices)
	{
		patchVertices = 0;
		switch (t)
		{
		case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: return GL_POINTS;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: return GL_LINES;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP: return GL_LINE_STRIP;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ: return GL_LINES_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ: return GL_LINE_STRIP_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ: return GL_TRIANGLES_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ: return GL_TRIANGLE_STRIP_ADJACENCY;
		default:
			if (t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST && t <= D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST)
			{
				patchVertices = (GLint)(t - D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
				return GL_PATCHES;
			}
			return GL_TRIANGLES;
		}
	}

	GLenum CompareFunc(D3D11_COMPARISON_FUNC f) { return GLCompare(f); }

	void InstallDebugOutput()
	{
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(DebugCallback, nullptr);
	}
}
