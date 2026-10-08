#pragma once
#include <nlohmann/json.hpp>

struct AdditionalLight;
class InstancedBasicEffect;
class FxEffect;

// Forward+ (클러스터 조명, Unity URP 의 Forward+ 와 같은 생각): 화면을 16 x 9 타일 x 24 깊이 조각 (로그 간격) 으로 나누고,
//  그림자 있는 앞의 빛 (LIGHT_SIZE) 밖의 점광 · 스포트광 · 입자 빛을 그 빛이 닿는 클러스터에만 넣는다 → 픽셀은 자기 클러스터의 빛만 돈다.
//  URP 처럼 CPU 가 짓는다 (빛마다 닿는 타일 · 조각 범위만 — 빛 수에 비례). GPU 로는 텍스처 3 개 (빛 · 클러스터 표 · 번호 목록)
//  — 컴퓨트 · SSBO 없이 Load 만 쓰므로 DirectX 11 · OpenGL · Vulkan · GLES · WebGPU 모두 같다.
//  뷰를 그릴 때마다 (Game · Scene · 반사 프로브 면) Build → 그 뷰의 효과마다 Bind
namespace ClusteredLighting
{
	constexpr int kTilesX = 16, kTilesY = 9, kSlices = 24;
	constexpr int kMaxIndices = 65535;   // 클러스터 표의 시작 번호가 16 비트

	// view · proj = 그 뷰의 카메라 (proj 는 TAA 지터가 있어도 된다 — 가까운 · 먼 면도 proj 에서)
	void Build(const std::vector<AdditionalLight>& lights, const XMFLOAT4X4& view, const XMFLOAT4X4& proj);
	void Bind(InstancedBasicEffect* fx);
	void BindFx(FxEffect* fx);   // 다른 효과 (Rendering Debugger 의 Light Count 보기)

	struct Stats
	{
		int Lights = 0;            // 넣은 빛
		int Culled = 0;            // 화면 · 깊이 밖이라 뺀 빛
		int Indices = 0;           // 클러스터 목록 칸 (빛 x 닿은 클러스터)
		int Dropped = 0;           // kMaxIndices 를 넘어 못 넣은 칸
		int MaxPerCluster = 0;
		int NonEmptyClusters = 0;
		float BuildMs = 0.0f;
	};
	const Stats& LastStats();
	// 끄면 앞의 LIGHT_SIZE 빛만 (Unity URP 의 Rendering Path = Forward 처럼) — 비교 · 진단용 (nova forwardplus set --enabled false)
	void SetEnabled(bool enabled);
	bool Enabled();
	nlohmann::json Info();
	void RegisterEditor();   // CLI: nova forwardplus info
}
