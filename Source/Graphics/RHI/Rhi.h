#pragma once
#include "GraphicsAPI.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxShaderResourceView;
class GfxUnorderedAccessView;

// RHI (Render Hardware Interface) — 렌더러가 그래픽 API 를 모르고 쓰는 공통 층.
//  - DirectX 11 구현(`Dx11Rhi`)은 에디터가 만든 장치 + Effects11, OpenGL 4.5 구현(`GLRhi`)은 자기 컨텍스트 + ShaderCross(GLSL)
//  - 셰이더는 지금처럼 .fx(HLSL) 하나. 효과(Effect) API 는 Effects11 과 같은 모양:
//    technique/pass 이름, 변수 이름으로 값·텍스처 넣기, Apply(pass) = 셰이더 + 상수 + 텍스처 + pass 의 상태
//  - 좌표 규칙은 D3D 그대로: 클립 z 0..1, 텍스처·렌더 타깃 행 0 = 위, uv (0,0) = 왼쪽 위, 앞면 = 시계 방향
//  - 행렬은 C++ 쪽 XMFLOAT4X4(행 우선) 그대로 넘긴다 (HLSL column_major 변수는 구현이 Effects11 처럼 전치)
namespace Rhi
{
	enum class Format
	{
		Unknown,
		RGBA8_UNorm,
		RGBA16_Float,
		RGBA32_Float,
		R32_Float,
		RG16_Float,
		D32_Float,
		D24_UNorm_S8_UInt,
	};
	uint32_t BytesPerPixel(Format format);
	bool IsDepth(Format format);

	enum class VertexFormat { Float1, Float2, Float3, Float4, UByte4_UNorm, UInt4 };

	enum class Topology { TriangleList, TriangleStrip, LineList, LineStrip, PointList };

	enum BindFlags : uint32_t
	{
		BindVertex = 1, BindIndex = 2, BindConstant = 4,
	};

	struct BufferDesc
	{
		uint32_t Size = 0;        // 바이트
		uint32_t Bind = BindVertex;
		bool Dynamic = false;     // UpdateBuffer 로 자주 바꾸는지
	};

	enum class TextureType { Tex2D, Tex2DArray, Cube };

	struct TextureDesc
	{
		TextureType Type = TextureType::Tex2D;
		uint32_t Width = 1, Height = 1;
		uint32_t ArraySize = 1;   // Cube = 6
		uint32_t MipLevels = 1;
		Rhi::Format Format = Rhi::Format::RGBA8_UNorm;
		bool RenderTarget = false;   // 색 타깃 또는 (깊이 형식이면) 깊이 타깃
	};

	// 처음 데이터: 조각(배열·큐브 면)마다 밉 0..n 순서 (D3D11_SUBRESOURCE_DATA 와 같은 순서), 행 0 = 위
	struct SubresourceData
	{
		const void* Data = nullptr;
		uint32_t RowPitch = 0;
	};

	struct VertexElement
	{
		const char* Semantic = "POSITION";
		uint32_t SemanticIndex = 0;
		VertexFormat Format = VertexFormat::Float3;
		uint32_t Offset = 0;
		uint32_t Slot = 0;
		bool PerInstance = false;
	};

	class Buffer
	{
	public:
		virtual ~Buffer() = default;
		const BufferDesc& Desc() const { return _desc; }
	protected:
		BufferDesc _desc;
	};

	class Texture
	{
	public:
		virtual ~Texture() = default;
		const TextureDesc& Desc() const { return _desc; }
	protected:
		TextureDesc _desc;
	};

	class InputLayout
	{
	public:
		virtual ~InputLayout() = default;
	};

	// 효과 변수 손잡이 (이름 → 번호, 없으면 -1)
	using VarId = int;

	class Effect
	{
	public:
		virtual ~Effect() = default;
		virtual int FindTechnique(const std::string& name) const = 0;          // 없으면 -1
		virtual int PassCount(int technique) const = 0;
		virtual VarId FindVariable(const std::string& name) = 0;               // cbuffer 멤버 또는 텍스처

		virtual int TechniqueCount() const = 0;
		virtual std::string TechniqueName(int technique) const = 0;

