#pragma once
#include "FxParser.h"
#include <map>
#include <string>
#include <vector>

// 셰이더 자동 변환 (원본 = HLSL .fx 하나): DXC 로 단계마다 SPIR-V → SPIRV-Cross 로 GLSL 4.50.
//  - DXC(dxcompiler.dll)는 실행 중에 불러온다. -fvk-invert-y + OpenGL 쪽 glClipControl(LOWER_LEFT, ZERO_TO_ONE) 로
//    D3D 와 같은 깊이 범위·텍스처 행 순서(행 0 = D3D 의 위)를 쓴다 (gl_FragCoord.y 도 D3D SV_Position.y 와 같은 값)
//  - cbuffer 는 D3D 패킹 그대로(-fvk-use-dx-layout) → GLSL uniform 블록의 offset 이 D3D 와 같다 (CPU 쪽 값 배치를 그대로 씀)
//  - 단계 사이 값은 의미(SEMANTIC) 이름 + 경계 번호로 맞물리게 이름을 바꾼다 (v0_TEXCOORD3 = 첫 단계 출력·둘째 단계 입력 …),
//    정점 입력은 in_POSITION … + location
//  - 텍스처 + 샘플러는 GL 의 결합 샘플러로 (이름 = 텍스처_샘플러), 바인딩 번호는 효과 하나 안에서 이름마다 고정
namespace ShaderCross
{
	using FxParser::Stage;

	struct UniformBlock
	{
		std::string Name;        // cbuffer 이름 (cbPerFrame, $Globals …)
		int Binding = 0;
		int Size = 0;            // 바이트
		struct Member
		{
			std::string Name;
			int Offset = 0;
			int Size = 0;          // 바이트 (배열이면 전체)
			int ArrayCount = 0;    // 0 = 배열 아님
			int ArrayStride = 0;
			int Rows = 1, Columns = 1;   // 벡터 = Rows 성분, 행렬 = Columns > 1
			bool Transpose = false;      // HLSL column_major 행렬: Effects11 SetMatrix 처럼 전치해서 넣는다
			bool Struct = false;
			bool Integer = false;        // int/uint/bool (초기값을 정수로 넣는다)
		};
		std::vector<Member> Members;
	};

	struct SamplerBinding
	{
		std::string Name;        // GLSL 이름 (텍스처_샘플러)
		std::string Texture, Sampler;
		int Unit = 0;            // 첫 유닛 (배열이면 Unit .. Unit + Count - 1)
		int Count = 1;
	};

	struct StageGlsl
	{
		Stage StageType = Stage::Vertex;
		std::string Entry;
		std::string Glsl;
	};

	struct PassGlsl
	{
		std::string Technique, Pass;
		std::vector<StageGlsl> Stages;
		std::vector<std::pair<std::string, int>> VertexInputs;   // 의미 → location
		std::string Error;      // 비면 성공
	};

	struct EffectGlsl
	{
		std::wstring File;
		FxParser::Effect Fx;
		std::vector<PassGlsl> Passes;
		std::map<std::string, UniformBlock> Blocks;      // 이름 → 블록 (모든 pass 공통 바인딩)
		std::map<std::string, SamplerBinding> Samplers;  // GLSL 이름 → 유닛 (Load 만 쓰는 텍스처 = 텍스처_nosampler)
		std::map<std::string, int> Images;               // RWTexture 이름 → image 유닛 (compute)
		std::map<std::string, int> Buffers;              // (RW)StructuredBuffer 이름 → SSBO 바인딩
		std::string Error;      // 효과 전체 실패 (전처리·해석)
		int PassesOk() const;
	};

