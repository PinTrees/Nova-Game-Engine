#pragma once
#include "Gfx.h"
#include <string>
#include <vector>

// Gfx DX12 장치(GfxD3D12*.cpp) ↔ DX12 효과(D3D12Rhi.cpp) 사이 (Vulkan 의 GfxVkShared 와 같은 역할).
//  - 효과 하나 = 바인딩 표 하나 (바인딩 번호 = ShaderCross 의 효과 바인딩, 원소 = 배열 칸)
//  - pass 하나 = 프로그램: 단계마다 DXIL + (레지스터 종류, 번호) → 효과 원소 표. 루트 시그니처는 단계마다 표 2 개 (CBV·SRV·UAV / 샘플러)
//  - Apply: cbuffer 를 업로드 링에 쓰고 (WriteConstants) 값 표와 프로그램을 컨텍스트에 (SetProgram). PSO 는 그리기 때 상태와 합쳐 만든다
namespace GfxD3D12Shared
{
	// 업로드 링 위치 (상수 버퍼 — GPU 가상 주소 + 이 위치가 아직 유효한지)
	struct RingLoc
	{
		uint64_t Gpu = 0;          // D3D12_GPU_VIRTUAL_ADDRESS
		const void* Chunk = nullptr;
		uint64_t Generation = 0;
	};

	// 원소 하나의 값 (효과 원소 순서)
	struct BindingValue
	{
		GfxShaderResourceView* View = nullptr;   // SRV (nullptr = 널 디스크립터)
		GfxUnorderedAccessView* Uav = nullptr;   // UAV
		GfxSamplerState* Sampler = nullptr;      // 샘플러 (nullptr = 기본)
		RingLoc Cbv;                             // 상수 버퍼
		uint32_t CbvSize = 0;
	};

	enum class SlotKind : uint8_t { Cbv, Srv, Uav, Sampler };

	// 단계 하나가 쓰는 칸 (레지스터 순서로): 루트 표의 n 번째 = 이 칸
	struct StageSlot
	{
		SlotKind Kind = SlotKind::Srv;
		uint32_t Register = 0;
		int Element = -1;          // 효과 원소 (-1 = 비움 → 널 디스크립터)
		int Dimension = 0;         // 널 SRV 모양 (D3D_SRV_DIMENSION)
		bool Buffer = false;       // 버퍼 SRV · UAV (널 = 버퍼 모양)
		bool Comparison = false;   // 샘플러: SamplerComparisonState (값이 없으면 기본 비교 샘플러)
	};

	enum class StageType : uint8_t { Vertex, Hull, Domain, Geometry, Pixel, Compute };

	struct StageCode
	{
		StageType Stage = StageType::Vertex;
		const std::vector<uint8_t>* Dxil = nullptr;
		std::vector<StageSlot> Slots;
	};

	// pixelOutputs = SV_Target 비트 (쓰지 않는 색 타깃은 쓰기 마스크 0)
	HRESULT CreateProgram(GfxDevice* device, const StageCode* stages, uint32_t count, uint32_t pixelOutputs, const std::string& name, GfxObject** out, std::string& error);

	// 업로드 링에 상수 블록 (256 바이트 맞춤). IsCurrent = 앞 위치를 이번 기록에서 그대로 써도 되는지
	bool WriteConstants(GfxDevice* device, const void* data, uint32_t size, RingLoc& loc);
	bool IsCurrent(GfxDevice* device, const RingLoc& loc);

	// 다음 그리기 · 디스패치의 프로그램 · 값 (컨텍스트가 값을 복사하고 뷰 · 샘플러를 잡는다). program = nullptr 이면 그리지 않음
	void SetProgram(GfxContext* context, GfxObject* program, const BindingValue* values, uint32_t count);
}
