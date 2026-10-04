#include "pch.h"
#include "GfxGLES.h"
#include "GLESState.h"
#include "FxStates.h"
#include "GLShared.h"
#include "Rhi.h"
#include <DirectXTex/DirectXTex.h>
#include <EGL/egl.h>

// Gfx(D3D11 모양) 층의 OpenGL ES 3.2 구현 — 데스크톱 GfxGL.cpp 를 바인딩 방식으로 옮긴 것.
//  - 좌표: 셰이더의 -fvk-invert-y (텍스처 행 0 = D3D 의 위) + fixup_clipspace (깊이 0..1 → -1..1), 창 y = D3D 행 번호 (뷰포트 · 가위 숫자 그대로)
//  - ES 에 없는 것:
//    · 텍스처 뷰 (glTextureView): 전체를 같은 모양 · 형식으로 읽는 뷰 = 텍스처 그대로. 밉 · 조각 일부, 큐브 ↔ 배열, 다른 형식으로 읽기,
//      스텐실 읽기 = 사본 텍스처를 두고 원본이 바뀌었을 때만(쓰기 횟수) glCopyImageSubData 로 새로 고친다 (피드백 고리도 없음)
//    · base instance 그리기: 인스턴스 단위 정점 버퍼의 시작 위치를 옮긴다 (시작 인스턴스 × 간격)
//    · 조건부 렌더링 (SetPredication): SetPredicationBuffer — 그리기를 간접 그리기로 바꿔 compute 가 쓴 0 / 1 을 InstanceCount 로 복사
//    · 뷰포트 · 가위 여러 개, 깊이 자르기 끄기, 선 채우기, 스트림 출력, 형식 버퍼 · append UAV
//  - compute (오클루전 컬링): 구조 · raw 버퍼 SRV · UAV = SSBO, 텍스처 UAV = image (밉 하나), 간접 그리기 (첫 인스턴스 없음 — 0 이어야 한다)
//  - 묶기: Gfx 가 텍스처를 만들거나 올릴 때는 맨 끝 유닛(Scratch)에만 묶는다 → 효과가 묶어 둔 유닛을 건드리지 않는다.
//    버퍼는 GL_COPY_WRITE_BUFFER 로 (GL_ELEMENT_ARRAY_BUFFER 는 VAO 의 상태라 쓰지 않는다)
namespace
{
	class GLDev;

	template <class Iface>
	class GLObj : public Iface
	{
	public:
		explicit GLObj(GLDev* dev);
		~GLObj() override;
		GLDev* Dev;
	};

	struct TexInfo
	{
		GLuint Id = 0;
		GLenum Target = GL_TEXTURE_2D;
		GLESState::Format Fmt;
		DXGI_FORMAT Dxgi = DXGI_FORMAT_UNKNOWN;
		UINT Width = 1, Height = 1, Depth = 1;
		UINT Layers = 1;
		UINT Mips = 1;
		UINT Samples = 1;
		UINT64 Bytes = 0;
		bool Uav = false;          // UNORDERED_ACCESS — image 로 쓴다 (필터 NEAREST: float32 는 필터할 수 없어 기본값이면 불완전 → 읽기 · 쓰기가 0)
		uint64_t Version = 0;      // 쓸 때마다 +1 (사본 뷰가 새로 고칠지)
		std::map<UINT, std::vector<uint8_t>> Mapped;
		std::map<UINT, D3D11_MAP> MapType;
		UINT MipW(UINT m) const { return (std::max)(1u, Width >> m); }
		UINT MipH(UINT m) const { return (std::max)(1u, Height >> m); }
		UINT MipD(UINT m) const { return (std::max)(1u, Depth >> m); }
	};

	class GLBuf : public GLObj<GfxBuffer>
	{
	public:
		using GLObj::GLObj;
		~GLBuf() override;
		void GetDesc(D3D11_BUFFER_DESC* d) const override { *d = Desc; }
		D3D11_BUFFER_DESC Desc = {};
		GLuint Id = 0;
		bool MappedNow = false;
		std::vector<uint8_t> Shadow;   // DYNAMIC: Map(WRITE_DISCARD) 는 CPU 사본, Unmap 때 한 번에 올린다
		bool ShadowMapped = false;
	};

	class GLTex1D : public GLObj<GfxTexture1D>
	{
	public:
		using GLObj::GLObj;
		~GLTex1D() override;
		void GetDesc(D3D11_TEXTURE1D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE1D_DESC Desc = {};
		TexInfo T;
	};
	class GLTex2D : public GLObj<GfxTexture2D>
	{
	public:
		using GLObj::GLObj;
		~GLTex2D() override;
		void GetDesc(D3D11_TEXTURE2D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE2D_DESC Desc = {};
		TexInfo T;
	};
	class GLTex3D : public GLObj<GfxTexture3D>
	{
	public:
		using GLObj::GLObj;
		~GLTex3D() override;
		void GetDesc(D3D11_TEXTURE3D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE3D_DESC Desc = {};
		TexInfo T;
	};

	TexInfo* TexOf(GfxResource* r)
	{
		if (!r) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		switch (dim)
		{
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D: return &static_cast<GLTex1D*>(r)->T;
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D: return &static_cast<GLTex2D*>(r)->T;
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return &static_cast<GLTex3D*>(r)->T;
		default: return nullptr;
		}
	}
	GLBuf* BufOf(GfxResource* r)
	{
		if (!r) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		return dim == D3D11_RESOURCE_DIMENSION_BUFFER ? static_cast<GLBuf*>(r) : nullptr;
	}

	// 뷰: 텍스처 그대로 (Copy = false) 또는 사본 텍스처 (Copy = true)
	struct ViewInfo
	{
		ComPtr<GfxResource> Res;
		TexInfo* Tex = nullptr;
		GLuint Name = 0;          // 셰이더 · 붙이기에 쓸 이름
		GLenum Target = 0;
		UINT Level = 0;           // 붙이기: 밉
		int Layer = -1;           // 붙이기: -1 = 전체(겹) / 0.. = 그 조각
		bool Copy = false;        // 사본 뷰
		UINT MinLevel = 0, NumLevels = 1, MinLayer = 0, NumLayers = 1;
		uint64_t CopiedVersion = ~0ull;
		// 버퍼 뷰 (구조 · raw 버퍼 SRV · UAV = SSBO): GL 버퍼 · 바이트 범위
		GLuint Buffer = 0;
		UINT BufferOffset = 0, BufferSize = 0;
		// 텍스처 UAV = image (glBindImageTexture): 텍스처 · 밉 (Level) · 형식
		GLenum ImageFormat = 0;
	};

	template <class Iface, class Desc>
	class GLView : public GLObj<Iface>
	{
	public:
		using GLObj<Iface>::GLObj;
		~GLView() override;
		void GetDesc(Desc* d) const override { *d = D; }
		void GetResource(GfxResource** out) const override
		{
			*out = V.Res.Get();
			if (*out) (*out)->AddRef();
		}
		Desc D = {};
		ViewInfo V;
	};
	using GLSrv = GLView<GfxShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
	using GLRtv = GLView<GfxRenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
	using GLDsv = GLView<GfxDepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
	using GLUav = GLView<GfxUnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;

	class GLLayout : public GLObj<GfxInputLayout>
	{
	public:
		using GLObj::GLObj;
		~GLLayout() override;
		GLuint Vao = 0;
		UINT SlotMask = 0;        // 쓰는 정점 버퍼 슬롯
		UINT InstanceMask = 0;    // 그중 인스턴스 단위 슬롯 (base instance 흉내)
	};

	template <class Iface, class Desc>
	class GLStateObj : public GLObj<Iface>
	{
	public:
		using GLObj<Iface>::GLObj;
		void GetDesc(Desc* d) const override { *d = D; }
		Desc D = {};
	};
	using GLRasterizer = GLStateObj<GfxRasterizerState, D3D11_RASTERIZER_DESC>;
	using GLBlend = GLStateObj<GfxBlendState, D3D11_BLEND_DESC>;
	using GLDepthStencil = GLStateObj<GfxDepthStencilState, D3D11_DEPTH_STENCIL_DESC>;

	class GLSampler : public GLObj<GfxSamplerState>
	{
	public:
		using GLObj::GLObj;
		~GLSampler() override;
		void GetDesc(D3D11_SAMPLER_DESC* d) const override { *d = D; }
		D3D11_SAMPLER_DESC D = {};
		GLuint Id = 0;
	};

	class GLQueryObj : public GLObj<GfxQuery>
	{
	public:
		using GLObj::GLObj;
		~GLQueryObj() override;
		void GetDesc(D3D11_QUERY_DESC* d) const override { *d = D; }
		D3D11_QUERY_DESC D = {};
		GLuint Id = 0;
		bool Issued = false;
		GLsync Fence = nullptr;   // D3D11_QUERY_EVENT: End 때 펜스 (그 앞 명령이 다 끝났나 — 오클루전 통계 읽기)
	};

	class GLCtx;

	// ---- 장치
	class GLDev : public GfxDevice
	{
	public:
		EGLContext Ctx = EGL_NO_CONTEXT;
		GLCtx* Immediate = nullptr;     // 약한 참조 (컨텍스트가 장치를 잡는다)
		GLuint EmptyVao = 0, ClearFbo = 0, DrawFbo = 0, BlitFbo = 0, ReadFbo = 0;
		GLuint ScratchUnit = 0;
		std::set<std::string> Reported;

		~GLDev() override
		{
			if (Current())
			{
				GLuint fbos[] = { ClearFbo, DrawFbo, BlitFbo, ReadFbo };
				glDeleteFramebuffers(4, fbos);
				if (EmptyVao) glDeleteVertexArrays(1, &EmptyVao);
			}
		}

		bool Current() const { return Ctx != EGL_NO_CONTEXT && eglGetCurrentContext() == Ctx; }

		bool Check(const char* what)
		{
			if (Current()) return true;
			if (Reported.insert(std::string("ctx:") + what).second)
				EditorLog::Write("Gfx", "OpenGL ES: %s called without the context - refused", what);
			return false;
		}

		void Once(const std::string& key, const char* fmt, const char* arg)
		{
			if (Reported.insert(key).second)
				EditorLog::Write("Gfx", fmt, arg);
		}

