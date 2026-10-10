#pragma once
#include <vector>
#include "NovaApi.h"
// nlohmann::json · XMFLOAT4X4 · FxEffect: pch

class SkinnedMeshRenderer;
namespace CrowdAnimation { struct Baked; }

// 스킨드 인스턴싱 (군중 — docs/CROWD.md): 같은 지오메트리 (LOD) · 재질의 Skinned Mesh Renderer 를 서브셋마다 한 번에 그린다.
//  - 본 팔레트: 프레임마다 한 번, 이번 프레임 그릴 렌더러 (지난 카메라 · 그림자 컬링에 보인 것) 를 구조 버퍼 하나에 (본마다 float4 세 칸)
//  - 패스마다 (본 · 그림자 · 깊이 프리패스 · 모션 벡터): 보이는 렌더러를 모아 인스턴스 (월드 + 팔레트 자리) 를 올리고 묶음마다 DrawIndexedInstanced
//  - 대상: Auto LOD 를 켠 렌더러 중 BlendShape · 천 · 패키지 셰이더 · Alpha Clipping 이 없는 것. 나머지는 예전처럼 렌더러마다
//  - 예전: 캐릭터마다 패스 4 개 × 서브셋 3 개, 그릴 때마다 본 상수 버퍼 16 KB (모션 벡터 32 KB) 를 올렸다
namespace SkinnedInstancing
{
	enum class Pass { Main, Shadow, NormalDepth };

	extern NOVA_API bool Enabled;   // 끄면 렌더러마다 (비교 측정 — CLI crowd instancing)

	// ---- 공용 (66. SkinInstancing.fx): 인스턴스 9 칸 — 다른 군중 시스템도 같은 셰이더로 그린다
	constexpr unsigned CrowdInstanceStride = 9;
	constexpr uint32_t kInstanceStill = 1u;        // 모션 벡터: 움직임 없음
	constexpr uint32_t kInstancePrevBuffer = 2u;   // 지난 팔레트는 gSkinPrevPalettes (지난 프레임 버퍼)
	// 팔레트 A → B 를 lerp 로 (구운 클립 프레임 · 이번 팔레트면 A = B), row = 임포스터 아틀라스 줄, tint = 알베도에 곱할 색
	NOVA_API void WriteInstance(std::vector<XMFLOAT4>& out, const XMFLOAT4X4& world, uint32_t palA, uint32_t palB, float lerp, float row,
		const XMFLOAT4X4& prevWorld, uint32_t prevA, uint32_t prevB, float prevLerp, uint32_t flags, const XMFLOAT4& tint);
	// CPU 가 쓰는 float4 구조 버퍼 (커지기만 한다 — 올릴 때마다 Map Discard)
	struct NOVA_API InstanceBuffer
	{
		ComPtr<GfxBuffer> Buffer;
		ComPtr<GfxShaderResourceView> Srv;
		unsigned Elements = 0;
		bool Upload(const XMFLOAT4* data, size_t count);
	};
	// 구운 클립의 애니메이션 임포스터 아틀라스 (8 방향 × 클립마다 8 프레임) — GPU, 렌더 타깃 · 상태를 되돌린다
	NOVA_API bool BakeImpostor(CrowdAnimation::Baked& baked);

	// 이 패스에 모은다. true 면 부른 쪽은 그리지 않는다 (Flush 가 그린다)
	bool Add(SkinnedMeshRenderer* r, Pass pass, bool editor);
	void Flush(Pass pass, bool editor);

	// 모션 벡터 패스 (MotionVectors.cpp): 지난 월드 · 지난 팔레트는 지난 프레임에 올린 팔레트 버퍼 (GPU 에 남겨 둔다 — CPU 로 본을 복사 · 비교하지 않는다)
	bool CanDrawMotion(SkinnedMeshRenderer* r);   // 인스턴싱 대상이고 이번 프레임 팔레트가 있다 (부른 쪽은 기록 · 그리기를 하지 않는다)
	// mode = Motion Vectors (0 Camera Motion · 1 Per Object · 2 Force No Motion). 그릴 것으로 모았으면 true (움직이지 않았으면 false)
	bool AddMotion(SkinnedMeshRenderer* r, int mode, bool objectMotion, bool skinnedMotion);
	// fx = 63. MotionVectors (gViewProj · gPrevViewProj 등은 부른 쪽이 넣어 둔다). 그린 렌더러 수
	int FlushMotion(FxEffect* fx);

	// 먼 (임포스터 단계) 캐릭터의 그림자도 임포스터로 그린다 (이번 프레임 그 기법이 있다) — 그림자에 보여도 자세 · 팔레트가 필요 없다
	NOVA_API bool ShadowImpostors();

	NOVA_API nlohmann::json Info();   // 지난 프레임: 팔레트 렌더러 · 패스별 인스턴스 · 그리기 수
}