	bool Available(std::string* error = nullptr);   // dxcompiler.dll 을 불러올 수 있는지
	// fxPath = .fx 파일. 모든 technique 의 모든 pass 를 변환 (pass 마다 성공/실패)
	bool CompileEffect(const std::wstring& fxPath, EffectGlsl& out);
	// OpenGL ES 3.20 (안드로이드) 변환 — 바인딩 · 이름 규칙은 위와 같고 GLSL 만 ES. 캐시 ShaderCache/GLES
	bool CompileEffectGles(const std::wstring& fxPath, EffectGlsl& out);
	bool CompileEffectAs(const std::wstring& fxPath, EffectGlsl& out, bool es);

	// ---- Vulkan: pass 마다 단계별 SPIR-V 를 그대로 쓴다 (GLSL 로 바꾸지 않음)
	//  - DXC 의 SPIR-V 에서 장식 (decoration) 값만 고친다: 모든 자원 = set 0, 바인딩 = 효과 안에서 이름마다 고정
	//    (cbuffer · 텍스처 · 샘플러가 따로 — Vulkan 은 텍스처와 샘플러를 나눠 묶을 수 있어 GL 처럼 합치지 않는다)
	//  - 단계 사이 location = 앞 단계 출력의 같은 의미(SEMANTIC) 의 location (구조체가 달라도 맞물린다)
	//  - 좌표는 GL 과 같은 -fvk-invert-y (Vulkan 프레임버퍼 행 0 = 위 = D3D), SV_InstanceID = D3D 처럼 시작 인스턴스를 빼고
	struct ResourceBinding
	{
		enum class Kind { SampledImage, Sampler, StorageImage, StorageBuffer, TexelBuffer };
		std::string Name;
		Kind Type = Kind::SampledImage;
		int Binding = 0;
		int Count = 1;           // 배열 원소 수
		int Dim = 1;             // 이미지: 0 1D, 1 2D, 2 3D, 3 Cube (spv::Dim)
		bool Arrayed = false;    // 1D/2D/Cube 배열
		bool Depth = false;      // SampleCmp 로 읽는 깊이 텍스처 (빈 칸에는 깊이 더미)
		bool Integer = false;    // 정수 텍스처 (uint/int)
		bool Comparison = false; // 샘플러: SamplerComparisonState
	};

	// WebGPU: Tint 가 알려 준 바인딩 하나 (--dump-inspector-bindings) — 바인딩 배치의 종류를 정할 때
	struct WgslBinding
	{
		int Binding = 0;
		std::string Type;      // UniformBuffer · StorageBuffer · ReadOnlyStorageBuffer · SampledTexture · DepthTexture · Sampler · WriteOnlyStorageTexture · ReadWriteStorageTexture …
		std::string Dim;       // 1d · 2d · 2dArray · Cube · CubeArray · 3d
		std::string Sampled;   // Float · SInt · UInt · unknown-filterable
		std::string Format;    // 스토리지 텍스처 형식 (R32Float · Rgba8Unorm …)
		std::string SamplerType;   // 샘플러: comparison · filtering · unknown-filtering
	};

	struct StageSpirv
	{
		Stage StageType = Stage::Vertex;
		std::string Entry;
		std::vector<uint32_t> Code;
		// WebGPU (CompileEffectWgsl): Tint 가 바꾼 WGSL + 바인딩 정보. 진입점 이름은 Tint 가 정한다 (WgslEntry)
		std::string Wgsl, WgslEntry;
		std::vector<WgslBinding> WgslBindings;
	};

	struct PassSpirv
	{
		std::string Technique, Pass;
		std::vector<StageSpirv> Stages;
		std::vector<std::pair<std::string, int>> VertexInputs;   // 의미 → location
		uint32_t PixelOutputs = 0;   // 픽셀 셰이더가 쓰는 SV_Target 번호 (비트) — 쓰지 않는 색 타깃은 쓰기 마스크 0
		std::vector<int> Bindings;   // 이 pass 의 단계들이 쓰는 바인딩 번호 (지원하지 않는 자원을 쓰는 pass 를 가린다)
		std::vector<std::pair<int, int>> SamplerPairs;   // (텍스처, 샘플러) 바인딩 — 같이 쓰는 쌍 (WebGPU: 깊이 텍스처면 샘플러도 non-filtering)
		std::string Error;
	};