		// Gfx 가 텍스처를 만지는 유닛 (효과의 유닛과 겹치지 않게 맨 끝)
		void BindScratch(GLenum target, GLuint id)
		{
			glActiveTexture(GL_TEXTURE0 + ScratchUnit);
			glBindTexture(target, id);
		}
		void UnbindScratch(GLenum target)
		{
			glBindTexture(target, 0);
			glActiveTexture(GL_TEXTURE0);
		}

		HRESULT CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out) override;
		HRESULT CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out) override;
		HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out) override;
		HRESULT CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out) override;
		HRESULT CreateShaderResourceView(GfxResource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out) override;
		HRESULT CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out) override;
		HRESULT CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out) override;
		HRESULT CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out) override;
		HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out) override;
		HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc, GfxRasterizerState** out) override
		{
			auto* s = new GLRasterizer(this);
			s->D = *desc;
			*out = s;
			return S_OK;
		}
		HRESULT CreateBlendState(const D3D11_BLEND_DESC* desc, GfxBlendState** out) override
		{
			auto* s = new GLBlend(this);
			s->D = *desc;
			*out = s;
			return S_OK;
		}
		HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc, GfxDepthStencilState** out) override
		{
			auto* s = new GLDepthStencil(this);
			s->D = *desc;
			*out = s;
			return S_OK;
		}
		HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, GfxSamplerState** out) override
		{
			if (!Check("CreateSamplerState")) return E_FAIL;
			auto* s = new GLSampler(this);
			s->D = *desc;
			s->Id = GLESState::CreateSampler(*desc);
			*out = s;
			return S_OK;
		}
		HRESULT CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out) override
		{
			if (!Check("CreateQuery")) return E_FAIL;
			auto* q = new GLQueryObj(this);
			q->D = *desc;
			if (desc->Query == D3D11_QUERY_OCCLUSION) glGenQueries(1, &q->Id);   // ES = ANY_SAMPLES_PASSED (0 / 1)
			*out = q;
			return S_OK;
		}
		void GetImmediateContext(GfxContext** out) override;
		HRESULT GetDeviceRemovedReason() override { return S_OK; }

		void Upload(TexInfo& t, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
		HRESULT MakeTexture(TexInfo& t, const D3D11_SUBRESOURCE_DATA* data);
		bool RefreshCopy(ViewInfo& v);
	};

	template <class Iface>
	GLObj<Iface>::GLObj(GLDev* dev) : Dev(dev) { Dev->AddRef(); }
	template <class Iface>
	GLObj<Iface>::~GLObj() { Dev->GfxDevice::Release(); }

	bool CanDelete(GLDev* dev)
	{
		if (dev->Current()) return true;
		dev->Once("delete-no-context", "%s", "OpenGL ES: object released without its context - GL name leaked");
		return false;
	}

	GLBuf::~GLBuf() { if (Id && CanDelete(Dev)) glDeleteBuffers(1, &Id); }
	void DeleteTex(GLDev* dev, TexInfo& t) { if (t.Id && CanDelete(dev)) glDeleteTextures(1, &t.Id); }
	GLTex1D::~GLTex1D() { DeleteTex(Dev, T); }
	GLTex2D::~GLTex2D() { DeleteTex(Dev, T); }
	GLTex3D::~GLTex3D() { DeleteTex(Dev, T); }
	template <class Iface, class Desc>
	GLView<Iface, Desc>::~GLView()
	{
		if (V.Copy && V.Name && CanDelete(this->Dev)) glDeleteTextures(1, &V.Name);
	}
	GLLayout::~GLLayout() { if (Vao && CanDelete(Dev)) glDeleteVertexArrays(1, &Vao); }
	GLSampler::~GLSampler() { if (Id && CanDelete(Dev)) glDeleteSamplers(1, &Id); }
	GLQueryObj::~GLQueryObj()
	{
		if (!CanDelete(Dev)) return;
		if (Id) glDeleteQueries(1, &Id);
		if (Fence) glDeleteSync(Fence);
	}

	UINT FullMips(UINT w, UINT h, UINT d)
	{
		UINT m = 1, s = (std::max)((std::max)(w, h), d);
		while (s > 1) { s >>= 1; ++m; }
		return m;
	}

	UINT64 TexBytes(const TexInfo& t)
	{
		UINT64 b = 0;
		for (UINT m = 0; m < t.Mips; ++m)
			b += GLESState::SliceBytes(t.Fmt, t.MipW(m), t.MipH(m)) * t.MipD(m);
		return b * t.Layers * (std::max)(1u, t.Samples);
	}

	void Storage(GLenum target, UINT mips, GLenum internal, UINT w, UINT h, UINT depthOrLayers, UINT samples)
	{
		switch (target)
		{
		case GL_TEXTURE_2D:
		case GL_TEXTURE_CUBE_MAP: glTexStorage2D(target, mips, internal, w, h); break;
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_CUBE_MAP_ARRAY:
		case GL_TEXTURE_3D: glTexStorage3D(target, mips, internal, w, h, depthOrLayers); break;
		case GL_TEXTURE_2D_MULTISAMPLE: glTexStorage2DMultisample(target, samples, internal, w, h, GL_TRUE); break;
		default: break;
		}
	}

	HRESULT GLDev::MakeTexture(TexInfo& t, const D3D11_SUBRESOURCE_DATA* data)
	{
		if (!t.Fmt.Internal)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%d", (int)t.Dxgi);
			Once(std::string("fmt:") + buf, "OpenGL ES: unsupported texture format DXGI %s", buf);
			return E_INVALIDARG;
		}
		t.Bytes = TexBytes(t);
		glGenTextures(1, &t.Id);
		BindScratch(t.Target, t.Id);
		Storage(t.Target, t.Mips, t.Fmt.Internal, t.Width, t.Height, t.Target == GL_TEXTURE_3D ? t.Depth : t.Layers, t.Samples);
		if (t.Target != GL_TEXTURE_2D_MULTISAMPLE)
			glTexParameteri(t.Target, GL_TEXTURE_MAX_LEVEL, (GLint)t.Mips - 1);
		if (t.Uav)
		{
			// image 로 쓰는 텍스처 (Hi-Z R32F): 텍스처 자체의 필터가 NEAREST 여야 완전하다 (image 는 샘플러를 쓰지 않는다)
			glTexParameteri(t.Target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(t.Target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
		UnbindScratch(t.Target);
		if (data)
			for (UINT layer = 0; layer < t.Layers; ++layer)
				for (UINT m = 0; m < t.Mips; ++m)
				{
					const D3D11_SUBRESOURCE_DATA& d = data[layer * t.Mips + m];
					if (d.pSysMem)
						Upload(t, layer * t.Mips + m, nullptr, d.pSysMem, d.SysMemPitch, d.SysMemSlicePitch);
				}
		return S_OK;
	}

	void GLDev::Upload(TexInfo& t, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
	{
		const UINT m = sub % t.Mips, layer = sub / t.Mips;
		const UINT x = box ? box->left : 0, y = box ? box->top : 0, z = box ? box->front : 0;
		const UINT w = box ? box->right - box->left : t.MipW(m);
		const UINT h = box ? box->bottom - box->top : t.MipH(m);
		const UINT d = box ? box->back - box->front : t.MipD(m);
		const bool is3D = t.Target == GL_TEXTURE_3D;
		const UINT slices = is3D ? d : 1;
		const GLint zOff = is3D ? (GLint)z : (GLint)layer;
		++t.Version;
		BindScratch(t.Target, t.Id);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		if (t.Fmt.BlockBytes)
		{
			// 압축: rowPitch 가 꽉 찬 블록 행과 같다고 본다 (DirectXTex 가 만드는 데이터)
			const UINT64 size = (UINT64)(rowPitch ? rowPitch : GLESState::RowBytes(t.Fmt, w)) * (std::max)(1u, (h + t.Fmt.BlockH - 1) / t.Fmt.BlockH);
			if (t.Target == GL_TEXTURE_2D)
				glCompressedTexSubImage2D(t.Target, m, x, y, w, h, t.Fmt.Internal, (GLsizei)size, data);
			else if (t.Target == GL_TEXTURE_CUBE_MAP)
				glCompressedTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, m, x, y, w, h, t.Fmt.Internal, (GLsizei)size, data);
			else
				glCompressedTexSubImage3D(t.Target, m, x, y, zOff, w, h, slices, t.Fmt.Internal, (GLsizei)(size * slices), data);
			UnbindScratch(t.Target);
			return;
		}
		// 원본 행 간격 → 꽉 찬 GL 데이터 (형식 바꾸기가 있으면 여기서)
		const UINT64 srcRow = rowPitch ? rowPitch : GLESState::RowBytes(t.Fmt, w);
		const UINT64 srcSlice = depthPitch ? depthPitch : srcRow * h;
		const UINT glBpp = GLESState::GLBytesPerPixel(t.Fmt), srcBpp = (std::max)(1u, t.Fmt.Bits / 8);
		std::vector<uint8_t> packed((size_t)w * h * slices * glBpp);
		for (UINT s = 0; s < slices; ++s)
			for (UINT row = 0; row < h; ++row)
			{
				const uint8_t* src = (const uint8_t*)data + s * srcSlice + row * srcRow;
				uint8_t* dst = packed.data() + ((size_t)s * h + row) * w * glBpp;
				switch (t.Fmt.Conv)
				{
				case GLESState::Convert::SwapRB:
					for (UINT i = 0; i < w; ++i)
					{
						dst[i * 4 + 0] = src[i * 4 + 2];
						dst[i * 4 + 1] = src[i * 4 + 1];
						dst[i * 4 + 2] = src[i * 4 + 0];
						dst[i * 4 + 3] = src[i * 4 + 3];
					}
					break;
				case GLESState::Convert::Unorm16ToFloat:
					for (UINT i = 0; i < w * t.Fmt.Channels; ++i)
					{
						uint16_t v;
						memcpy(&v, src + i * 2, 2);
						const float f = v / 65535.0f;
						memcpy(dst + i * 4, &f, 4);
					}
					break;
				default:
					memcpy(dst, src, (size_t)w * srcBpp);
					break;
				}
			}
		switch (t.Target)
		{
		case GL_TEXTURE_2D: glTexSubImage2D(t.Target, m, x, y, w, h, t.Fmt.Upload, t.Fmt.Type, packed.data()); break;
		case GL_TEXTURE_CUBE_MAP: glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, m, x, y, w, h, t.Fmt.Upload, t.Fmt.Type, packed.data()); break;
		default: glTexSubImage3D(t.Target, m, x, y, zOff, w, h, slices, t.Fmt.Upload, t.Fmt.Type, packed.data()); break;
		}
		UnbindScratch(t.Target);
	}

	HRESULT GLDev::CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!desc || desc->ByteWidth == 0) return E_INVALIDARG;
		if (!Check("CreateBuffer")) return E_FAIL;
		auto* b = new GLBuf(this);
		b->Desc = *desc;
		glGenBuffers(1, &b->Id);
		glBindBuffer(GL_COPY_WRITE_BUFFER, b->Id);
		const GLenum usage = desc->Usage == D3D11_USAGE_DYNAMIC ? GL_DYNAMIC_DRAW : desc->Usage == D3D11_USAGE_STAGING ? GL_STREAM_READ : GL_STATIC_DRAW;
		glBufferData(GL_COPY_WRITE_BUFFER, desc->ByteWidth, data ? data->pSysMem : nullptr, usage);
		if (desc->Usage == D3D11_USAGE_DYNAMIC)
		{
			b->Shadow.resize(desc->ByteWidth);
			if (data && data->pSysMem) memcpy(b->Shadow.data(), data->pSysMem, desc->ByteWidth);
		}
		*out = b;
		return S_OK;
	}

	HRESULT GLDev::CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateTexture1D")) return E_FAIL;
		auto* t = new GLTex1D(this);
		t->Desc = *desc;
		TexInfo& i = t->T;
		i.Dxgi = desc->Format;
		i.Fmt = GLESState::FromDxgi(desc->Format, (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0);
		i.Width = desc->Width;
		i.Layers = (std::max)(1u, desc->ArraySize);
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, 1, 1);
		t->Desc.MipLevels = i.Mips;
		i.Target = i.Layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;   // ES 에 1D 가 없다 → 높이 1
		const HRESULT hr = MakeTexture(i, data);
		if (FAILED(hr)) { delete t; return hr; }
		*out = t;
		return S_OK;
	}

	HRESULT GLDev::CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateTexture2D")) return E_FAIL;
		auto* t = new GLTex2D(this);
		t->Desc = *desc;
		TexInfo& i = t->T;
		i.Dxgi = desc->Format;
		i.Fmt = GLESState::FromDxgi(desc->Format, (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0);
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Layers = (std::max)(1u, desc->ArraySize);
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, 1);
		t->Desc.MipLevels = i.Mips;
		i.Samples = (std::max)(1u, desc->SampleDesc.Count);
		i.Uav = (desc->BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
		const bool cube = (desc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
		if (i.Samples > 1) i.Target = GL_TEXTURE_2D_MULTISAMPLE;
		else if (cube) i.Target = i.Layers == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_CUBE_MAP_ARRAY;
		else i.Target = i.Layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
		const HRESULT hr = MakeTexture(i, data);
		if (FAILED(hr)) { delete t; return hr; }
		*out = t;
		return S_OK;
	}

	HRESULT GLDev::CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateTexture3D")) return E_FAIL;
		auto* t = new GLTex3D(this);
		t->Desc = *desc;
		TexInfo& i = t->T;
		i.Dxgi = desc->Format;
		i.Fmt = GLESState::FromDxgi(desc->Format, false);
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Depth = desc->Depth;
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, desc->Depth);
		t->Desc.MipLevels = i.Mips;
		i.Target = GL_TEXTURE_3D;
		const HRESULT hr = MakeTexture(i, data);
		if (FAILED(hr)) { delete t; return hr; }
		*out = t;
		return S_OK;
	}

	// 사본 뷰 새로 고치기: 원본이 마지막 복사 뒤에 바뀌었으면 [밉 · 조각] 범위를 복사
	bool GLDev::RefreshCopy(ViewInfo& v)
	{
		TexInfo& t = *v.Tex;
		if (v.CopiedVersion == t.Version) return true;
		for (UINT l = 0; l < v.NumLevels; ++l)
		{
			const UINT m = v.MinLevel + l;
			const bool is3D = t.Target == GL_TEXTURE_3D;
			glCopyImageSubData(t.Id, t.Target, (GLint)m, 0, 0, is3D ? 0 : (GLint)v.MinLayer,
				v.Name, v.Target, (GLint)l, 0, 0, 0, (GLsizei)t.MipW(m), (GLsizei)t.MipH(m), (GLsizei)(is3D ? t.MipD(m) : v.NumLayers));
		}
		v.CopiedVersion = t.Version;
		return true;
	}

	// 셰이더가 읽는 뷰: 전체 · 같은 모양 · 같은 형식이면 텍스처 그대로, 아니면 사본 텍스처
	bool MakeViewName(GLDev* dev, TexInfo& t, GLenum target, DXGI_FORMAT viewFormat, UINT minLevel, UINT numLevels, UINT minLayer, UINT numLayers,
		bool stencil, ViewInfo& v)
	{
		GLenum internal = t.Fmt.Internal;
		if (viewFormat != DXGI_FORMAT_UNKNOWN && !t.Fmt.Depth)
		{
			const GLESState::Format f = GLESState::FromDxgi(viewFormat, false);
			if (f.Internal && GLESState::GLBytesPerPixel(f) == GLESState::GLBytesPerPixel(t.Fmt) && f.BlockBytes == t.Fmt.BlockBytes)
				internal = f.Internal;   // 같은 크기 부류만 (glCopyImageSubData 가 옮길 수 있는 것)
		}
		numLevels = (std::min)(numLevels, t.Mips - (std::min)(minLevel, t.Mips));
		numLayers = (std::min)(numLayers, t.Layers - (std::min)(minLayer, t.Layers));
		if (numLevels == 0 || numLayers == 0)
			return false;
		v.Tex = &t;
		v.Target = target;
		if (!stencil && target == t.Target && internal == t.Fmt.Internal && minLevel == 0 && numLevels == t.Mips && minLayer == 0 && numLayers == t.Layers)
		{
			v.Name = t.Id;
			return true;
		}
		v.Copy = true;
		v.MinLevel = minLevel;
		v.NumLevels = numLevels;
		v.MinLayer = minLayer;
		v.NumLayers = numLayers;
		glGenTextures(1, &v.Name);
		dev->BindScratch(target, v.Name);
		const UINT w = t.MipW(minLevel), h = t.MipH(minLevel);
		Storage(target, numLevels, internal, w, h, target == GL_TEXTURE_3D ? t.MipD(minLevel) : numLayers, t.Samples);
		if (target != GL_TEXTURE_2D_MULTISAMPLE)
			glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)numLevels - 1);
		if (stencil)
			glTexParameteri(target, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX);
		dev->UnbindScratch(target);
		return true;
	}

	HRESULT GLDev::CreateShaderResourceView(GfxResource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateShaderResourceView")) return E_FAIL;
		TexInfo* t = TexOf(r);
		if (!t)
		{
			// 구조 · raw 버퍼 → SSBO (셰이더의 StructuredBuffer · ByteAddressBuffer)
			GLBuf* b = BufOf(r);
			if (!b || !desc || (desc->ViewDimension != D3D11_SRV_DIMENSION_BUFFER && desc->ViewDimension != D3D11_SRV_DIMENSION_BUFFEREX))
			{
				Once("srv-buffer", "%s", "OpenGL ES: this buffer shader resource view is not supported (structured / raw only)");
				return E_NOTIMPL;
			}
			const UINT stride = desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? 4 : (b->Desc.StructureByteStride ? b->Desc.StructureByteStride : 4);
			const UINT first = desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? desc->BufferEx.FirstElement : desc->Buffer.FirstElement;
			const UINT count = desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? desc->BufferEx.NumElements : desc->Buffer.NumElements;
			auto* v = new GLSrv(this);
			v->D = *desc;
			v->V.Res = r;
			v->V.Buffer = b->Id;
			v->V.BufferOffset = first * stride;
			v->V.BufferSize = (std::min)(count * stride, b->Desc.ByteWidth - (std::min)(first * stride, b->Desc.ByteWidth));
			*out = v;
			return S_OK;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC d = {};
		if (desc) d = *desc;
		else
		{
			d.Format = t->Dxgi;
			switch (t->Target)
			{
			case GL_TEXTURE_2D: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels = t->Mips; break;
			case GL_TEXTURE_2D_ARRAY: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipLevels = t->Mips; d.Texture2DArray.ArraySize = t->Layers; break;
			case GL_TEXTURE_CUBE_MAP: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; d.TextureCube.MipLevels = t->Mips; break;
			case GL_TEXTURE_CUBE_MAP_ARRAY: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; d.TextureCubeArray.MipLevels = t->Mips; d.TextureCubeArray.NumCubes = t->Layers / 6; break;
			case GL_TEXTURE_3D: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D; d.Texture3D.MipLevels = t->Mips; break;
			case GL_TEXTURE_2D_MULTISAMPLE: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS; break;
			default: break;
			}
		}
		auto mips = [&](UINT most, UINT count) { return count == (UINT)-1 ? t->Mips - (std::min)(most, t->Mips) : count; };
		GLenum target = 0;
		UINT minLevel = 0, numLevels = 1, minLayer = 0, numLayers = 1;
		switch (d.ViewDimension)
		{
		// 1D = 높이 1 인 2D
		case D3D11_SRV_DIMENSION_TEXTURE1D: target = GL_TEXTURE_2D; minLevel = d.Texture1D.MostDetailedMip; numLevels = mips(minLevel, d.Texture1D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: target = GL_TEXTURE_2D_ARRAY; minLevel = d.Texture1DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture1DArray.MipLevels);
			minLayer = d.Texture1DArray.FirstArraySlice; numLayers = d.Texture1DArray.ArraySize; break;
		case D3D11_SRV_DIMENSION_TEXTURE2D: target = GL_TEXTURE_2D; minLevel = d.Texture2D.MostDetailedMip; numLevels = mips(minLevel, d.Texture2D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE2DARRAY: target = GL_TEXTURE_2D_ARRAY; minLevel = d.Texture2DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture2DArray.MipLevels);
			minLayer = d.Texture2DArray.FirstArraySlice; numLayers = d.Texture2DArray.ArraySize; break;
		case D3D11_SRV_DIMENSION_TEXTURECUBE: target = GL_TEXTURE_CUBE_MAP; minLevel = d.TextureCube.MostDetailedMip; numLevels = mips(minLevel, d.TextureCube.MipLevels); numLayers = 6; break;
		case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY: target = GL_TEXTURE_CUBE_MAP_ARRAY; minLevel = d.TextureCubeArray.MostDetailedMip; numLevels = mips(minLevel, d.TextureCubeArray.MipLevels);
			minLayer = d.TextureCubeArray.First2DArrayFace; numLayers = d.TextureCubeArray.NumCubes * 6; break;
		case D3D11_SRV_DIMENSION_TEXTURE3D: target = GL_TEXTURE_3D; minLevel = d.Texture3D.MostDetailedMip; numLevels = mips(minLevel, d.Texture3D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE2DMS: target = GL_TEXTURE_2D_MULTISAMPLE; break;
		default:
			Once("srv-dim", "%s", "OpenGL ES: unsupported shader resource view dimension");
			return E_NOTIMPL;
		}
		// 배열 하나를 2D 로 (조각 하나) — D3D 는 2D 텍스처 뷰가 조각 0 을 읽는다
		if (target == GL_TEXTURE_2D && t->Target == GL_TEXTURE_2D_ARRAY) numLayers = 1;
		auto* v = new GLSrv(this);
		v->D = d;
		v->V.Res = r;
		const bool stencil = d.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT || d.Format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
		if (!MakeViewName(this, *t, target, d.Format, minLevel, numLevels, minLayer, numLayers, stencil, v->V))
		{
			v->Release();
			return E_INVALIDARG;
		}
		*out = v;
		return S_OK;
	}

	HRESULT GLDev::CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!desc || !Check("CreateUnorderedAccessView")) return E_INVALIDARG;
		if (GLBuf* b = BufOf(r))
		{
			// 구조 · raw 버퍼 → SSBO (RWStructuredBuffer · RWByteAddressBuffer)
			if (desc->ViewDimension != D3D11_UAV_DIMENSION_BUFFER || (desc->Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)))
			{
				Once("uav-buffer", "%s", "OpenGL ES: only structured / raw buffer UAVs without append or counter are supported");
				return E_NOTIMPL;
			}
			const UINT stride = (desc->Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) ? 4 : (b->Desc.StructureByteStride ? b->Desc.StructureByteStride : 4);
			auto* v = new GLUav(this);
			v->D = *desc;
			v->V.Res = r;
			v->V.Buffer = b->Id;
			v->V.BufferOffset = desc->Buffer.FirstElement * stride;
			v->V.BufferSize = (std::min)(desc->Buffer.NumElements * stride, b->Desc.ByteWidth - (std::min)(v->V.BufferOffset, b->Desc.ByteWidth));
			*out = v;
			return S_OK;
		}
		TexInfo* t = TexOf(r);
		if (!t || !t->Uav || t->Target != GL_TEXTURE_2D || desc->ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D || desc->Texture2D.MipSlice >= t->Mips)
		{
			Once("uav", "%s", "OpenGL ES: only buffer and 2D texture (one mip) unordered access views are supported");
			return E_NOTIMPL;
		}
		// 텍스처 → image 유닛 (밉 하나). 형식은 텍스처 형식 그대로 (ES 의 image 는 텍스처 형식과 같아야 한다)
		auto* v = new GLUav(this);
		v->D = *desc;
		v->V.Res = r;
		v->V.Tex = t;
		v->V.Name = t->Id;
		v->V.Target = t->Target;
		v->V.Level = desc->Texture2D.MipSlice;
		v->V.ImageFormat = t->Fmt.Internal;
		*out = v;
		return S_OK;
	}

	// 붙이기용 뷰 (렌더 타깃 · 깊이): 밉 하나, 조각 하나 또는 전체(겹). 다른 형식으로 쓰기는 ES 에서 못 한다 → 텍스처 형식 그대로
	template <class V>
	HRESULT MakeAttachView(GLDev* dev, GfxResource* r, DXGI_FORMAT fmt, UINT mip, UINT firstLayer, bool layered, V* v)
	{
		TexInfo* t = TexOf(r);
		if (!t) return E_INVALIDARG;
		v->V.Res = r;
		v->V.Tex = t;
		if (fmt != DXGI_FORMAT_UNKNOWN && !t->Fmt.Depth)
		{
			const GLESState::Format f = GLESState::FromDxgi(fmt, false);
			if (f.Internal && f.Internal != t->Fmt.Internal)
				dev->Once("rtv-fmt", "%s", "OpenGL ES: render target view with a different format writes in the texture format");
		}
		v->V.Name = t->Id;
		v->V.Target = t->Target;
		v->V.Level = mip;
		v->V.Layer = (layered || (t->Layers <= 1 && t->Target != GL_TEXTURE_3D)) ? -1 : (int)firstLayer;
		return S_OK;
	}

	HRESULT GLDev::CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateRenderTargetView")) return E_FAIL;
		TexInfo* t = TexOf(r);
		if (!t) return E_INVALIDARG;
		auto* v = new GLRtv(this);
		UINT mip = 0, first = 0, count = t->Layers;
		bool layered = t->Layers > 1;
		if (desc)
		{
			v->D = *desc;
			switch (desc->ViewDimension)
			{
			case D3D11_RTV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; layered = false; count = 1; break;
			case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice; count = desc->Texture2DArray.ArraySize;
				layered = count > 1; break;
			case D3D11_RTV_DIMENSION_TEXTURE1D: mip = desc->Texture1D.MipSlice; layered = false; break;
			case D3D11_RTV_DIMENSION_TEXTURE3D: mip = desc->Texture3D.MipSlice; first = desc->Texture3D.FirstWSlice; count = desc->Texture3D.WSize; layered = count != 1; break;
			default: break;
			}
		}
		else
		{
			v->D.Format = t->Dxgi;
			v->D.ViewDimension = t->Layers > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
		}
		const HRESULT hr = MakeAttachView(this, r, v->D.Format, mip, first, layered, v);
		if (FAILED(hr)) { v->Release(); return hr; }
		*out = v;
		return S_OK;
	}

	HRESULT GLDev::CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateDepthStencilView")) return E_FAIL;
		TexInfo* t = TexOf(r);
		if (!t || !t->Fmt.Depth) return E_INVALIDARG;
		auto* v = new GLDsv(this);
		UINT mip = 0, first = 0;
		bool layered = t->Layers > 1;
		if (desc)
		{
			v->D = *desc;
			switch (desc->ViewDimension)
			{
			case D3D11_DSV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; layered = false; break;
			case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice;
				layered = desc->Texture2DArray.ArraySize > 1; break;
			default: break;
			}
		}
		else
		{
			v->D.Format = t->Dxgi;
			v->D.ViewDimension = t->Layers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
		}
		const HRESULT hr = MakeAttachView(this, r, DXGI_FORMAT_UNKNOWN, mip, first, layered, v);
		if (FAILED(hr)) { v->Release(); return hr; }
		*out = v;
		return S_OK;
	}

	HRESULT GLDev::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!Check("CreateInputLayout")) return E_FAIL;
		const auto* sig = static_cast<const GLInputSignature*>(signature);
		if (!sig || signatureSize != sizeof(GLInputSignature) || sig->Magic != GLInputSignature::kMagic || !sig->Inputs)
		{
			Once("layout-sig", "%s", "OpenGL ES: CreateInputLayout needs a pass signature from FxPass::GetDesc");
			return E_INVALIDARG;
		}
		auto upper = [](std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; };
		auto location = [&](const std::string& sem) -> int {
			for (const auto& [s, l] : *sig->Inputs)
				if (s == sem) return l;
			return -1;
		};
		auto* l = new GLLayout(this);
		glGenVertexArrays(1, &l->Vao);
		glBindVertexArray(l->Vao);
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		std::set<std::string> provided;
		for (UINT i = 0; i < count; ++i)
		{
			const D3D11_INPUT_ELEMENT_DESC& e = elements[i];
			GLint size; GLenum type; GLboolean norm; bool integer; UINT bytes;
			if (!GLESState::VertexFormat(e.Format, size, type, norm, integer, bytes))
			{
				Once(std::string("vfmt:") + e.SemanticName, "OpenGL ES: unsupported vertex format for %s", e.SemanticName);
				glBindVertexArray(0);
				l->Release();
				return E_INVALIDARG;
			}
			const UINT offset = e.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? offsets[e.InputSlot] : e.AlignedByteOffset;
			offsets[e.InputSlot] = offset + bytes;
			const std::string base = upper(e.SemanticName);
			const std::string sem = base + std::to_string(e.SemanticIndex);
			provided.insert(sem);
			int loc = location(sem);
			if (loc < 0 && e.SemanticIndex > 0)
			{
				const int first = location(base + "0");   // 행렬 입력 (WORLD0..3)
				if (first >= 0) loc = first + (int)e.SemanticIndex;
			}
			if (loc < 0) continue;   // 셰이더가 쓰지 않는 요소 (D3D 와 같이 무시)
			glEnableVertexAttribArray(loc);
			if (integer) glVertexAttribIFormat(loc, size, type, offset);
			else glVertexAttribFormat(loc, size, type, norm, offset);
			glVertexAttribBinding(loc, e.InputSlot);
			const bool perInstance = e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA;
			glVertexBindingDivisor(e.InputSlot, perInstance ? (std::max)(1u, e.InstanceDataStepRate) : 0);
			l->SlotMask |= 1u << e.InputSlot;
			if (perInstance) l->InstanceMask |= 1u << e.InputSlot;
		}
		glBindVertexArray(0);
		for (const auto& [s, loc] : *sig->Inputs)
		{
			if (provided.count(s)) continue;
			if (s.rfind("SV_", 0) == 0) continue;
			Once("missing:" + s, "OpenGL ES: vertex input %s is not provided by the layout", s.c_str());
			l->Release();
			return E_INVALIDARG;
		}
		*out = l;
		return S_OK;
	}

	// ---- 컨텍스트
	class GLCtx : public GfxContext
	{
	public:
		ComPtr<GLDev> Dev;
		ComPtr<GfxInputLayout> Layout;
		D3D11_PRIMITIVE_TOPOLOGY Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		struct VB { ComPtr<GfxBuffer> Buf; UINT Stride = 0, Offset = 0; };
		VB Vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		ComPtr<GfxBuffer> Ib;
		DXGI_FORMAT IbFormat = DXGI_FORMAT_R16_UINT;
		UINT IbOffset = 0;
		ComPtr<GfxRenderTargetView> Rtvs[8];
		UINT RtvCount = 0;
		ComPtr<GfxDepthStencilView> Dsv;
		bool TargetsBound = false;
		ComPtr<GfxDepthStencilState> Ds;
		UINT StencilRef = 0;
		ComPtr<GfxBlendState> Bs;
		float BlendFactor[4] = { 1, 1, 1, 1 };
		UINT SampleMask = 0xFFFFFFFF;
		ComPtr<GfxRasterizerState> Rs;
		D3D11_VIEWPORT Viewports[16] = {};
		UINT ViewportCount = 0;
		D3D11_RECT Scissors[16] = {};
		UINT ScissorCount = 0;

		~GLCtx() override
		{
			if (PredArgs && Dev && Dev->Current()) glDeleteBuffers(1, &PredArgs);
			if (Dev && Dev->Immediate == this) Dev->Immediate = nullptr;
		}

		void ApplyDepthStencil()
		{
			D3D11_DEPTH_STENCIL_DESC d = Ds ? static_cast<GLDepthStencil*>(Ds.Get())->D : FxStates::DefaultDepthStencil();
			if (Dsv)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dd;
				Dsv->GetDesc(&dd);
				if (dd.Flags & D3D11_DSV_READ_ONLY_DEPTH) d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
				if (dd.Flags & D3D11_DSV_READ_ONLY_STENCIL) d.StencilWriteMask = 0;
			}
			GLESState::ApplyDepthStencil(d, StencilRef);
		}
		void ApplyBlend()
		{
			const D3D11_BLEND_DESC d = Bs ? static_cast<GLBlend*>(Bs.Get())->D : FxStates::DefaultBlend();
			GLESState::ApplyBlend(d, BlendFactor, SampleMask);
		}
		void ApplyRasterizer()
		{
			const D3D11_RASTERIZER_DESC d = Rs ? static_cast<GLRasterizer*>(Rs.Get())->D : FxStates::DefaultRasterizer();
			GLESState::ApplyRasterizer(d);
		}

		// 지금 묶인 프레임버퍼(fbTarget)에 뷰 붙이기
		static void Attach(GLenum fbTarget, GLenum attachment, const ViewInfo* v)
		{
			if (!v || !v->Name)
			{
				glFramebufferTexture2D(fbTarget, attachment, GL_TEXTURE_2D, 0, 0);
				return;
			}
			const GLenum target = v->Target;
			if (v->Layer < 0)
			{
				if (target == GL_TEXTURE_2D || target == GL_TEXTURE_2D_MULTISAMPLE) glFramebufferTexture2D(fbTarget, attachment, target, v->Name, v->Level);
				else glFramebufferTexture(fbTarget, attachment, v->Name, v->Level);   // 겹 (지오메트리 셰이더가 조각을 고른다)
			}
			else if (target == GL_TEXTURE_CUBE_MAP)
				glFramebufferTexture2D(fbTarget, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + v->Layer, v->Name, v->Level);
			else
				glFramebufferTextureLayer(fbTarget, attachment, v->Name, v->Level, v->Layer);
		}
		static GLenum DepthAttachment(const ViewInfo* v) { return v && v->Tex && v->Tex->Fmt.Stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT; }

		void BindTargets()
		{
			glBindFramebuffer(GL_FRAMEBUFFER, Dev->DrawFbo);
			GLenum bufs[8];
			for (UINT i = 0; i < 8; ++i)
			{
				const ViewInfo* v = i < RtvCount && Rtvs[i] ? &static_cast<GLRtv*>(Rtvs[i].Get())->V : nullptr;
				Attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, v);
				bufs[i] = v ? GL_COLOR_ATTACHMENT0 + i : GL_NONE;
			}
			glDrawBuffers(8, bufs);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			const ViewInfo* dv = Dsv ? &static_cast<GLDsv*>(Dsv.Get())->V : nullptr;
			if (dv) Attach(GL_FRAMEBUFFER, DepthAttachment(dv), dv);
			TargetsBound = true;
		}
		void RebindDraw() { glBindFramebuffer(GL_FRAMEBUFFER, TargetsBound ? Dev->DrawFbo : 0); }

		// 그리기 전에: 묶인 타깃은 이번 그리기로 바뀐다 (사본 뷰가 새로 고치도록)
		void MarkTargetsWritten()
		{
			for (UINT i = 0; i < RtvCount; ++i)
				if (Rtvs[i]) if (TexInfo* t = static_cast<GLRtv*>(Rtvs[i].Get())->V.Tex) ++t->Version;
			if (Dsv)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dd;
				Dsv->GetDesc(&dd);
				if (!(dd.Flags & D3D11_DSV_READ_ONLY_DEPTH))
					if (TexInfo* t = static_cast<GLDsv*>(Dsv.Get())->V.Tex) ++t->Version;
			}
		}

		// ---- IA
		void IASetInputLayout(GfxInputLayout* l) override { Layout = l; }
		void IAGetInputLayout(GfxInputLayout** l) override { *l = Layout.Get(); if (*l) (*l)->AddRef(); }
		void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t) override { Topo = t; }
		void IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* t) override { *t = Topo; }
		void IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets) override
		{
			for (UINT i = 0; i < count && start + i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
				Vbs[start + i] = { buffers ? buffers[i] : nullptr, strides ? strides[i] : 0, offsets ? offsets[i] : 0 };
		}
		void IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets) override
		{
			for (UINT i = 0; i < count && start + i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
			{
				const VB& v = Vbs[start + i];
				if (buffers) { buffers[i] = v.Buf.Get(); if (buffers[i]) buffers[i]->AddRef(); }
				if (strides) strides[i] = v.Stride;
				if (offsets) offsets[i] = v.Offset;
			}
		}
		void IASetIndexBuffer(GfxBuffer* b, DXGI_FORMAT f, UINT o) override { Ib = b; IbFormat = f; IbOffset = o; }
		void IAGetIndexBuffer(GfxBuffer** b, DXGI_FORMAT* f, UINT* o) override
		{
			if (b) { *b = Ib.Get(); if (*b) (*b)->AddRef(); }
			if (f) *f = IbFormat;
			if (o) *o = IbOffset;
		}
		void SOSetTargets(UINT count, GfxBuffer* const* buffers, const UINT*) override
		{
			if (count && buffers && buffers[0])
				Dev->Once("so", "%s", "OpenGL ES: stream output is not supported");
		}

		// ---- OM / RS
		void OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv) override
		{
			RtvCount = (std::min)(count, 8u);
			for (UINT i = 0; i < 8; ++i)
				Rtvs[i] = i < RtvCount && rtvs ? rtvs[i] : nullptr;
			const bool dsvChanged = (Dsv.Get() != dsv);
			Dsv = dsv;
			if (Dev->Check("OMSetRenderTargets"))
			{
				BindTargets();
				if (dsvChanged) ApplyDepthStencil();
			}
		}
		void OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv) override
		{
			for (UINT i = 0; rtvs && i < count; ++i)
			{
				rtvs[i] = i < 8 ? Rtvs[i].Get() : nullptr;
				if (rtvs[i]) rtvs[i]->AddRef();
			}
			if (dsv) { *dsv = Dsv.Get(); if (*dsv) (*dsv)->AddRef(); }
		}
		void OMSetDepthStencilState(GfxDepthStencilState* s, UINT ref) override { Ds = s; StencilRef = ref; if (Dev->Check("OMSetDepthStencilState")) ApplyDepthStencil(); }
		void OMGetDepthStencilState(GfxDepthStencilState** s, UINT* ref) override
		{
			if (s) { *s = Ds.Get(); if (*s) (*s)->AddRef(); }
			if (ref) *ref = StencilRef;
		}
		void OMSetBlendState(GfxBlendState* s, const FLOAT f[4], UINT mask) override
		{
			Bs = s;
			for (int i = 0; i < 4; ++i) BlendFactor[i] = f ? f[i] : 1.0f;
			SampleMask = mask;
			if (Dev->Check("OMSetBlendState")) ApplyBlend();
		}
		void OMGetBlendState(GfxBlendState** s, FLOAT f[4], UINT* mask) override
		{
			if (s) { *s = Bs.Get(); if (*s) (*s)->AddRef(); }
			if (f) for (int i = 0; i < 4; ++i) f[i] = BlendFactor[i];
			if (mask) *mask = SampleMask;
		}
		void RSSetState(GfxRasterizerState* s) override { Rs = s; if (Dev->Check("RSSetState")) ApplyRasterizer(); }
		void RSGetState(GfxRasterizerState** s) override { *s = Rs.Get(); if (*s) (*s)->AddRef(); }
		// ES 는 뷰포트 · 가위가 하나 (0 번만 쓴다)
		void RSSetViewports(UINT count, const D3D11_VIEWPORT* v) override
		{
			ViewportCount = (std::min)(count, 16u);
			for (UINT i = 0; i < ViewportCount; ++i) Viewports[i] = v[i];
			if (ViewportCount && Dev->Check("RSSetViewports"))
			{
				glViewport((GLint)v[0].TopLeftX, (GLint)v[0].TopLeftY, (GLsizei)v[0].Width, (GLsizei)v[0].Height);   // 창 y = D3D 행 번호
				glDepthRangef(v[0].MinDepth, v[0].MaxDepth);
			}
		}
		void RSGetViewports(UINT* count, D3D11_VIEWPORT* v) override
		{
			if (v) for (UINT i = 0; i < (std::min)(*count, ViewportCount); ++i) v[i] = Viewports[i];
			*count = ViewportCount;
		}
		void RSSetScissorRects(UINT count, const D3D11_RECT* r) override
		{
			ScissorCount = (std::min)(count, 16u);
			for (UINT i = 0; i < ScissorCount; ++i) Scissors[i] = r[i];
			if (ScissorCount && Dev->Check("RSSetScissorRects"))
				glScissor(r[0].left, r[0].top, (std::max)(0, r[0].right - r[0].left), (std::max)(0, r[0].bottom - r[0].top));
		}
		void RSGetScissorRects(UINT* count, D3D11_RECT* r) override
		{
			if (r) for (UINT i = 0; i < (std::min)(*count, ScissorCount); ++i) r[i] = Scissors[i];
			*count = ScissorCount;
		}

		void SetSrvs(const char* stage, UINT count, GfxShaderResourceView* const* views)
		{
			for (UINT i = 0; views && i < count; ++i)
				if (views[i]) { Dev->Once(std::string("srv:") + stage, "OpenGL ES: %sSetShaderResources with a view outside effects is ignored", stage); break; }
		}
		void PSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { SetSrvs("PS", count, views); }
		void VSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { SetSrvs("VS", count, views); }
		void CSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { SetSrvs("CS", count, views); }
		void CSSetUnorderedAccessViews(UINT, UINT, GfxUnorderedAccessView* const*, const UINT*) override {}
		void CSSetShader(void*, void*, UINT) override {}

		// ---- 그리기
		bool PrepareDraw(GLenum& mode, UINT startInstance)
		{
			if (!Dev->Check("Draw")) return false;
			GLint patch = 0;
			mode = GLESState::Topology(Topo, patch);
			if (patch) glPatchParameteri(GL_PATCH_VERTICES, patch);
			GLuint vao = Dev->EmptyVao;
			UINT mask = 0, instanceMask = 0;
			if (Layout)
			{
				auto* l = static_cast<GLLayout*>(Layout.Get());
				vao = l->Vao;
				mask = l->SlotMask;
				instanceMask = l->InstanceMask;
			}
			glBindVertexArray(vao);
			for (UINT s = 0; mask; ++s, mask >>= 1)
				if (mask & 1)
				{
					const VB& v = Vbs[s];
					GLBuf* b = BufOf(v.Buf.Get());
					// base instance 흉내: 인스턴스 단위 버퍼를 시작 인스턴스만큼 옮긴다
					const UINT offset = v.Offset + ((instanceMask >> s) & 1 ? startInstance * v.Stride : 0);
					glBindVertexBuffer(s, b ? b->Id : 0, offset, v.Stride);
				}
			GLBuf* ib = BufOf(Ib.Get());
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib ? ib->Id : 0);
			MarkTargetsWritten();
			return true;
		}
		GLenum IndexType() const { return IbFormat == DXGI_FORMAT_R32_UINT ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT; }
		UINT IndexSize() const { return IbFormat == DXGI_FORMAT_R32_UINT ? 4 : 2; }

		void Draw(UINT c, UINT s) override
		{
			GLenum mode;
			if (!PrepareDraw(mode, 0)) return;
			if (!(PredBuf && DrawPredicated(mode, false, c, s, 0))) glDrawArrays(mode, s, c);
		}
		void DrawIndexed(UINT c, UINT s, INT b) override { DrawIndexedInstanced(c, 1, s, b, 0); }
		void DrawInstanced(UINT c, UINT n, UINT s, UINT si) override
		{
			GLenum mode;
			if (!PrepareDraw(mode, si)) return;
			if (!(PredBuf && n == 1 && DrawPredicated(mode, false, c, s, 0))) glDrawArraysInstanced(mode, s, c, n);
		}
		void DrawIndexedInstanced(UINT c, UINT n, UINT s, INT b, UINT si) override
		{
			GLenum mode;
			if (!Ib || !PrepareDraw(mode, si)) return;
			if (PredBuf && n == 1 && IbOffset % IndexSize() == 0 && DrawPredicated(mode, true, c, s + IbOffset / IndexSize(), b)) return;
			glDrawElementsInstancedBaseVertex(mode, c, IndexType(), (const void*)(uintptr_t)(IbOffset + (UINT64)s * IndexSize()), n, b);
		}

		// ---- 예측 (조건부 렌더링 대신): 그리기 인자를 CPU 가 쓰고 InstanceCount 자리에 버퍼의 0 / 1 을 복사 → 간접 그리기
		static constexpr UINT kPredSlots = 1024;   // 인자 고리 (20 바이트씩 — 다시 쓰면 GL 이 앞 그리기 뒤로 맞춘다)
		ComPtr<GfxBuffer> PredBuf;
		UINT PredOffset = 0;
		GLuint PredArgs = 0;
		UINT PredNext = 0;
		bool SetPredicationBuffer(GfxBuffer* buffer, UINT offset) override
		{
			if (!Dev->Check("SetPredicationBuffer")) return false;
			PredBuf = buffer;
			PredOffset = offset;
			return true;
		}
		bool DrawPredicated(GLenum mode, bool indexed, UINT count, UINT first, INT baseVertex)
		{
			GLBuf* pb = BufOf(PredBuf.Get());
			if (!pb || PredOffset + 4 > pb->Desc.ByteWidth) return false;
			if (!PredArgs)
			{
				glGenBuffers(1, &PredArgs);
				glBindBuffer(GL_COPY_WRITE_BUFFER, PredArgs);
				glBufferData(GL_COPY_WRITE_BUFFER, kPredSlots * 20, nullptr, GL_DYNAMIC_DRAW);
			}
			const GLintptr at = (GLintptr)(PredNext++ % kPredSlots) * 20;
			// 인덱스: (count, instanceCount, firstIndex, baseVertex, 0) · 정점: (count, instanceCount, first, 0)
			const uint32_t args[5] = { count, 0, first, indexed ? (uint32_t)baseVertex : 0u, 0 };
			glBindBuffer(GL_COPY_WRITE_BUFFER, PredArgs);
			glBufferSubData(GL_COPY_WRITE_BUFFER, at, indexed ? 20 : 16, args);
			glBindBuffer(GL_COPY_READ_BUFFER, pb->Id);
			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, PredOffset, at + 4, 4);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, PredArgs);
			if (indexed) glDrawElementsIndirect(mode, IndexType(), (const void*)(uintptr_t)at);
			else glDrawArraysIndirect(mode, (const void*)(uintptr_t)at);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
			return true;
		}

		// ---- GPU 가 정하는 그리기 (오클루전 컬링): 인자 배치 = D3D 와 같다. ES 에는 첫 인스턴스가 없다 (그 자리는 0 이어야 한다)
		mutable int GpuDriven = -1;
		bool SupportsGpuDriven() const override
		{
			if (GpuDriven < 0 && Dev->Current())
			{
				// compute 커널 하나의 SSBO 수 (많아야 5) · 효과 하나의 SSBO 바인딩 번호 (이름마다 — 12 개) · image
				GLint blocks = 0, bindings = 0, images = 0;
				glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &blocks);
				glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &bindings);
				glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &images);
				GpuDriven = blocks >= 6 && bindings >= 12 && images >= 1 ? 1 : 0;
				EditorLog::Write("Gfx", "OpenGL ES: GPU-driven drawing %s (compute SSBO blocks %d, SSBO bindings %d, compute images %d)",
					GpuDriven ? "on" : "off", blocks, bindings, images);
			}
			return GpuDriven > 0;
		}
		bool DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset) override
		{
			GLBuf* a = BufOf(args);
			GLenum mode;
			if (!a || !Ib || IbOffset != 0 || (offset & 3) || !PrepareDraw(mode, 0)) return false;
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, a->Id);
			glDrawElementsIndirect(mode, IndexType(), (const void*)(uintptr_t)offset);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
			return true;
		}
		bool DrawInstancedIndirect(GfxBuffer* args, UINT offset) override
		{
			GLBuf* a = BufOf(args);
			GLenum mode;
			if (!a || (offset & 3) || !PrepareDraw(mode, 0)) return false;
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, a->Id);
			glDrawArraysIndirect(mode, (const void*)(uintptr_t)offset);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
			return true;
		}
		void DrawAuto() override { Dev->Once("drawauto", "%s", "OpenGL ES: DrawAuto (stream output) is not supported"); }
		void Dispatch(UINT x, UINT y, UINT z) override
		{
			if (!Dev->Check("Dispatch")) return;
			glDispatchCompute(x, y, z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);
		}

		// ---- 지우기 (D3D: 쓰기 마스크 · 가위와 상관없다)
		void ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT c[4]) override
		{
			if (!rtv || !Dev->Check("ClearRenderTargetView")) return;
			const ViewInfo& v = static_cast<GLRtv*>(rtv)->V;
			glBindFramebuffer(GL_FRAMEBUFFER, Dev->ClearFbo);
			Attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &v);
			const GLenum buf = GL_COLOR_ATTACHMENT0;
			glDrawBuffers(1, &buf);
			glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			if (v.Tex && v.Tex->Fmt.Integer)
			{
				const GLuint u[4] = { (GLuint)c[0], (GLuint)c[1], (GLuint)c[2], (GLuint)c[3] };
				glClearBufferuiv(GL_COLOR, 0, u);
			}
			else glClearBufferfv(GL_COLOR, 0, c);
			Attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr);
			if (v.Tex) ++v.Tex->Version;
			RebindDraw();
			ApplyBlend();
			ApplyRasterizer();
		}
		void ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) override
		{
			if (!dsv || !Dev->Check("ClearDepthStencilView")) return;
			const ViewInfo& v = static_cast<GLDsv*>(dsv)->V;
			glBindFramebuffer(GL_FRAMEBUFFER, Dev->ClearFbo);
			const GLenum att = DepthAttachment(&v);
			Attach(GL_FRAMEBUFFER, att, &v);
			const GLenum none = GL_NONE;
			glDrawBuffers(1, &none);
			glDepthMask(GL_TRUE);
			glStencilMask(0xFF);
			glDisable(GL_SCISSOR_TEST);
			const bool d = (flags & D3D11_CLEAR_DEPTH) != 0, s = (flags & D3D11_CLEAR_STENCIL) != 0 && v.Tex && v.Tex->Fmt.Stencil;
			if (d && s) glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
			else if (d) glClearBufferfv(GL_DEPTH, 0, &depth);
			else if (s) { const GLint sv = stencil; glClearBufferiv(GL_STENCIL, 0, &sv); }
			Attach(GL_FRAMEBUFFER, att, nullptr);
			if (v.Tex) ++v.Tex->Version;
			RebindDraw();
			ApplyDepthStencil();
			ApplyRasterizer();
		}

		// ---- 자원
		HRESULT Map(GfxResource* r, UINT sub, D3D11_MAP type, UINT, D3D11_MAPPED_SUBRESOURCE* m) override
		{
			if (!m || !Dev->Check("Map")) return E_FAIL;
			*m = {};
			if (GLBuf* b = BufOf(r))
			{
				if (type == D3D11_MAP_WRITE_DISCARD && !b->Shadow.empty())
				{
					b->ShadowMapped = true;
					m->pData = b->Shadow.data();
					m->RowPitch = m->DepthPitch = b->Desc.ByteWidth;
					return S_OK;
				}
				GLbitfield access = 0;
				switch (type)
				{
				case D3D11_MAP_WRITE_DISCARD: access = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT; break;
				case D3D11_MAP_WRITE_NO_OVERWRITE: access = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT; break;
				case D3D11_MAP_READ: access = GL_MAP_READ_BIT; break;
				case D3D11_MAP_READ_WRITE: access = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT; break;
				default: access = GL_MAP_WRITE_BIT; break;
				}
				glBindBuffer(GL_COPY_WRITE_BUFFER, b->Id);
				void* p = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, b->Desc.ByteWidth, access);
				if (!p) return E_FAIL;
				b->MappedNow = true;
				m->pData = p;
				m->RowPitch = m->DepthPitch = b->Desc.ByteWidth;
				return S_OK;
			}
			TexInfo* t = TexOf(r);
			if (!t) return E_INVALIDARG;
			const UINT mip = sub % t->Mips, layer = sub / t->Mips;
			const UINT w = t->MipW(mip), h = t->MipH(mip), d = t->MipD(mip);
			const UINT64 row = GLESState::RowBytes(t->Fmt, w), slice = GLESState::SliceBytes(t->Fmt, w, h);
			std::vector<uint8_t>& buf = t->Mapped[sub];
			buf.assign((size_t)(slice * d), 0);
			t->MapType[sub] = type;
			if ((type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE) && !ReadTexture(*t, mip, layer, buf.data()))
				Dev->Once("map-read", "%s", "OpenGL ES: reading this texture back is not supported (zeros)");
			m->pData = buf.data();
			m->RowPitch = (UINT)row;
			m->DepthPitch = (UINT)slice;
			return S_OK;
		}

		// 텍스처 밉 하나 · 조각 하나 → CPU (색 · 비압축 · 바꾸기 없는 형식만: 프레임버퍼에 붙여 glReadPixels)
		bool ReadTexture(TexInfo& t, UINT mip, UINT layer, uint8_t* out)
		{
			if (t.Fmt.BlockBytes || t.Fmt.Depth || t.Fmt.Conv != GLESState::Convert::None || t.Target == GL_TEXTURE_3D) return false;
			glBindFramebuffer(GL_READ_FRAMEBUFFER, Dev->ReadFbo);
			ViewInfo v;
			v.Name = t.Id;
			v.Target = t.Target;
			v.Level = mip;
			v.Layer = t.Target == GL_TEXTURE_2D ? -1 : (int)layer;
			v.Tex = &t;
			Attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &v);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, t.MipW(mip), t.MipH(mip), t.Fmt.Upload, t.Fmt.Type, out);   // 행 0 = 텍스처 행 0 = D3D 의 위
			Attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, TargetsBound ? Dev->DrawFbo : 0);
			return glGetError() == GL_NO_ERROR;
		}

		void Unmap(GfxResource* r, UINT sub) override
		{
			if (!Dev->Check("Unmap")) return;
			if (GLBuf* b = BufOf(r))
			{
				glBindBuffer(GL_COPY_WRITE_BUFFER, b->Id);
				if (b->ShadowMapped)
				{
					glBufferSubData(GL_COPY_WRITE_BUFFER, 0, b->Desc.ByteWidth, b->Shadow.data());
					b->ShadowMapped = false;
					return;
				}
				if (b->MappedNow) glUnmapBuffer(GL_COPY_WRITE_BUFFER);
				b->MappedNow = false;
				return;
			}
			TexInfo* t = TexOf(r);
			if (!t) return;
			auto it = t->Mapped.find(sub);
			if (it == t->Mapped.end()) return;
			if (t->MapType[sub] != D3D11_MAP_READ)
			{
				const UINT mip = sub % t->Mips;
				const UINT w = t->MipW(mip), h = t->MipH(mip);
				Dev->Upload(*t, sub, nullptr, it->second.data(), (UINT)GLESState::RowBytes(t->Fmt, w), (UINT)GLESState::SliceBytes(t->Fmt, w, h));
			}
			t->Mapped.erase(it);
			t->MapType.erase(sub);
		}
		void UpdateSubresource(GfxResource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT row, UINT depth) override
		{
			if (!data || !Dev->Check("UpdateSubresource")) return;
			if (GLBuf* b = BufOf(r))
			{
				const UINT off = box ? box->left : 0;
				const UINT size = box ? box->right - box->left : b->Desc.ByteWidth;
				glBindBuffer(GL_COPY_WRITE_BUFFER, b->Id);
				glBufferSubData(GL_COPY_WRITE_BUFFER, off, size, data);
				if (!b->Shadow.empty() && off + size <= b->Shadow.size()) memcpy(b->Shadow.data() + off, data, size);
				return;
			}
			if (TexInfo* t = TexOf(r))
				Dev->Upload(*t, sub, box, data, row, depth);
		}
		void CopyResource(GfxResource* dst, GfxResource* src) override
		{
			if (!Dev->Check("CopyResource")) return;
			GLBuf* bd = BufOf(dst), * bs = BufOf(src);
			if (bd && bs)
			{
				glBindBuffer(GL_COPY_READ_BUFFER, bs->Id);
				glBindBuffer(GL_COPY_WRITE_BUFFER, bd->Id);
				glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, (std::min)(bs->Desc.ByteWidth, bd->Desc.ByteWidth));
				return;
			}
			TexInfo* td = TexOf(dst), * ts = TexOf(src);
			if (!td || !ts) return;
			for (UINT m = 0; m < (std::min)(td->Mips, ts->Mips); ++m)
				glCopyImageSubData(ts->Id, ts->Target, m, 0, 0, 0, td->Id, td->Target, m, 0, 0, 0, ts->MipW(m), ts->MipH(m),
					ts->Target == GL_TEXTURE_3D ? ts->MipD(m) : ts->Layers);
			++td->Version;
		}
		void CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box) override
		{
			if (!Dev->Check("CopySubresourceRegion")) return;
			GLBuf* bd = BufOf(dst), * bs = BufOf(src);
			if (bd && bs)
			{
				const UINT off = box ? box->left : 0, size = box ? box->right - box->left : bs->Desc.ByteWidth;
				glBindBuffer(GL_COPY_READ_BUFFER, bs->Id);
				glBindBuffer(GL_COPY_WRITE_BUFFER, bd->Id);
				glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, off, x, size);
				return;
			}
			TexInfo* td = TexOf(dst), * ts = TexOf(src);
			if (!td || !ts) return;
			const UINT sm = srcSub % ts->Mips, sl = srcSub / ts->Mips, dm = dstSub % td->Mips, dl = dstSub / td->Mips;
			const UINT bx = box ? box->left : 0, by = box ? box->top : 0, bz = box ? box->front : 0;
			const UINT w = box ? box->right - box->left : ts->MipW(sm), h = box ? box->bottom - box->top : ts->MipH(sm), d = box ? box->back - box->front : ts->MipD(sm);
			const bool s3 = ts->Target == GL_TEXTURE_3D, d3 = td->Target == GL_TEXTURE_3D;
			glCopyImageSubData(ts->Id, ts->Target, sm, bx, by, s3 ? bz : sl, td->Id, td->Target, dm, x, y, d3 ? z : dl, w, h, s3 ? d : 1);
			++td->Version;
		}
		void GenerateMips(GfxShaderResourceView* srv) override
		{
			if (!srv || !Dev->Check("GenerateMips")) return;
			ViewInfo& v = static_cast<GLSrv*>(srv)->V;
			if (!v.Tex) return;
			Dev->BindScratch(v.Tex->Target, v.Tex->Id);
			glGenerateMipmap(v.Tex->Target);
			Dev->UnbindScratch(v.Tex->Target);
			++v.Tex->Version;
		}

		// ---- 쿼리 (ES: 가림 = 보였나 0/1, 타임스탬프 없음)
		void Begin(GfxQuery* q) override
		{
			auto* g = static_cast<GLQueryObj*>(q);
			if (g && g->D.Query == D3D11_QUERY_OCCLUSION && g->Id && Dev->Check("Begin")) glBeginQuery(GL_ANY_SAMPLES_PASSED, g->Id);
		}
		void End(GfxQuery* q) override
		{
			auto* g = static_cast<GLQueryObj*>(q);
			if (!g || !Dev->Check("End")) return;
			if (g->D.Query == D3D11_QUERY_OCCLUSION && g->Id) glEndQuery(GL_ANY_SAMPLES_PASSED);
			else if (g->D.Query == D3D11_QUERY_EVENT)
			{
				if (g->Fence) glDeleteSync(g->Fence);
				g->Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			}
			g->Issued = true;
		}
		HRESULT GetData(GfxQuery* q, void* data, UINT size, UINT) override
		{
			auto* g = static_cast<GLQueryObj*>(q);
			if (!g || !Dev->Check("GetData")) return E_FAIL;
			switch (g->D.Query)
			{
			case D3D11_QUERY_TIMESTAMP_DISJOINT:
				if (data && size >= sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT))
					*static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data) = { 1000000000ull, TRUE };   // 끊김 = 시간 값 쓰지 말 것
				return S_OK;
			case D3D11_QUERY_OCCLUSION:
			{
				if (!g->Issued || !g->Id) return S_FALSE;
				GLuint ready = 0;
				glGetQueryObjectuiv(g->Id, GL_QUERY_RESULT_AVAILABLE, &ready);
				if (!ready) return S_FALSE;
				GLuint v = 0;
				glGetQueryObjectuiv(g->Id, GL_QUERY_RESULT, &v);
				if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = v;
				return S_OK;
			}
			case D3D11_QUERY_EVENT:
				// 펜스가 신호되었나 (기다리지 않는다). End 를 안 했으면 아직
				if (g->Fence)
				{
					GLint status = GL_UNSIGNALED;
					glGetSynciv(g->Fence, GL_SYNC_STATUS, 1, nullptr, &status);   // bufSize = 정수 칸 수 (바이트 수가 아니다 — 4 를 주면 4 칸을 쓸 수 있다)
					if (status != GL_SIGNALED) return S_FALSE;
				}
				else if (!g->Issued)
					return S_FALSE;
				if (data && size >= sizeof(BOOL)) *static_cast<BOOL*>(data) = TRUE;
				return S_OK;
			default:
				if (data && size) memset(data, 0, size);
				return S_OK;
			}
		}
		void Flush() override { if (Dev->Check("Flush")) glFlush(); }
		void ClearState() override
		{
			Layout = nullptr;
			Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			for (auto& v : Vbs) v = VB();
			Ib = nullptr;
			for (auto& r : Rtvs) r = nullptr;
			RtvCount = 0;
			Dsv = nullptr;
			Ds = nullptr;
			Bs = nullptr;
			Rs = nullptr;
			StencilRef = 0;
			SampleMask = 0xFFFFFFFF;
			for (float& f : BlendFactor) f = 1.0f;
			ViewportCount = ScissorCount = 0;
			TargetsBound = false;
			PredBuf = nullptr;
			if (Dev->Check("ClearState"))
			{
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				GLESState::ApplyDefaults();
			}
		}
	};

	void GLDev::GetImmediateContext(GfxContext** out)
	{
		if (!Immediate)
		{
			auto* c = new GLCtx();
			c->Dev = this;
			Immediate = c;
			*out = c;
			return;
		}
		Immediate->AddRef();
		*out = Immediate;
	}

	ComPtr<GfxDevice> s_Device;
	ComPtr<GfxContext> s_Context;
}

