#include "pch.h"
#include "GfxGL.h"
#include "GLContext.h"
#include "GLLoader.h"
#include "GLShared.h"
#include "GLState.h"
#include "Rhi.h"

// Gfx(D3D11 모양) 층의 OpenGL 4.5 구현 (DSA).
//  - 좌표: GLContext 가 glClipControl(LOWER_LEFT, ZERO_TO_ONE), 셰이더는 -fvk-invert-y → 텍스처 행 0 = D3D 의 위,
//    창 y = D3D 의 행 번호 (뷰포트·가위 숫자 그대로). 화면 표시는 Present 가 뒤집어 복사
//  - 수명: 모든 GL 객체는 장치를 잡는다 → 장치(=GL 컨텍스트)는 마지막 객체가 사라진 뒤에 없어진다.
//    컨텍스트는 장치를 잡고, 장치는 컨텍스트를 약하게 가리킨다 (순환 없음)
//  - 셰이더 자원 뷰 = GL 텍스처 뷰 (glTextureView): 밉·조각 범위, 2D 배열 ↔ 큐브, 형식 바꿔 읽기 (D3D 의 SRV 와 같은 뜻)
//  - VRAM 예산 가드: 이 장치가 만든 텍스처·버퍼 바이트를 세어 예산을 넘으면 만들지 않는다 (2026-10-02 사고 대비)
namespace
{
	class GLDev;

