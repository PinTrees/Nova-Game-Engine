#pragma once
#include "VkLoader.h"
#include "Gfx.h"
#include <string>
#include <utility>
#include <vector>

// Gfx Vulkan 장치(GfxVk*.cpp) ↔ Vulkan 효과(VkRhi.cpp) 사이.
//  - 효과 하나 = 바인딩 배치 하나 (모든 pass 가 같은 집합 0 · 바인딩 번호 — ShaderCross::EffectSpirv)
//  - pass 하나 = 프로그램 (단계별 셰이더 모듈 + 정점 입력 의미 → location)
//  - 효과의 Apply: cbuffer 를 링에 쓰고 (WriteConstants) 바인딩 값 표와 프로그램을 컨텍스트에 (SetProgram).
//    파이프라인 · 디스크립터 집합은 그리기 때 컨텍스트가 지금 상태(타깃 형식 · 래스터 · 블렌드 …)와 합쳐 만든다
namespace GfxVkShared
{
	enum class BindingType : uint8_t { UniformBuffer, SampledImage, Sampler, Unsupported };

	struct BindingDesc
	{
		BindingType Type = BindingType::Unsupported;
		uint32_t Count = 1;          // 배열 원소 수
		// 빈 이미지 칸에 묶을 더미의 모양 (셰이더 선언과 같아야 한다)
		int Dim = 1;                 // spv::Dim: 0 1D, 1 2D, 2 3D, 3 Cube
		bool Arrayed = false;
		bool Depth = false;          // 비교 샘플러로 읽는 깊이 텍스처 → 값 1 깊이 더미
		bool Integer = false;
		bool Comparison = false;     // 샘플러: 비교 샘플러 (빈 칸 = 비교 더미)
	};

	// 링 메모리 위치 (프레임 안에서 한 번 쓰는 상수 · 동적 버퍼)
	struct RingLoc
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		uint64_t BufferId = 0;
		uint32_t Offset = 0;
		const void* Chunk = nullptr;
		uint64_t Generation = 0;
	};

	// 바인딩 원소 하나의 값 (BindingLayout 의 원소 순서: 바인딩 0 의 원소들, 바인딩 1 …)
	struct BindingValue
	{
		GfxShaderResourceView* View = nullptr;   // SampledImage (nullptr = 더미)
		GfxSamplerState* Sampler = nullptr;      // Sampler (nullptr = 더미)
		RingLoc Ubo;                             // UniformBuffer
		uint32_t Range = 0;
	};

	struct StageCode
	{
		VkShaderStageFlagBits Stage = VK_SHADER_STAGE_VERTEX_BIT;
		const std::vector<uint32_t>* Code = nullptr;
		std::string Entry;
	};

	HRESULT CreateBindingLayout(GfxDevice* device, const BindingDesc* bindings, uint32_t count, GfxObject** out, std::string& error);
	uint32_t ElementCount(GfxObject* layout);                  // 원소 수 (BindingValue 표 크기)
	uint32_t ElementOffset(GfxObject* layout, uint32_t binding);

	HRESULT CreateProgram(GfxDevice* device, GfxObject* layout, const StageCode* stages, uint32_t count,
		const std::vector<std::pair<std::string, int>>& vertexInputs, uint32_t pixelOutputs, const std::string& name, GfxObject** out, std::string& error);

	// 링에 상수 블록 쓰기 (minUniformBufferOffsetAlignment 맞춤). IsCurrent = 앞에 쓴 위치를 이번 기록에서 그대로 써도 되는지
	bool WriteConstants(GfxDevice* device, const void* data, uint32_t size, RingLoc& loc);
	bool IsCurrent(GfxDevice* device, const RingLoc& loc);

	// 다음 그리기의 프로그램 · 바인딩 값 (컨텍스트가 값을 복사하고 뷰 · 샘플러를 잡는다). program = nullptr 이면 그리지 않음
	void SetProgram(GfxContext* context, GfxObject* program, const BindingValue* values, uint32_t count);
}
