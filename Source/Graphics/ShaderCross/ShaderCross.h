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
}