	// ---- 공통: 장치를 잡는 객체
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
		GLState::Format Fmt;
		DXGI_FORMAT Dxgi = DXGI_FORMAT_UNKNOWN;
		UINT Width = 1, Height = 1, Depth = 1;   // Depth = 3D 깊이
		UINT Layers = 1;                          // 배열 조각 (큐브 = 6 의 배수)
		UINT Mips = 1;
		UINT Samples = 1;
		UINT64 Bytes = 0;
		D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
		UINT CpuAccess = 0;
		std::map<UINT, std::vector<uint8_t>> Mapped;   // Map 중인 서브리소스 → CPU 사본
		std::map<UINT, D3D11_MAP> MapType;
		UINT MipW(UINT m) const { return (std::max)(1u, Width >> m); }
		UINT MipH(UINT m) const { return (std::max)(1u, Height >> m); }
		UINT MipD(UINT m) const { return (std::max)(1u, Depth >> m); }
	};

	TexInfo* TexOf(GfxResource* r);

	class GLBuf : public GLObj<GfxBuffer>
	{
	public:
		using GLObj::GLObj;
		~GLBuf() override;
		void GetDesc(D3D11_BUFFER_DESC* d) const override { *d = Desc; }
		D3D11_BUFFER_DESC Desc = {};
		GLuint Id = 0;
		bool MappedNow = false;
		// D3D11_USAGE_DYNAMIC: Map(WRITE_DISCARD) 는 이 CPU 사본을 주고 Unmap 때 glNamedBufferSubData 로 한 번에 올린다.
		// GL 버퍼를 직접 매핑하면 GPU 가 앞 그리기에서 그 버퍼를 다 쓸 때까지 기다리거나(고정 저장소),
		// 고아 만들기(glNamedBufferData)의 비용이 그리기 호출로 넘어가 GL CPU 시간의 큰 몫이었다
		std::vector<uint8_t> Shadow;
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

	// 뷰 공통: 자원을 잡고, 필요하면 자기 GL 텍스처 뷰를 가진다
	struct ViewInfo
	{
		ComPtr<GfxResource> Res;
		TexInfo* Tex = nullptr;
		GLuint Name = 0;          // 셰이더·붙이기에 쓸 이름 (텍스처 자체 또는 뷰)
		bool OwnsName = false;    // glTextureView 로 만든 이름
		GLenum Target = 0;
		UINT Level = 0;           // 붙이기: Name 기준 밉
		int Layer = -1;           // 붙이기: -1 = 전체(겹) / 0.. = 그 조각 (Name 기준)
		// 버퍼 뷰 (구조 · raw 버퍼 SRV · UAV = SSBO): Buffer = GL 버퍼, 바이트 범위
		GLuint Buffer = 0;
		UINT BufferOffset = 0, BufferSize = 0;
		// 텍스처 UAV = image (glBindImageTexture): 텍스처 · 밉 · 형식
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
		GLState::VaoCache Cache;  // 이 VAO 에 지정한 정점·인덱스 버퍼 (같으면 다시 지정하지 않는다)
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
		bool Issued = false;   // End 로 GL 쿼리를 한 번이라도 넣었나 (안 넣은 쿼리의 결과를 물으면 GL 오류)
		GLsync Fence = nullptr;   // D3D11_QUERY_EVENT: End 때 펜스 (그 앞 명령이 다 끝났나)
	};

	class GLCtx;

	// ---- 장치
	class GLDev : public GfxDevice
	{
	public:
		GLContext::Handle Ctx;
		DWORD OwnerThread = 0;
		GLCtx* Immediate = nullptr;     // 약한 참조 (컨텍스트가 장치를 잡는다)
		GLuint EmptyVao = 0, ClearFbo = 0, DrawFbo = 0, BlitFbo = 0;
		GLState::VaoCache EmptyVaoCache;
		UINT64 LiveBytes = 0, BudgetBytes = 0;
		int Refused = 0;
		std::set<std::string> Reported;

		~GLDev() override
		{
			if (Ctx.Rc && GLContext::MakeCurrent(Ctx))
			{
				GLuint fbos[] = { ClearFbo, DrawFbo, BlitFbo };
				glDeleteFramebuffers(3, fbos);
				if (EmptyVao) { glDeleteVertexArrays(1, &EmptyVao); GLState::InvalidateBindings(); }
			}
			GLContext::Destroy(Ctx);
		}

		bool Current() const { return Ctx.Rc && ::wglGetCurrentContext() == Ctx.Rc; }

		// 다른 스레드·다른 컨텍스트에서 부르면 거절 (GL 은 컨텍스트가 현재인 스레드에서만)
		bool Check(const char* what)
		{
			if (::GetCurrentThreadId() == OwnerThread)
			{
				// 같은 스레드에서 다른 GL 장치(검사용)가 현재가 된 뒤면 우리 컨텍스트로 되돌린다
				if (Current() || GLContext::MakeCurrent(Ctx))
					return true;
			}
			if (Reported.insert(std::string("thread:") + what).second)
				EditorLog::Write("Gfx", "OpenGL: %s called on another thread or without the context - refused", what);
			return false;
		}

		void Once(const std::string& key, const char* fmt, const char* arg)
		{
			if (Reported.insert(key).second)
				EditorLog::Write("Gfx", fmt, arg);
		}

		bool Reserve(UINT64 bytes, const char* what)
		{
			if (bytes >= (4ull << 20) && LiveBytes + bytes > BudgetBytes)
			{
				if (++Refused <= 20 || Refused % 1000 == 0)
					EditorLog::Write("Gfx", "OpenGL VRAM budget: refused %s of %.1f MB (live %.0f MB / budget %.0f MB, refused %d so far)", what,
						bytes / 1048576.0, LiveBytes / 1048576.0, BudgetBytes / 1048576.0, Refused);
				return false;
			}
			LiveBytes += bytes;
			return true;
		}
		void FreeBytes(UINT64 bytes) { LiveBytes = bytes > LiveBytes ? 0 : LiveBytes - bytes; }

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
			s->Id = GLState::CreateSampler(*desc);
			*out = s;
			return S_OK;
		}
		HRESULT CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out) override
		{
			if (!Check("CreateQuery")) return E_FAIL;
			auto* q = new GLQueryObj(this);
			q->D = *desc;
			// TIMESTAMP_DISJOINT 도 끝 시각을 하나 찍는다: D3D 처럼 GPU 가 그 프레임을 다 끝내야 "준비됨" 이 되도록
			// (예전에는 늘 바로 준비됨이라 Profiler 가 아직 안 끝난 타임스탬프를 읽다 실패해 GPU 시간이 자주 비었다)
			if (desc->Query == D3D11_QUERY_TIMESTAMP || desc->Query == D3D11_QUERY_TIMESTAMP_DISJOINT) glCreateQueries(GL_TIMESTAMP, 1, &q->Id);
			else if (desc->Query == D3D11_QUERY_OCCLUSION) glCreateQueries(GL_SAMPLES_PASSED, 1, &q->Id);
			else if (desc->Query == D3D11_QUERY_OCCLUSION_PREDICATE) glCreateQueries(GL_ANY_SAMPLES_PASSED, 1, &q->Id);
			*out = q;
			return S_OK;
		}
		void GetImmediateContext(GfxContext** out) override;
		HRESULT GetDeviceRemovedReason() override { return S_OK; }

		// 자원 데이터 올리기 (서브리소스 하나, box = 영역 또는 전체)
		void Upload(TexInfo& t, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
		HRESULT MakeTexture(TexInfo& t, const D3D11_SUBRESOURCE_DATA* data, const char* what);
	};

	template <class Iface>
	GLObj<Iface>::GLObj(GLDev* dev) : Dev(dev) { Dev->AddRef(); }
	template <class Iface>
	GLObj<Iface>::~GLObj() { Dev->GfxDevice::Release(); }

	// GL 이름 지우기는 컨텍스트가 현재일 때만 (아니면 새게 두고 기록 — 잘못된 컨텍스트에 GL 을 부르면 죽을 수 있다)
	bool CanDelete(GLDev* dev)
	{
		if (dev->Current()) return true;
		dev->Once("delete-no-context", "%s", "OpenGL: object released without its context - GL name leaked");
		return false;
	}

	GLBuf::~GLBuf()
	{
		if (Id && CanDelete(Dev)) { glDeleteBuffers(1, &Id); GLState::InvalidateBindings(); }
		Dev->FreeBytes(Desc.ByteWidth);
	}
	void DeleteTex(GLDev* dev, TexInfo& t)
	{
		if (t.Id && CanDelete(dev)) { glDeleteTextures(1, &t.Id); GLState::InvalidateBindings(); }
		dev->FreeBytes(t.Bytes);
	}
	GLTex1D::~GLTex1D() { DeleteTex(Dev, T); }
	GLTex2D::~GLTex2D() { DeleteTex(Dev, T); }
	GLTex3D::~GLTex3D() { DeleteTex(Dev, T); }
	template <class Iface, class Desc>
	GLView<Iface, Desc>::~GLView()
	{
		if (V.OwnsName && V.Name && CanDelete(this->Dev)) { glDeleteTextures(1, &V.Name); GLState::InvalidateBindings(); }
	}
	GLLayout::~GLLayout() { if (Vao && CanDelete(Dev)) { glDeleteVertexArrays(1, &Vao); GLState::InvalidateBindings(); } }
	GLSampler::~GLSampler() { if (Id && CanDelete(Dev)) { glDeleteSamplers(1, &Id); GLState::InvalidateBindings(); } }
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
			b += GLState::SliceBytes(t.Fmt, t.MipW(m), t.MipH(m)) * t.MipD(m);
		return b * t.Layers * (std::max)(1u, t.Samples);
	}

	HRESULT GLDev::MakeTexture(TexInfo& t, const D3D11_SUBRESOURCE_DATA* data, const char* what)
	{
		if (!t.Fmt.Internal)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%d", (int)t.Dxgi);
			Once(std::string("fmt:") + buf, "OpenGL: unsupported texture format DXGI %s", buf);
			return E_INVALIDARG;
		}
		t.Bytes = TexBytes(t);
		if (!Reserve(t.Bytes, what))
			return E_OUTOFMEMORY;
		glCreateTextures(t.Target, 1, &t.Id);
		switch (t.Target)
		{
		case GL_TEXTURE_1D: glTextureStorage1D(t.Id, t.Mips, t.Fmt.Internal, t.Width); break;
		case GL_TEXTURE_1D_ARRAY: glTextureStorage2D(t.Id, t.Mips, t.Fmt.Internal, t.Width, t.Layers); break;
		case GL_TEXTURE_2D:
		case GL_TEXTURE_CUBE_MAP: glTextureStorage2D(t.Id, t.Mips, t.Fmt.Internal, t.Width, t.Height); break;
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_CUBE_MAP_ARRAY: glTextureStorage3D(t.Id, t.Mips, t.Fmt.Internal, t.Width, t.Height, t.Layers); break;
		case GL_TEXTURE_3D: glTextureStorage3D(t.Id, t.Mips, t.Fmt.Internal, t.Width, t.Height, t.Depth); break;
		case GL_TEXTURE_2D_MULTISAMPLE: glTextureStorage2DMultisample(t.Id, t.Samples, t.Fmt.Internal, t.Width, t.Height, GL_TRUE); break;
		default: break;
		}
		glTextureParameteri(t.Id, GL_TEXTURE_MAX_LEVEL, (GLint)t.Mips - 1);
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
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		if (t.Fmt.BlockBytes)
		{
			// 압축: 블록 행 단위. rowPitch 가 꽉 찬 블록 행과 같다고 본다 (DirectXTex 가 만드는 데이터)
			const UINT64 size = (UINT64)rowPitch * (std::max)(1u, (h + 3) / 4);
			if (t.Target == GL_TEXTURE_2D)
				glCompressedTextureSubImage2D(t.Id, m, x, y, w, h, t.Fmt.Internal, (GLsizei)size, data);
			else
				glCompressedTextureSubImage3D(t.Id, m, x, y, t.Target == GL_TEXTURE_3D ? z : layer, w, h, t.Target == GL_TEXTURE_3D ? d : 1, t.Fmt.Internal, (GLsizei)size, data);
			return;
		}
		const UINT bpp = (std::max)(1u, t.Fmt.Bits / 8);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rowPitch ? rowPitch / bpp : 0);
		glPixelStorei(0x806E /* GL_UNPACK_IMAGE_HEIGHT */, (rowPitch && depthPitch) ? depthPitch / rowPitch : 0);
		switch (t.Target)
		{
		case GL_TEXTURE_1D: glTextureSubImage1D(t.Id, m, x, w, t.Fmt.Upload, t.Fmt.Type, data); break;
		case GL_TEXTURE_1D_ARRAY: glTextureSubImage2D(t.Id, m, x, layer, w, 1, t.Fmt.Upload, t.Fmt.Type, data); break;
		case GL_TEXTURE_2D: glTextureSubImage2D(t.Id, m, x, y, w, h, t.Fmt.Upload, t.Fmt.Type, data); break;
		case GL_TEXTURE_3D: glTextureSubImage3D(t.Id, m, x, y, z, w, h, d, t.Fmt.Upload, t.Fmt.Type, data); break;
		default: glTextureSubImage3D(t.Id, m, x, y, layer, w, h, 1, t.Fmt.Upload, t.Fmt.Type, data); break;   // 배열·큐브 = 조각
		}
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glPixelStorei(0x806E, 0);
	}

	HRESULT GLDev::CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!desc || desc->ByteWidth == 0) return E_INVALIDARG;
		if (!Check("CreateBuffer")) return E_FAIL;
		if (!Reserve(desc->ByteWidth, "buffer")) return E_OUTOFMEMORY;
		auto* b = new GLBuf(this);
		b->Desc = *desc;
		glCreateBuffers(1, &b->Id);
		GLbitfield flags = GL_DYNAMIC_STORAGE_BIT;
		if (desc->CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) flags |= GL_MAP_WRITE_BIT;
		if (desc->CPUAccessFlags & D3D11_CPU_ACCESS_READ) flags |= GL_MAP_READ_BIT;
		if (desc->Usage == D3D11_USAGE_STAGING) flags |= GL_CLIENT_STORAGE_BIT;
		glNamedBufferStorage(b->Id, desc->ByteWidth, data ? data->pSysMem : nullptr, flags);
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
		i.Fmt = GLState::FromDxgi(desc->Format, (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0);
		i.Width = desc->Width;
		i.Layers = (std::max)(1u, desc->ArraySize);
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, 1, 1);
		t->Desc.MipLevels = i.Mips;
		i.Target = i.Layers > 1 ? GL_TEXTURE_1D_ARRAY : GL_TEXTURE_1D;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		const HRESULT hr = MakeTexture(i, data, "texture1D");
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
		i.Fmt = GLState::FromDxgi(desc->Format, (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0);
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Layers = (std::max)(1u, desc->ArraySize);
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, 1);
		t->Desc.MipLevels = i.Mips;
		i.Samples = (std::max)(1u, desc->SampleDesc.Count);
		const bool cube = (desc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
		if (i.Samples > 1) i.Target = GL_TEXTURE_2D_MULTISAMPLE;
		else if (cube) i.Target = i.Layers == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_CUBE_MAP_ARRAY;
		else i.Target = i.Layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		const HRESULT hr = MakeTexture(i, data, "texture2D");
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
		i.Fmt = GLState::FromDxgi(desc->Format, false);
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Depth = desc->Depth;
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, desc->Depth);
		t->Desc.MipLevels = i.Mips;
		i.Target = GL_TEXTURE_3D;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		const HRESULT hr = MakeTexture(i, data, "texture3D");
		if (FAILED(hr)) { delete t; return hr; }
		*out = t;
		return S_OK;
	}

	// 뷰: [밉 minLevel, numLevels] [조각 minLayer, numLayers] 를 target 으로 읽는 GL 이름 (전체·같은 모양이면 텍스처 그대로)
	bool MakeViewName(GLDev* dev, TexInfo& t, GLenum target, DXGI_FORMAT viewFormat, UINT minLevel, UINT numLevels, UINT minLayer, UINT numLayers, ViewInfo& v)
	{
		GLenum internal = t.Fmt.Internal;
		if (viewFormat != DXGI_FORMAT_UNKNOWN && !t.Fmt.Depth)
		{
			const GLState::Format f = GLState::FromDxgi(viewFormat, false);
			if (f.Internal) internal = f.Internal;
		}
		numLevels = (std::min)(numLevels, t.Mips - (std::min)(minLevel, t.Mips));
		numLayers = (std::min)(numLayers, t.Layers - (std::min)(minLayer, t.Layers));
		if (numLevels == 0 || numLayers == 0)
			return false;
		v.Tex = &t;
		v.Target = target;
		if (target == t.Target && internal == t.Fmt.Internal && minLevel == 0 && numLevels == t.Mips && minLayer == 0 && numLayers == t.Layers)
		{
			v.Name = t.Id;
			return true;
		}
		glGenTextures(1, &v.Name);   // glTextureView 는 한 번도 묶지 않은 이름이 필요하다 (glCreateTextures 는 안 됨)
		glTextureView(v.Name, target, t.Id, internal, minLevel, numLevels, minLayer, numLayers);
		v.OwnsName = true;
		(void)dev;
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
				Once("srv-buffer", "%s", "OpenGL: this buffer shader resource view is not supported (structured / raw only)");
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
			case GL_TEXTURE_1D: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D; d.Texture1D.MipLevels = t->Mips; break;
			case GL_TEXTURE_1D_ARRAY: d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY; d.Texture1DArray.MipLevels = t->Mips; d.Texture1DArray.ArraySize = t->Layers; break;
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
		case D3D11_SRV_DIMENSION_TEXTURE1D: target = GL_TEXTURE_1D; minLevel = d.Texture1D.MostDetailedMip; numLevels = mips(minLevel, d.Texture1D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: target = GL_TEXTURE_1D_ARRAY; minLevel = d.Texture1DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture1DArray.MipLevels);
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
			Once("srv-dim", "%s", "OpenGL: unsupported shader resource view dimension");
			return E_NOTIMPL;
		}
		auto* v = new GLSrv(this);
		v->D = d;
		v->V.Res = r;
		if (!MakeViewName(this, *t, target, d.Format, minLevel, numLevels, minLayer, numLayers, v->V))
		{
			v->Release();
			return E_INVALIDARG;
		}
		if (d.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT || d.Format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
		{
			// 스텐실을 읽는 뷰 (자기 GL 뷰가 있어야 모드를 바꿀 수 있다)
			if (!v->V.OwnsName)
			{
				glGenTextures(1, &v->V.Name);
				glTextureView(v->V.Name, target, t->Id, t->Fmt.Internal, minLevel, numLevels, minLayer, numLayers);
				v->V.OwnsName = true;
			}
			glTextureParameteri(v->V.Name, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX);
		}
		*out = v;
		return S_OK;
	}

	// 붙이기용 뷰 (렌더 타깃·깊이): 밉 하나, 조각 하나 또는 전체(겹)
	template <class V>
	HRESULT MakeAttachView(GLDev* dev, GfxResource* r, DXGI_FORMAT fmt, UINT mip, UINT firstLayer, UINT layers, bool layered, V* v)
	{
		TexInfo* t = TexOf(r);
		if (!t) return E_INVALIDARG;
		v->V.Res = r;
		v->V.Tex = t;
		GLenum internal = t->Fmt.Internal;
		if (fmt != DXGI_FORMAT_UNKNOWN && !t->Fmt.Depth)
		{
			const GLState::Format f = GLState::FromDxgi(fmt, false);
			if (f.Internal) internal = f.Internal;
		}
		if (internal != t->Fmt.Internal)
		{
			// 다른 형식으로 쓰기 (예: 타입 없는 RGBA8 텍스처에 _SRGB RTV) → 그 형식의 GL 뷰를 붙인다
			const GLenum target = t->Target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_2D_ARRAY : t->Target;
			glGenTextures(1, &v->V.Name);
			glTextureView(v->V.Name, target, t->Id, internal, mip, 1, layered ? 0 : firstLayer, layered ? t->Layers : 1);
			v->V.OwnsName = true;
			v->V.Level = 0;
			v->V.Layer = layered ? -1 : (t->Layers > 1 ? 0 : -1);
			v->V.Target = target;
			return S_OK;
		}
		v->V.Name = t->Id;
		v->V.Target = t->Target;
		v->V.Level = mip;
		v->V.Layer = (layered || (t->Layers <= 1 && t->Target != GL_TEXTURE_3D)) ? -1 : (int)firstLayer;
		(void)layers;
		(void)dev;
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
		const HRESULT hr = MakeAttachView(this, r, v->D.Format, mip, first, count, layered, v);
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
		UINT mip = 0, first = 0, count = t->Layers;
		bool layered = t->Layers > 1;
		if (desc)
		{
			v->D = *desc;
			switch (desc->ViewDimension)
			{
			case D3D11_DSV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; layered = false; count = 1; break;
			case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice; count = desc->Texture2DArray.ArraySize;
				layered = count > 1; break;
			default: break;
			}
		}
		else
		{
			v->D.Format = t->Dxgi;
			v->D.ViewDimension = t->Layers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
		}
		const HRESULT hr = MakeAttachView(this, r, DXGI_FORMAT_UNKNOWN, mip, first, count, layered, v);
		if (FAILED(hr)) { v->Release(); return hr; }
		*out = v;
		return S_OK;
	}

	HRESULT GLDev::CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!desc || !Check("CreateUnorderedAccessView")) return E_INVALIDARG;
		auto* v = new GLUav(this);
		v->D = *desc;
		v->V.Res = r;
		if (GLBuf* b = BufOf(r))
		{
			// 구조 · raw 버퍼 → SSBO (RWStructuredBuffer · RWByteAddressBuffer)
			if (desc->ViewDimension != D3D11_UAV_DIMENSION_BUFFER)
			{
				v->Release();
				return E_INVALIDARG;
			}
			const UINT stride = (desc->Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) ? 4 : (b->Desc.StructureByteStride ? b->Desc.StructureByteStride : 4);
			v->V.Buffer = b->Id;
			v->V.BufferOffset = desc->Buffer.FirstElement * stride;
			v->V.BufferSize = (std::min)(desc->Buffer.NumElements * stride, b->Desc.ByteWidth - (std::min)(v->V.BufferOffset, b->Desc.ByteWidth));
			*out = v;
			return S_OK;
		}
		TexInfo* t = TexOf(r);
		if (!t || desc->ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D)
		{
			Once("uav", "%s", "OpenGL: only buffer and 2D texture unordered access views are supported");
			v->Release();
			return E_NOTIMPL;
		}
		// 텍스처 → image 유닛 (밉 하나). 형식은 텍스처의 GL 내부 형식 (UAV 형식이 다르면 그 형식)
		v->V.Tex = t;
		v->V.Name = t->Id;
		v->V.Target = t->Target;
		v->V.Level = desc->Texture2D.MipSlice;
		GLenum internal = t->Fmt.Internal;
		if (desc->Format != DXGI_FORMAT_UNKNOWN)
		{
			const GLState::Format f = GLState::FromDxgi(desc->Format, false);
			if (f.Internal) internal = f.Internal;
		}
		v->V.ImageFormat = internal;
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
			Once("layout-sig", "%s", "OpenGL: CreateInputLayout needs a pass signature from FxPass::GetDesc");
			return E_INVALIDARG;
		}
		auto upper = [](std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; };
		auto location = [&](const std::string& sem) -> int {
			for (const auto& [s, l] : *sig->Inputs)
				if (s == sem) return l;
			return -1;
		};
		auto* l = new GLLayout(this);
		glCreateVertexArrays(1, &l->Vao);
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		std::set<std::string> provided;
		for (UINT i = 0; i < count; ++i)
		{
			const D3D11_INPUT_ELEMENT_DESC& e = elements[i];
			GLint size; GLenum type; GLboolean norm; bool integer; UINT bytes;
			if (!GLState::VertexFormat(e.Format, size, type, norm, integer, bytes))
			{
				Once(std::string("vfmt:") + e.SemanticName, "OpenGL: unsupported vertex format for %s", e.SemanticName);
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
				// 행렬 입력 (WORLD0..3): 셰이더는 WORLD0 하나에 location 4 칸
				const int first = location(base + "0");
				if (first >= 0) loc = first + (int)e.SemanticIndex;
			}
			if (loc < 0) continue;   // 셰이더가 쓰지 않는 요소 (D3D 와 같이 무시)
			glEnableVertexArrayAttrib(l->Vao, loc);
			if (integer) glVertexArrayAttribIFormat(l->Vao, loc, size, type, offset);
			else glVertexArrayAttribFormat(l->Vao, loc, size, type, norm, offset);
			glVertexArrayAttribBinding(l->Vao, loc, e.InputSlot);
			glVertexArrayBindingDivisor(l->Vao, e.InputSlot, e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA ? (std::max)(1u, e.InstanceDataStepRate) : 0);
			l->SlotMask |= 1u << e.InputSlot;
		}
		for (const auto& [s, loc] : *sig->Inputs)
		{
			if (provided.count(s)) continue;
			if (s.rfind("SV_", 0) == 0) continue;   // 시스템 값 (SV_VertexID …)
			Once("missing:" + s, "OpenGL: vertex input %s is not provided by the layout", s.c_str());
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
		// IA
		ComPtr<GfxInputLayout> Layout;
		D3D11_PRIMITIVE_TOPOLOGY Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		struct VB { ComPtr<GfxBuffer> Buf; UINT Stride = 0, Offset = 0; };
		VB Vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		ComPtr<GfxBuffer> Ib;
		DXGI_FORMAT IbFormat = DXGI_FORMAT_R16_UINT;
		UINT IbOffset = 0;
		// OM / RS
		ComPtr<GfxRenderTargetView> Rtvs[8];
		UINT RtvCount = 0;
		ComPtr<GfxDepthStencilView> Dsv;
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
			if (Dev && Dev->Immediate == this) Dev->Immediate = nullptr;
		}

		// ---- 상태 적용
		void ApplyDepthStencil()
		{
			D3D11_DEPTH_STENCIL_DESC d = Ds ? static_cast<GLDepthStencil*>(Ds.Get())->D : GLState::DefaultDepthStencil();
			// 읽기 전용 DSV: 깊이·스텐실 쓰기 끔 (D3D 는 쓰기가 오류)
			if (Dsv)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dd;
				Dsv->GetDesc(&dd);
				if (dd.Flags & D3D11_DSV_READ_ONLY_DEPTH) d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
				if (dd.Flags & D3D11_DSV_READ_ONLY_STENCIL) d.StencilWriteMask = 0;
			}
			GLState::ApplyDepthStencil(d, StencilRef);
		}
		void ApplyBlend()
		{
			const D3D11_BLEND_DESC d = Bs ? static_cast<GLBlend*>(Bs.Get())->D : GLState::DefaultBlend();
			GLState::ApplyBlend(d, BlendFactor, SampleMask);
		}
		void ApplyRasterizer()
		{
			const D3D11_RASTERIZER_DESC d = Rs ? static_cast<GLRasterizer*>(Rs.Get())->D : GLState::DefaultRasterizer();
			GLState::ApplyRasterizer(d);
		}

		// FBO 에 뷰 붙이기
		static void Attach(GLuint fbo, GLenum attachment, const ViewInfo* v)
		{
			if (!v || !v->Name)
			{
				glNamedFramebufferTexture(fbo, attachment, 0, 0);
				return;
			}
			if (v->Layer < 0)
				glNamedFramebufferTexture(fbo, attachment, v->Name, v->Level);
			else
				glNamedFramebufferTextureLayer(fbo, attachment, v->Name, v->Level, v->Layer);
		}
		static GLenum DepthAttachment(const ViewInfo* v) { return v && v->Tex && v->Tex->Fmt.Stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT; }

		void BindTargets()
		{
			const GLuint fbo = Dev->DrawFbo;
			GLenum bufs[8];
			for (UINT i = 0; i < 8; ++i)
			{
				const ViewInfo* v = i < RtvCount && Rtvs[i] ? &static_cast<GLRtv*>(Rtvs[i].Get())->V : nullptr;
				Attach(fbo, GL_COLOR_ATTACHMENT0 + i, v);
				bufs[i] = v ? GL_COLOR_ATTACHMENT0 + i : GL_NONE;
			}
			glNamedFramebufferDrawBuffers(fbo, 8, bufs);
			glNamedFramebufferTexture(fbo, GL_DEPTH_STENCIL_ATTACHMENT, 0, 0);
			glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, 0, 0);
			const ViewInfo* dv = Dsv ? &static_cast<GLDsv*>(Dsv.Get())->V : nullptr;
			if (dv) Attach(fbo, DepthAttachment(dv), dv);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
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
				Dev->Once("so", "%s", "OpenGL: stream output is not supported");
		}

		// ---- OM / RS
		void OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv) override
		{
			RtvCount = (std::min)(count, 8u);
			for (UINT i = 0; i < 8; ++i)
				Rtvs[i] = i < RtvCount && rtvs ? rtvs[i] : nullptr;
			const bool readOnlyChanged = (Dsv.Get() != dsv);
			Dsv = dsv;
			if (Dev->Check("OMSetRenderTargets"))
			{
				BindTargets();
				if (readOnlyChanged) ApplyDepthStencil();
				// 읽기 전용 DSV + 같은 깊이를 셰이더가 읽음 (물): D3D 는 정상이지만 GL 은 피드백 루프 →
				// 앞에서 쓴 깊이를 읽도록 텍스처 장벽 (없으면 지운 직후 값 1.0 을 읽어 물이 끝없이 깊어 보였다)
				if (dsv)
				{
					D3D11_DEPTH_STENCIL_VIEW_DESC dd;
					dsv->GetDesc(&dd);
					if (dd.Flags & (D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL))
						glTextureBarrier();
				}
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
		void RSSetViewports(UINT count, const D3D11_VIEWPORT* v) override
		{
			ViewportCount = (std::min)(count, 16u);
			for (UINT i = 0; i < ViewportCount; ++i)
			{
				Viewports[i] = v[i];
				if (Dev->Check("RSSetViewports"))
				{
					glViewportIndexedf(i, v[i].TopLeftX, v[i].TopLeftY, v[i].Width, v[i].Height);   // 창 y = D3D 행 번호
					glDepthRangeIndexed(i, v[i].MinDepth, v[i].MaxDepth);
				}
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
			for (UINT i = 0; i < ScissorCount; ++i)
			{
				Scissors[i] = r[i];
				if (Dev->Check("RSSetScissorRects"))
					glScissorIndexed(i, r[i].left, r[i].top, (std::max)(0L, r[i].right - r[i].left), (std::max)(0L, r[i].bottom - r[i].top));
			}
		}
		void RSGetScissorRects(UINT* count, D3D11_RECT* r) override
		{
			if (r) for (UINT i = 0; i < (std::min)(*count, ScissorCount); ++i) r[i] = Scissors[i];
			*count = ScissorCount;
		}

		// ---- 효과 밖 셰이더 자원 (GL 은 효과가 유닛을 묶는다 → 풀기(nullptr)만 받는다)
		void SetSrvs(const char* stage, UINT count, GfxShaderResourceView* const* views)
		{
			for (UINT i = 0; views && i < count; ++i)
				if (views[i]) { Dev->Once(std::string("srv:") + stage, "OpenGL: %sSetShaderResources with a view outside effects is ignored", stage); break; }
		}
		void PSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { SetSrvs("PS", count, views); }
		void VSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { SetSrvs("VS", count, views); }
		void CSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { SetSrvs("CS", count, views); }
		void CSSetUnorderedAccessViews(UINT, UINT, GfxUnorderedAccessView* const*, const UINT*) override {}
		void CSSetShader(void*, void*, UINT) override {}

		// ---- 그리기
		bool PrepareDraw(GLenum& mode)
		{
			if (!Dev->Check("Draw")) return false;
			GLint patch = 0;
			mode = GLState::Topology(Topo, patch);
			if (patch) glPatchParameteri(GL_PATCH_VERTICES, patch);
			GLuint vao = Dev->EmptyVao;
			GLState::VaoCache* cache = &Dev->EmptyVaoCache;
			UINT mask = 0;
			if (Layout)
			{
				auto* l = static_cast<GLLayout*>(Layout.Get());
				vao = l->Vao;
				mask = l->SlotMask;
				cache = &l->Cache;
			}
			// 그리기마다 VAO 의 버퍼 지정을 다시 하면 드라이버가 VAO 를 다시 검사한다 → 바뀐 것만 (VaoCache)
			for (UINT s = 0; mask; ++s, mask >>= 1)
				if (mask & 1)
				{
					const VB& v = Vbs[s];
					GLBuf* b = BufOf(v.Buf.Get());
					cache->VertexBuffer(vao, s, b ? b->Id : 0, v.Offset, v.Stride);
				}
			GLBuf* ib = BufOf(Ib.Get());
			cache->ElementBuffer(vao, ib ? ib->Id : 0);
			GLState::BindVertexArray(vao);
			return true;
		}
		GLenum IndexType() const { return IbFormat == DXGI_FORMAT_R32_UINT ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT; }
		UINT IndexSize() const { return IbFormat == DXGI_FORMAT_R32_UINT ? 4 : 2; }

		void Draw(UINT c, UINT s) override
		{
			GLenum mode;
			if (PrepareDraw(mode)) glDrawArraysInstancedBaseInstance(mode, s, c, 1, 0);
		}
		void DrawIndexed(UINT c, UINT s, INT b) override { DrawIndexedInstanced(c, 1, s, b, 0); }
		void DrawInstanced(UINT c, UINT n, UINT s, UINT si) override
		{
			GLenum mode;
			if (PrepareDraw(mode)) glDrawArraysInstancedBaseInstance(mode, s, c, n, si);
		}
		void DrawIndexedInstanced(UINT c, UINT n, UINT s, INT b, UINT si) override
		{
			GLenum mode;
			if (!Ib || !PrepareDraw(mode)) return;
			glDrawElementsInstancedBaseVertexBaseInstance(mode, c, IndexType(), (const void*)(uintptr_t)(IbOffset + (UINT64)s * IndexSize()), n, b, si);
		}
		void DrawAuto() override { Dev->Once("drawauto", "%s", "OpenGL: DrawAuto (stream output) is not supported"); }

		// ---- GPU 가 정하는 그리기 (오클루전 컬링): 인자 배치 = D3D 와 같다 (GL 4.2+ 의 baseInstance 포함)
		bool SupportsGpuDriven() const override { return true; }   // GL 4.5 컨텍스트 (compute · SSBO · image · 간접 그리기 · 조건부 그리기)
		bool DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset) override
		{
			GLBuf* a = BufOf(args);
			GLenum mode;
			if (!a || !Ib || !PrepareDraw(mode)) return false;
			if (IbOffset != 0)
			{
				Dev->Once("indirect-iboffset", "%s", "OpenGL: indirect draw with an index buffer offset is not supported");
				return false;
			}
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, a->Id);
			glDrawElementsIndirect(mode, IndexType(), (const void*)(uintptr_t)offset);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
			return true;
		}
		bool DrawInstancedIndirect(GfxBuffer* args, UINT offset) override
		{
			GLBuf* a = BufOf(args);
			GLenum mode;
			if (!a || !PrepareDraw(mode)) return false;
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, a->Id);
			glDrawArraysIndirect(mode, (const void*)(uintptr_t)offset);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
			return true;
		}
		// D3D 의 SetPredication(쿼리, FALSE) = GL 조건부 그리기 (쿼리 결과가 FALSE 면 그리기를 버린다)
		bool SetPredication(GfxQuery* predicate, BOOL value) override
		{
			if (!Dev->Check("SetPredication")) return false;
			if (Conditional)
			{
				glEndConditionalRender();
				Conditional = false;
			}
			auto* g = static_cast<GLQueryObj*>(predicate);
			if (!g || !g->Issued || g->D.Query != D3D11_QUERY_OCCLUSION_PREDICATE || value)
				return g == nullptr;   // GL 은 결과가 TRUE 일 때 건너뛰기가 없다 (D3D 의 value = TRUE)
			glBeginConditionalRender(g->Id, GL_QUERY_WAIT);
			Conditional = true;
			return true;
		}
		bool ClearUnorderedAccessViewUint(GfxUnorderedAccessView* uav, const UINT values[4]) override
		{
			if (!uav || !Dev->Check("ClearUnorderedAccessViewUint")) return false;
			const ViewInfo& v = static_cast<GLUav*>(uav)->V;
			if (v.Buffer)
			{
				glClearNamedBufferSubData(v.Buffer, GL_R32UI, v.BufferOffset, v.BufferSize, GL_RED_INTEGER, GL_UNSIGNED_INT, &values[0]);
				return true;
			}
			if (v.Name)
			{
				glClearTexImage(v.Name, v.Level, GL_RED_INTEGER, GL_UNSIGNED_INT, &values[0]);
				return true;
			}
			return false;
		}
		bool Conditional = false;
		void Dispatch(UINT x, UINT y, UINT z) override
		{
			if (!Dev->Check("Dispatch")) return;
			glDispatchCompute(x, y, z);
			glMemoryBarrier(GL_ALL_BARRIER_BITS);
		}

		// ---- 지우기 (D3D: 쓰기 마스크·가위와 상관없다)
		void ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT c[4]) override
		{
			if (!rtv || !Dev->Check("ClearRenderTargetView")) return;
			const ViewInfo& v = static_cast<GLRtv*>(rtv)->V;
			const GLuint fbo = Dev->ClearFbo;
			Attach(fbo, GL_COLOR_ATTACHMENT0, &v);
			glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			glClearNamedFramebufferfv(fbo, GL_COLOR, 0, c);
			Attach(fbo, GL_COLOR_ATTACHMENT0, nullptr);
			ApplyBlend();        // 마스크 되돌리기
			ApplyRasterizer();   // 가위 되돌리기
		}
		void ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) override
		{
			if (!dsv || !Dev->Check("ClearDepthStencilView")) return;
			const ViewInfo& v = static_cast<GLDsv*>(dsv)->V;
			const GLuint fbo = Dev->ClearFbo;
			const GLenum att = DepthAttachment(&v);
			Attach(fbo, att, &v);
			glDepthMask(GL_TRUE);
			glStencilMask(0xFF);
			glDisable(GL_SCISSOR_TEST);
			const bool d = (flags & D3D11_CLEAR_DEPTH) != 0, s = (flags & D3D11_CLEAR_STENCIL) != 0 && v.Tex && v.Tex->Fmt.Stencil;
			if (d && s) glClearNamedFramebufferfi(fbo, GL_DEPTH_STENCIL, 0, depth, stencil);
			else if (d) glClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &depth);
			else if (s) { const GLint sv = stencil; glClearNamedFramebufferiv(fbo, GL_STENCIL, 0, &sv); }
			Attach(fbo, att, nullptr);
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
				GLbitfield access = 0;
				switch (type)
				{
				case D3D11_MAP_WRITE_DISCARD:
					if (!b->Shadow.empty())
					{
						// CPU 사본에 쓰고 Unmap 때 올린다 (위 Shadow 설명)
						b->ShadowMapped = true;
						m->pData = b->Shadow.data();
						m->RowPitch = m->DepthPitch = b->Desc.ByteWidth;
						return S_OK;
					}
					access = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;
					break;
				case D3D11_MAP_WRITE_NO_OVERWRITE: access = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT; break;
				case D3D11_MAP_READ: access = GL_MAP_READ_BIT; break;
				case D3D11_MAP_READ_WRITE: access = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT; break;
				default: access = GL_MAP_WRITE_BIT; break;
				}
				void* p = glMapNamedBufferRange(b->Id, 0, b->Desc.ByteWidth, access);
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
			const UINT64 row = GLState::RowBytes(t->Fmt, w), slice = GLState::SliceBytes(t->Fmt, w, h);
			std::vector<uint8_t>& buf = t->Mapped[sub];
			buf.assign((size_t)(slice * d), 0);
			t->MapType[sub] = type;
			if (type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE)
			{
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				const GLint z = t->Target == GL_TEXTURE_3D ? 0 : (GLint)layer;
				const GLsizei depth = t->Target == GL_TEXTURE_3D ? (GLsizei)d : 1;
				if (t->Fmt.BlockBytes)
					glGetCompressedTextureSubImage(t->Id, mip, 0, 0, z, w, h, depth, (GLsizei)buf.size(), buf.data());
				else
					glGetTextureSubImage(t->Id, mip, 0, 0, z, w, h, depth, t->Fmt.Upload, t->Fmt.Type, (GLsizei)buf.size(), buf.data());
			}
			m->pData = buf.data();
			m->RowPitch = (UINT)row;
			m->DepthPitch = (UINT)slice;
			return S_OK;
		}
		void Unmap(GfxResource* r, UINT sub) override
		{
			if (!Dev->Check("Unmap")) return;
			if (GLBuf* b = BufOf(r))
			{
				if (b->ShadowMapped)
				{
					glNamedBufferSubData(b->Id, 0, b->Desc.ByteWidth, b->Shadow.data());
					b->ShadowMapped = false;
					return;
				}
				if (b->MappedNow) glUnmapNamedBuffer(b->Id);
				b->MappedNow = false;
				return;
			}
			TexInfo* t = TexOf(r);
			if (!t) return;
			auto it = t->Mapped.find(sub);
			if (it == t->Mapped.end()) return;
			const D3D11_MAP type = t->MapType[sub];
			if (type != D3D11_MAP_READ)
			{
				const UINT mip = sub % t->Mips;
				const UINT w = t->MipW(mip), h = t->MipH(mip);
				Dev->Upload(*t, sub, nullptr, it->second.data(), (UINT)GLState::RowBytes(t->Fmt, w), (UINT)GLState::SliceBytes(t->Fmt, w, h));
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
				glNamedBufferSubData(b->Id, off, size, data);
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
				glCopyNamedBufferSubData(bs->Id, bd->Id, 0, 0, (std::min)(bs->Desc.ByteWidth, bd->Desc.ByteWidth));
				return;
			}
			TexInfo* td = TexOf(dst), * ts = TexOf(src);
			if (!td || !ts) return;
			for (UINT m = 0; m < (std::min)(td->Mips, ts->Mips); ++m)
				glCopyImageSubData(ts->Id, ts->Target, m, 0, 0, 0, td->Id, td->Target, m, 0, 0, 0, ts->MipW(m), ts->MipH(m),
					ts->Target == GL_TEXTURE_3D ? ts->MipD(m) : ts->Layers);
		}
		void CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box) override
		{
			if (!Dev->Check("CopySubresourceRegion")) return;
			GLBuf* bd = BufOf(dst), * bs = BufOf(src);
			if (bd && bs)
			{
				const UINT off = box ? box->left : 0, size = box ? box->right - box->left : bs->Desc.ByteWidth;
				glCopyNamedBufferSubData(bs->Id, bd->Id, off, x, size);
				return;
			}
			TexInfo* td = TexOf(dst), * ts = TexOf(src);
			if (!td || !ts) return;
			const UINT sm = srcSub % ts->Mips, sl = srcSub / ts->Mips, dm = dstSub % td->Mips, dl = dstSub / td->Mips;
			const UINT bx = box ? box->left : 0, by = box ? box->top : 0, bz = box ? box->front : 0;
			const UINT w = box ? box->right - box->left : ts->MipW(sm), h = box ? box->bottom - box->top : ts->MipH(sm), d = box ? box->back - box->front : ts->MipD(sm);
			const bool s3 = ts->Target == GL_TEXTURE_3D, d3 = td->Target == GL_TEXTURE_3D;
			glCopyImageSubData(ts->Id, ts->Target, sm, bx, by, s3 ? bz : sl, td->Id, td->Target, dm, x, y, d3 ? z : dl, w, h, s3 ? d : 1);
		}
		void GenerateMips(GfxShaderResourceView* srv) override
		{
			if (srv && Dev->Check("GenerateMips"))
				glGenerateTextureMipmap(static_cast<GLSrv*>(srv)->V.Name);
		}

		// ---- 쿼리
		void Begin(GfxQuery* q) override
		{
			auto* g = static_cast<GLQueryObj*>(q);
			if (g && g->D.Query == D3D11_QUERY_OCCLUSION && Dev->Check("Begin")) glBeginQuery(GL_SAMPLES_PASSED, g->Id);
			if (g && g->D.Query == D3D11_QUERY_OCCLUSION_PREDICATE && Dev->Check("Begin")) glBeginQuery(GL_ANY_SAMPLES_PASSED, g->Id);
		}
		void End(GfxQuery* q) override
		{
			auto* g = static_cast<GLQueryObj*>(q);
			if (!g || !Dev->Check("End")) return;
			if (g->D.Query == D3D11_QUERY_TIMESTAMP || g->D.Query == D3D11_QUERY_TIMESTAMP_DISJOINT) glQueryCounter(g->Id, GL_TIMESTAMP);
			else if (g->D.Query == D3D11_QUERY_OCCLUSION) glEndQuery(GL_SAMPLES_PASSED);
			else if (g->D.Query == D3D11_QUERY_OCCLUSION_PREDICATE) glEndQuery(GL_ANY_SAMPLES_PASSED);
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
			{
				if (!g->Issued) return S_FALSE;
				GLuint ready = 0;
				glGetQueryObjectuiv(g->Id, GL_QUERY_RESULT_AVAILABLE, &ready);
				if (!ready) return S_FALSE;
				if (data && size >= sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT))
					*static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data) = { 1000000000ull, FALSE };   // GL 시각 = 나노초
				return S_OK;
			}
			case D3D11_QUERY_TIMESTAMP:
			case D3D11_QUERY_OCCLUSION:
			{
				if (!g->Issued) return S_FALSE;
				GLuint ready = 0;
				glGetQueryObjectuiv(g->Id, GL_QUERY_RESULT_AVAILABLE, &ready);
				if (!ready) return S_FALSE;
				GLuint64 v = 0;
				glGetQueryObjectui64v(g->Id, GL_QUERY_RESULT, &v);
				if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = v;
				return S_OK;
			}
			case D3D11_QUERY_PIPELINE_STATISTICS:
				if (data && size >= sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS)) memset(data, 0, sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS));
				return S_OK;
			case D3D11_QUERY_OCCLUSION_PREDICATE:
			{
				if (!g->Issued) return S_FALSE;
				GLuint ready = 0;
				glGetQueryObjectuiv(g->Id, GL_QUERY_RESULT_AVAILABLE, &ready);
				if (!ready) return S_FALSE;
				GLuint v = 0;
				glGetQueryObjectuiv(g->Id, GL_QUERY_RESULT, &v);
				if (data && size >= sizeof(BOOL)) *static_cast<BOOL*>(data) = v ? TRUE : FALSE;
				return S_OK;
			}
			case D3D11_QUERY_EVENT:
			{
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
			}
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
			if (Dev->Check("ClearState"))
			{
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				GLState::ApplyDefaults();
			}
		}
	};

	void GLDev::GetImmediateContext(GfxContext** out)
	{
		if (!Immediate)
		{
			// 장치 생성 때 만든 것이 사라졌으면 새로 (드묾)
			auto* c = new GLCtx();
			c->Dev = this;
			Immediate = c;
			*out = c;
			return;
		}
		Immediate->AddRef();
		*out = Immediate;
	}
}