unsigned int GfxGLES_ResolveView(GfxShaderResourceView* view, unsigned int* target)
{
	if (!view || view->Api() != GfxApi::OpenGL) return 0;
	auto* v = static_cast<GLSrv*>(view);
	if (v->V.Copy) v->Dev->RefreshCopy(v->V);
	if (target) *target = v->V.Target;
	return v->V.Name;
}

unsigned int GfxGL_TextureName(GfxShaderResourceView* view)   // GLShared.h (데스크톱과 같은 이름)
{
	return GfxGLES_ResolveView(view, nullptr);
}

// GLESRhi 가 SSBO · image 를 묶을 때 (GLShared.h — 데스크톱과 같은 이름)
bool GfxGL_BufferRange(GfxShaderResourceView* view, unsigned& buffer, unsigned& offset, unsigned& size)
{
	if (!view || view->Api() != GfxApi::OpenGL) return false;
	const ViewInfo& v = static_cast<GLSrv*>(view)->V;
	buffer = v.Buffer;
	offset = v.BufferOffset;
	size = v.BufferSize;
	return v.Buffer != 0;
}

bool GfxGL_UavBuffer(GfxUnorderedAccessView* view, unsigned& buffer, unsigned& offset, unsigned& size)
{
	if (!view || view->Api() != GfxApi::OpenGL) return false;
	const ViewInfo& v = static_cast<GLUav*>(view)->V;
	buffer = v.Buffer;
	offset = v.BufferOffset;
	size = v.BufferSize;
	return v.Buffer != 0;
}

