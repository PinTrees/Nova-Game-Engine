#pragma once
#include <map>
#include <string>
#include <vector>

// .fx (Effects11) 해석기 — 셰이더 원본(HLSL)을 하나로 두고 다른 그래픽 API 로 옮기기 위한 첫 단계.
//  입력 = 전처리를 마친 .fx 글 (#include·매크로 펼침, 주석 없음).
//  - technique11 / pass / SetXxxShader(CompileShader(프로파일, 진입점(인자))) / SetDepthStencilState·SetRasterizerState·SetBlendState
//  - 상태 블록 DepthStencilState·RasterizerState·BlendState·SamplerState·SamplerComparisonState { 키 = 값; } → 기록
//  - cbuffer 멤버 초기값 (float4 x = ...;) → 기록하고 지운다 (순수 HLSL 은 cbuffer 멤버 초기값을 받지 않는다)
//  - 인자를 넘기는 진입점 PS(3, true) → 같은 매개변수를 받는 감싸는 진입점 __nova_<번호> 를 만든다 (uniform 매개변수를 인자로 채움)
//  결과 Source = DXC 가 그대로 컴파일하는 순수 HLSL.
namespace FxParser
{
	enum class Stage { Vertex, Hull, Domain, Geometry, Pixel, Compute, Count };
	const char* StageName(Stage stage);

	struct ShaderRef
	{
		Stage StageType = Stage::Vertex;
		std::string Profile;       // vs_5_0 …
		std::string Entry;         // 컴파일할 진입점 (인자가 있었으면 만든 감싸는 함수)
		std::string Original;      // 원래 진입점 이름
	};

	struct Pass
	{
		std::string Name;
		std::vector<ShaderRef> Shaders;   // NULL 은 넣지 않는다
		std::string DepthStencilState, RasterizerState, BlendState;
		int StencilRef = 0;
		float BlendFactor[4] = { 0, 0, 0, 0 };
		unsigned SampleMask = 0xFFFFFFFFu;
	};

	struct Technique
	{
		std::string Name;
		std::vector<Pass> Passes;
	};

	struct StateBlock
	{
		std::string Type;                          // DepthStencilState …
		std::map<std::string, std::string> Fields; // 소문자 키 (BlendEnable[0] → blendenable[0]) → 값 글
	};

	struct Effect
	{
		std::string Source;                                 // 순수 HLSL
		std::vector<Technique> Techniques;
		std::map<std::string, StateBlock> States;          // 이름 → 상태 (샘플러 포함)
		std::map<std::string, std::string> Defaults;        // cbuffer 변수 → 초기값 글
		std::vector<std::string> Warnings;
	};

	bool Parse(const std::string& preprocessed, Effect& out, std::string& error);
}