unsigned int GfxGL_TextureName(GfxShaderResourceView* view)
{
	if (!view || view->Api() != GfxApi::OpenGL) return 0;   // D3D11 · Vulkan 뷰
	return static_cast<GLSrv*>(view)->V.Name;
}

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

std::unique_ptr<Rhi::Device> CreateGLRhiDeviceOnCurrent(GfxDevice* sinkDevice, GfxContext* sinkContext, std::string& error);   // GLRhi.cpp

namespace GfxGL
{
	bool CreateDevice(HWND window, GfxDevice** device, GfxContext** context, std::string& error)
	{
		*device = nullptr;
		*context = nullptr;
		auto* d = new GLDev();
		if (!GLContext::Create(window, d->Ctx, error))
		{
			d->GfxDevice::Release();
			return false;
		}
		d->OwnerThread = ::GetCurrentThreadId();
		glCreateVertexArrays(1, &d->EmptyVao);
		glCreateFramebuffers(1, &d->ClearFbo);
		glCreateFramebuffers(1, &d->DrawFbo);
		glCreateFramebuffers(1, &d->BlitFbo);
		// VRAM 예산: NVIDIA 는 전체 VRAM (NVX_gpu_memory_info, KB), 아니면 4 GB 로 보고 85 %
		GLint totalKb = 0;
		glGetIntegerv(0x9048 /* GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX */, &totalKb);
		glGetError();   // NVIDIA 가 아니면 INVALID_ENUM
		const UINT64 total = totalKb > 0 ? (UINT64)totalKb * 1024 : (4ull << 30);
		d->BudgetBytes = total / 100 * 85;
		char buf[32] = {};
		if (::GetEnvironmentVariableA("NOVA_GFX_VRAM_BUDGET_MB", buf, sizeof(buf)))
			d->BudgetBytes = (std::min)(d->BudgetBytes, (UINT64)atoll(buf) << 20);
		auto* c = new GLCtx();
		c->Dev = d;           // 컨텍스트가 장치를 잡는다
		d->Immediate = c;     // 장치는 약하게
		*device = d;          // 참조 1 (호출한 쪽) + 컨텍스트가 잡은 1
		*context = c;
		EditorLog::Write("Gfx", "OpenGL device: %s / %s, VRAM budget %.0f MB", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
			d->BudgetBytes / 1048576.0);
		return true;
	}

	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error)
	{
		return CreateGLRhiDeviceOnCurrent(device, context, error);
	}

	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight, int syncInterval)
	{
		auto* d = static_cast<GLDev*>(device);
		if (!d->Check("Present")) return;
		if (TexInfo* t = TexOf(backBuffer))
		{
			// 백버퍼 행 0 = 화면 위 → GL 창은 아래가 행 0 이므로 위아래를 뒤집어 복사
			glNamedFramebufferTexture(d->BlitFbo, GL_COLOR_ATTACHMENT0, t->Id, 0);
			glDisable(GL_FRAMEBUFFER_SRGB);   // 그대로 복사 (sRGB 기본 프레임버퍼에서 다시 감마를 입히지 않게)
			glDisable(GL_SCISSOR_TEST);
			glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glBlitNamedFramebuffer(d->BlitFbo, 0, 0, 0, (GLint)t->Width, (GLint)t->Height, 0, windowHeight, windowWidth, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glNamedFramebufferTexture(d->BlitFbo, GL_COLOR_ATTACHMENT0, 0, 0);
			glEnable(GL_FRAMEBUFFER_SRGB);
			if (d->Immediate)
			{
				d->Immediate->ApplyBlend();
				d->Immediate->ApplyRasterizer();
			}
		}
		GLContext::SetSwapInterval(syncInterval);
		::SwapBuffers(d->Ctx.Dc);
	}

	void RestoreState(GfxContext* context)
	{
		auto* c = static_cast<GLCtx*>(context);
		if (!c || !c->Dev->Check("RestoreState")) return;
		c->BindTargets();
		for (UINT i = 0; i < c->ViewportCount; ++i)
		{
			const D3D11_VIEWPORT& v = c->Viewports[i];
			glViewportIndexedf(i, v.TopLeftX, v.TopLeftY, v.Width, v.Height);
			glDepthRangeIndexed(i, v.MinDepth, v.MaxDepth);
		}
		for (UINT i = 0; i < c->ScissorCount; ++i)
		{
			const D3D11_RECT& r = c->Scissors[i];
			glScissorIndexed(i, r.left, r.top, (std::max)(0L, r.right - r.left), (std::max)(0L, r.bottom - r.top));
		}
		c->ApplyRasterizer();
		c->ApplyBlend();
		c->ApplyDepthStencil();
	}

	bool IsFormatSupported(DXGI_FORMAT format)
	{
		return GLState::FromDxgi(format, false).Internal != 0;
	}

	HRESULT CreateTextureFromImages(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out)
	{
		*out = nullptr;
		const size_t subs = meta.arraySize * meta.mipLevels * (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? 1 : 1);
		if (count < subs && meta.dimension != DirectX::TEX_DIMENSION_TEXTURE3D) return E_INVALIDARG;
		std::vector<D3D11_SUBRESOURCE_DATA> data;
		if (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
		{
			// 3D: 밉마다 깊이 조각들이 이어진 이미지 → 첫 조각 주소 + 조각 간격
			size_t index = 0;
			for (size_t m = 0; m < meta.mipLevels; ++m)
			{
				const DirectX::Image& img = images[index];
				data.push_back({ img.pixels, (UINT)img.rowPitch, (UINT)img.slicePitch });
				index += (std::max<size_t>)(1, meta.depth >> m);
			}
		}
		else
			for (size_t i = 0; i < subs; ++i)
				data.push_back({ images[i].pixels, (UINT)images[i].rowPitch, (UINT)images[i].slicePitch });
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

	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out)
	{
		auto* c = static_cast<GLCtx*>(context);
		TexInfo* t = TexOf(texture);
		if (!t || !c->Dev->Check("CaptureTexture")) return E_INVALIDARG;
		if (t->Fmt.BlockBytes) return E_NOTIMPL;   // 비압축만
		if (t->Target == GL_TEXTURE_CUBE_MAP || t->Target == GL_TEXTURE_CUBE_MAP_ARRAY || t->Target == GL_TEXTURE_2D_ARRAY)
		{
			// 큐브 · 배열 (Reflection Probe 굽기): 모든 조각 · 밉 (DX11 의 DirectX::CaptureTexture 와 같은 모양)
			const bool cube = t->Target != GL_TEXTURE_2D_ARRAY;
			HRESULT hr = cube ? out.InitializeCube(t->Dxgi, t->Width, t->Height, (std::max)(1u, t->Layers / 6), t->Mips)
				: out.Initialize2D(t->Dxgi, t->Width, t->Height, t->Layers, t->Mips);
			if (FAILED(hr)) return hr;
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			for (UINT layer = 0; layer < t->Layers; ++layer)
				for (UINT mip = 0; mip < t->Mips; ++mip)
				{
					const DirectX::Image* img = out.GetImage(mip, layer, 0);
					if (!img) continue;
					glPixelStorei(0x0D02 /* GL_PACK_ROW_LENGTH */, (GLint)(img->rowPitch / (std::max)(1u, t->Fmt.Bits / 8)));
					glGetTextureSubImage(t->Id, (GLint)mip, 0, 0, (GLint)layer, (GLsizei)img->width, (GLsizei)img->height, 1,
						t->Fmt.Upload, t->Fmt.Type, (GLsizei)img->slicePitch, img->pixels);
				}
			glPixelStorei(0x0D02, 0);
			return S_OK;
		}
		if (t->Target != GL_TEXTURE_2D) return E_NOTIMPL;   // 스크린샷·검사용: 2D 는 밉 0 만
		HRESULT hr = out.Initialize2D(t->Dxgi, t->Width, t->Height, 1, 1);
		if (FAILED(hr)) return hr;
		const DirectX::Image* img = out.GetImage(0, 0, 0);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glPixelStorei(0x0D02 /* GL_PACK_ROW_LENGTH */, (GLint)(img->rowPitch / (std::max)(1u, t->Fmt.Bits / 8)));
		glGetTextureImage(t->Id, 0, t->Fmt.Upload, t->Fmt.Type, (GLsizei)img->slicePitch, img->pixels);
		glPixelStorei(0x0D02, 0);
		return S_OK;
	}
}