bool GfxGL_UavImage(GfxUnorderedAccessView* view, unsigned& texture, unsigned& level, unsigned& format)
{
	if (!view || view->Api() != GfxApi::OpenGL) return false;
	const ViewInfo& v = static_cast<GLUav*>(view)->V;
	texture = v.Name;
	level = v.Level;
	format = v.ImageFormat;
	return v.Name != 0 && v.ImageFormat != 0;
}

std::unique_ptr<Rhi::Device> CreateGlesRhiDeviceOnGfx(GfxDevice* sinkDevice, GfxContext* sinkContext, std::string& error);   // GLESRhi.cpp

namespace GfxGLES
{
	bool CreateDevice(GfxDevice** device, GfxContext** context, std::string& error)
	{
		*device = nullptr;
		*context = nullptr;
		const EGLContext ctx = eglGetCurrentContext();
		if (ctx == EGL_NO_CONTEXT || !glGetString(GL_VERSION)) { error = "no current OpenGL ES context"; return false; }
		auto* d = new GLDev();
		d->Ctx = ctx;
		GLint units = 0;
		glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &units);
		d->ScratchUnit = (GLuint)(std::max)(1, units - 1);
		glGenVertexArrays(1, &d->EmptyVao);
		GLuint fbos[4];
		glGenFramebuffers(4, fbos);
		d->ClearFbo = fbos[0];
		d->DrawFbo = fbos[1];
		d->BlitFbo = fbos[2];
		d->ReadFbo = fbos[3];
		GLESState::ApplyDefaults();
		auto* c = new GLCtx();
		c->Dev = d;           // 컨텍스트가 장치를 잡는다
		d->Immediate = c;     // 장치는 약하게
		*device = d;
		*context = c;
		EditorLog::Write("Gfx", "OpenGL ES device: %s / %s, scratch unit %u", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER), d->ScratchUnit);
		return true;
	}

	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error)
	{
		return CreateGlesRhiDeviceOnGfx(device, context, error);
	}

	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight)
	{
		auto* d = static_cast<GLDev*>(device);
		if (!d->Check("Present")) return;
		TexInfo* t = TexOf(backBuffer);
		if (!t) return;
		// 백버퍼 행 0 = 화면 위 → GL 기본 프레임버퍼는 아래가 행 0 이므로 위아래를 뒤집어 복사
		glBindFramebuffer(GL_READ_FRAMEBUFFER, d->BlitFbo);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->Id, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glDisable(GL_SCISSOR_TEST);
		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glBlitFramebuffer(0, 0, (GLint)t->Width, (GLint)t->Height, 0, windowHeight, windowWidth, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
		if (d->Immediate)
		{
			d->Immediate->RebindDraw();
			d->Immediate->ApplyBlend();
			d->Immediate->ApplyRasterizer();
		}
	}

	void RestoreState(GfxContext* context)
	{
		auto* c = static_cast<GLCtx*>(context);
		if (!c || !c->Dev->Check("RestoreState")) return;
		if (c->TargetsBound) c->BindTargets();
		if (c->ViewportCount)
		{
			const D3D11_VIEWPORT& v = c->Viewports[0];
			glViewport((GLint)v.TopLeftX, (GLint)v.TopLeftY, (GLsizei)v.Width, (GLsizei)v.Height);
			glDepthRangef(v.MinDepth, v.MaxDepth);
		}
		if (c->ScissorCount)
		{
			const D3D11_RECT& r = c->Scissors[0];
			glScissor(r.left, r.top, (std::max)(0, r.right - r.left), (std::max)(0, r.bottom - r.top));
		}
		c->ApplyRasterizer();
		c->ApplyBlend();
		c->ApplyDepthStencil();
	}

	bool IsFormatSupported(DXGI_FORMAT format)
	{
		return GLESState::FromDxgi(format, false).Internal != 0;
	}
}