		// 값 넣기 — Effects11 과 같은 뜻: 타입이 다르면 바꿔 넣는다 (int 변수에 SetFloat = 정수로), 변수 크기를 넘지 않는다
		virtual void SetRaw(VarId var, const void* data, uint32_t bytes, uint32_t offset = 0) = 0;
		virtual void SetFloat(VarId var, float v) = 0;
		virtual void SetInt(VarId var, int v) = 0;
		virtual void SetBool(VarId var, bool v) = 0;
		virtual void SetVector(VarId var, const float v[4]) = 0;             // 변수 크기만큼 (float3 = 12 바이트)
		virtual void SetFloatArray(VarId var, const float* v, uint32_t first, uint32_t count) = 0;      // 원소 간격 = cbuffer 배치 (16 바이트)
		virtual void SetVectorArray(VarId var, const float* v, uint32_t first, uint32_t count) = 0;     // float4 원소
		virtual void SetMatrix(VarId var, const float m[16]) = 0;            // 행 우선 (XMFLOAT4X4)
		virtual void SetMatrixArray(VarId var, const float* m, uint32_t first, uint32_t count) = 0;
		virtual void SetTexture(VarId var, Texture* texture, uint32_t arrayIndex = 0) = 0;
		virtual void GetVector(VarId var, float out[4]) = 0;                 // 지금 들어 있는 값 (float4)

		// Gfx 층(Gfx.h)의 뷰 — 엔진 렌더러가 만든 텍스처
		virtual void SetView(VarId var, GfxShaderResourceView* srv, uint32_t arrayIndex = 0) = 0;
		virtual void SetUav(VarId var, GfxUnorderedAccessView* uav) = 0;
		// pass 의 정점 입력 서명 (D3D11 CreateInputLayout 용, 다른 API = false)
		virtual bool NativeInputSignature(int technique, int pass, const void** data, size_t* size) = 0;

		virtual void Apply(int technique, int pass) = 0;
		// 그 pass 를 이 기기에서 쓸 수 있는가 (OpenGL · Vulkan: 변환 · 링크 · 파이프라인이 실패했거나 기기에 그 단계가 없으면 false
		//  — 예: 테셀레이션이 없는 OpenGL ES). 기법이 있는지는 FindTechnique
		virtual bool PassUsable(int technique, int pass) const { return technique >= 0 && pass >= 0 && pass < PassCount(technique); }
	};

	class Device
	{
	public:
		virtual ~Device() = default;
		virtual GraphicsAPI GetAPI() const = 0;
		virtual std::string Description() const = 0;     // GPU·드라이버·버전 (로그용)

		virtual std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
		virtual void UpdateBuffer(Buffer* buffer, const void* data, uint32_t bytes) = 0;
		virtual std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc, const SubresourceData* initialData = nullptr) = 0;
		virtual std::unique_ptr<Effect> LoadEffect(const std::wstring& fxPath, std::string& error) = 0;
		// 효과 pass 의 정점 입력과 맞춘 입력 배치 (D3D 는 VS 서명, GL 은 의미 → location)
		virtual std::unique_ptr<InputLayout> CreateInputLayout(const VertexElement* elements, uint32_t count, Effect* effect, int technique, int pass, std::string& error) = 0;

		// 상태를 D3D 기본값으로 (깊이 LESS·쓰기, 뒷면 컬링, 블렌드 끔)
		virtual void ResetState() = 0;
		// depthSlice = 깊이 텍스처가 배열이면 그릴 조각 (그림자 캐스케이드·큐브 면)
		virtual void SetRenderTargets(Texture* const* colors, uint32_t count, Texture* depth, uint32_t depthSlice = 0) = 0;
		virtual void SetViewport(float x, float y, float width, float height) = 0;
		virtual void ClearColor(Texture* target, const float rgba[4]) = 0;
		virtual void ClearDepth(Texture* target, float depth) = 0;   // 배열이면 모든 조각

		virtual void SetInputLayout(InputLayout* layout) = 0;
		virtual void SetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t stride, uint32_t offset = 0) = 0;
		virtual void SetIndexBuffer(Buffer* buffer, bool use32Bit) = 0;
		virtual void SetTopology(Topology topology) = 0;
		virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;
		virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0) = 0;
		virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex = 0, int32_t baseVertex = 0, uint32_t startInstance = 0) = 0;

		// RGBA8 로 읽기 (행 0 = 위). 검사·스크린샷용
		virtual bool ReadPixels(Texture* texture, std::vector<uint8_t>& rgba, std::string& error) = 0;
		virtual void Finish() = 0;
	};

	// api 에 맞는 장치. DirectX 11 = 에디터가 만든 장치를 감쌈, OpenGL = 숨은 창 + GL 4.5 컨텍스트 (이 스레드에 현재로)
	std::unique_ptr<Device> CreateDevice(GraphicsAPI api, std::string& error);

	// Gfx 층의 DirectX 11 장치·컨텍스트 위에 (App 이 만든 장치)
	std::unique_ptr<Device> WrapD3D11(class GfxDevice* device, class GfxContext* context);

	// 엔진이 쓰는 장치 (App 이 그래픽 초기화 때 정한다). 렌더러·효과(FxEffect)는 이것을 쓴다
	Device* Main();
	void SetMain(std::unique_ptr<Device> device);
}
