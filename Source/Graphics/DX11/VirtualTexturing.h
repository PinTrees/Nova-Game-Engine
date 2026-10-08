#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Streaming Virtual Texturing (Unity SVT 와 같은 생각 — 렌더링 현대화 5 단계, docs/VIRTUAL_TEXTURING.md).
//  큰 텍스처 (텍스처 가져오기 설정 "Virtual Texture Only") 를 통째로 GPU 에 올리지 않고, 화면에 보이는 128 x 128 페이지만 올린다.
//  - 타일 파일 (Library/VirtualTextures/*.nvt): 처음 쓸 때 원본 → 밉 (가장 작은 밉이 128 이상인 데까지) → 페이지마다 테두리 4 텍셀을 붙인 136 x 136 RGBA8
//  - 물리 캐시: 모든 가상 텍스처가 함께 쓰는 텍스처 하나 (타일 칸 N x N), LRU 로 비운다. 가장 거친 밉의 페이지는 늘 올라 있다 (대체)
//  - 페이지 표: 가상 텍스처마다 밉 사슬이 있는 작은 텍스처 — 칸마다 (캐시 칸 x, y, 실제로 올라 있는 밉). 없는 페이지는 올라 있는 가장 가까운 조상을 가리킨다
//  - 피드백: Render Graph 의 VT Feedback 패스가 VT 물체를 화면의 1/8 크기로 그려 (텍스처 번호, 밉, 페이지 x, y) 를 쓰고, 2 프레임 뒤 CPU 가 읽어 필요한 페이지를 요청
//  - 페이지 읽기는 작업 스레드 (파일), 올리기는 프레임마다 정해진 수까지
//  셰이더: 32. InstancedBasic.fx 의 SampleVirtual (Base Map 이 가상이면 — gPbr.UseBaseMap == 2)
namespace VirtualTexturing
{
	// 가져오기 설정이 Virtual Texture Only 인가 (fullPath = 텍스처 파일)
	bool IsVirtual(const std::wstring& fullPath);
	// 등록 (처음이면 타일 파일을 만든다 — 큰 텍스처는 몇 초). 0 = 실패 (로그). 같은 파일은 같은 번호
	int Register(const std::wstring& fullPath);

	// 재질 Apply 가 넣을 것: 페이지 표 · 캐시 SRV, Info0 = (가상 폭, 높이, 밉 수, 0), Info1 = (캐시 폭 px, 높이 px, 타일 px (테두리 포함), 테두리 px)
	struct Binding
	{
		GfxShaderResourceView* PageTable = nullptr;
		GfxShaderResourceView* Cache = nullptr;
		float Info0[4] = {};
		float Info1[4] = {};
	};
	bool GetBinding(int id, Binding& out);
	// VT 가 없는 패스 (그림자 · 깊이 알파 자르기 · GI 찍기 · 미리 보기) 가 쓸 작은 보통 텍스처 (가장 거친 밉)
	GfxShaderResourceView* Fallback(int id);

	bool Enabled();                       // 끄면 재질은 대체 텍스처만 (비교 · 문제 찾기)
	bool HasWork();                       // 등록한 가상 텍스처가 있고 켜져 있다 (VT Feedback 패스를 넣을지)

	// 피드백 패스 (Render Graph): 이 뷰의 VT 물체를 1/8 크기로. normalDepth = 깊이 프리패스 (가려진 면은 요청하지 않는다)
	void RenderFeedback(GfxContext* ctx, const Matrix& view, const Matrix& proj, UINT viewWidth, UINT viewHeight, GfxShaderResourceView* normalDepth, bool editorView);
	// 프레임마다 한 번: 피드백 읽기 → 요청 → 다 읽은 페이지 올리기 → 페이지 표
	void Update();

	nlohmann::json Info();
	void RegisterEditor();   // CLI: nova vt info|page|set|flush
}