// ---- Gfx 공용 함수 (Windows 에서는 GfxDx11.cpp)
namespace Gfx
{
	GfxDevice* Device() { return s_Device.Get(); }
	GfxContext* Context() { return s_Context.Get(); }
	void SetMain(GfxDevice* device, GfxContext* context)
	{
		s_Device = device;
		s_Context = context;
	}

	GfxObject* WrapD3D11(IUnknown*) { return nullptr; }

	HRESULT CreateTexture(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out)
	{
		*out = nullptr;
		std::vector<D3D11_SUBRESOURCE_DATA> data;
		if (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
		{
			size_t index = 0;
			for (size_t m = 0; m < meta.mipLevels && index < count; ++m)
			{
				const DirectX::Image& img = images[index];
				data.push_back({ img.pixels, (UINT)img.rowPitch, (UINT)img.slicePitch });
				index += (std::max<size_t>)(1, meta.depth >> m);
			}
		}
		else
		{
			const size_t subs = meta.arraySize * meta.mipLevels;
			if (count < subs) return E_INVALIDARG;
			for (size_t i = 0; i < subs; ++i)
				data.push_back({ images[i].pixels, (UINT)images[i].rowPitch, (UINT)images[i].slicePitch });
		}
		switch (meta.dimension)
		{
		case DirectX::TEX_DIMENSION_TEXTURE1D:
		{
			D3D11_TEXTURE1D_DESC d = { (UINT)meta.width, (UINT)meta.mipLevels, (UINT)meta.arraySize, meta.format, usage, bindFlags, cpuAccess, miscFlags };
			GfxTexture1D* t = nullptr;
			const HRESULT hr = device->CreateTexture1D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		case DirectX::TEX_DIMENSION_TEXTURE3D:
		{
			D3D11_TEXTURE3D_DESC d = { (UINT)meta.width, (UINT)meta.height, (UINT)meta.depth, (UINT)meta.mipLevels, meta.format, usage, bindFlags, cpuAccess, miscFlags };
			GfxTexture3D* t = nullptr;
			const HRESULT hr = device->CreateTexture3D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		default:
		{
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = (UINT)meta.width;
			d.Height = (UINT)meta.height;
			d.MipLevels = (UINT)meta.mipLevels;
			d.ArraySize = (UINT)meta.arraySize;
			d.Format = meta.format;
			d.SampleDesc.Count = 1;
			d.Usage = usage;
			d.BindFlags = bindFlags;
			d.CPUAccessFlags = cpuAccess;
			d.MiscFlags = miscFlags | (meta.IsCubemap() ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);
			GfxTexture2D* t = nullptr;
			const HRESULT hr = device->CreateTexture2D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		}
	}

	HRESULT CreateShaderResourceView(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta, GfxShaderResourceView** out)
	{
		*out = nullptr;
		ComPtr<GfxResource> tex;
		HRESULT hr = CreateTexture(device, images, count, meta, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, tex.GetAddressOf());
		if (FAILED(hr)) return hr;
		return device->CreateShaderResourceView(tex.Get(), nullptr, out);
	}

	// 텍스처 → CPU 이미지 (검사 · 스크린샷): 2D 밉 0, 색 · 비압축
	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out)
	{
		auto* c = static_cast<GLCtx*>(context);
		TexInfo* t = TexOf(texture);
		if (!t || !c->Dev->Check("CaptureTexture")) return E_INVALIDARG;
		if (t->Target != GL_TEXTURE_2D) return E_NOTIMPL;
		HRESULT hr = out.Initialize2D(t->Dxgi, t->Width, t->Height, 1, 1);
		if (FAILED(hr)) return hr;
		const DirectX::Image* img = out.GetImage(0, 0, 0);
		if (!c->ReadTexture(*t, 0, 0, img->pixels)) return E_NOTIMPL;
		return S_OK;
	}
}