	struct EffectSpirv
	{
		std::wstring File;
		FxParser::Effect Fx;
		std::vector<PassSpirv> Passes;
		std::map<std::string, UniformBlock> Blocks;          // 이름 → cbuffer (Binding = 바인딩)
		std::map<std::string, ResourceBinding> Resources;    // 이름 → 텍스처 · 샘플러 · 버퍼
		int BindingCount = 0;                                // 바인딩 번호 0 .. BindingCount - 1
		std::string Error;
		int PassesOk() const;
	};

	bool CompileEffectSpirv(const std::wstring& fxPath, EffectSpirv& out);

	// WebGPU (웹 빌드): SPIR-V 와 같은 바인딩 · 배치에 단계마다 WGSL (DXC → SPIR-V → Tint).
	//  - NOVA_WEBGPU 를 정의한다. Y 뒤집기 · 시작 인스턴스 보정 없음 (WebGPU 좌표 = D3D, SV_InstanceID 는 백엔드가 맞춘다)
	//  - 테셀레이션 · 지오메트리 단계가 있는 pass 는 Error (웹에 없다 — 셰이더가 테셀레이션 없는 기법으로 갈아탄다)
	//  - Tint = tint.exe (TintPath). 캐시 ShaderCache/WGSL
	bool CompileEffectWgsl(const std::wstring& fxPath, EffectSpirv& out);

	// ---- DirectX 12: pass 마다 단계별 DXIL (DXC + dxil.dll 서명). 좌표 · 인스턴스 · 깊이 범위는 D3D 그대로 (바꿀 것 없음)
	//  - 효과 전체의 자원 정보 (cbuffer 배치 · 이름마다 고정 바인딩 번호) 는 CompileEffectSpirv 의 것 (Meta) 을 같이 쓴다
	//  - 레지스터는 DXC 가 단계마다 정한다 → 리플렉션으로 (종류, 레지스터, 개수, 이름) 을 모아 효과 바인딩 번호로 잇는다
	//    (D3D12 백엔드는 단계마다 디스크립터 표를 따로 둔다 — 단계끼리 레지스터가 겹쳐도 된다)
	struct DxilBinding
	{
		enum class Kind { Cbv, Srv, Uav, Sampler };
		Kind Type = Kind::Srv;
		std::string Name;
		int Register = 0;
		int Count = 1;
		int Binding = -1;        // 효과 바인딩 번호 (Meta.Blocks / Meta.Resources) — -1 = 모름 (빈 칸)
		int Dimension = 0;       // D3D_SRV_DIMENSION (빈 칸 널 디스크립터의 모양)
		bool Buffer = false;     // 구조 · raw 버퍼 (SRV · UAV)
		bool Comparison = false; // SamplerComparisonState
	};

	struct StageDxil
	{
		Stage StageType = Stage::Vertex;
		std::string Entry;
		std::vector<uint8_t> Code;
		std::vector<DxilBinding> Bindings;
	};

	struct PassDxil
	{
		std::string Technique, Pass;
		std::vector<StageDxil> Stages;
		uint32_t PixelOutputs = 0;   // SV_Target 번호 (비트)
		std::string Error;
	};

	struct EffectDxil
	{
		std::wstring File;
		EffectSpirv Meta;            // cbuffer · 자원 바인딩 번호 · 파싱한 효과 (상태 블록 · 초기값)
		std::vector<PassDxil> Passes;
		std::string Error;
		int PassesOk() const;
	};

	bool CompileEffectDxil(const std::wstring& fxPath, EffectDxil& out);
	std::wstring TintPath();   // NOVA_TINT 환경 변수 → Binaries/tint.exe → %USERPROFILE%/.nova/dawn/out/tint/Release/tint.exe (없으면 "")
}
