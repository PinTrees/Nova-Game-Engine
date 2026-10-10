#pragma once
#include <memory>
#include "NovaApi.h"

class SkinnedMesh;
class MeshGeometry;
class SkeletonAvataData;

// 스킨 메시 LOD 자동 생성 (군중 원거리 단순화): meshoptimizer 로 서브셋마다 삼각형을 줄인다.
//  단계마다 쓰는 정점만 모은 정점 버퍼 (본 줄이기로 가중치가 달라진다 — BlendShape 를 섞는 렌더러는 원본 단계로 그린다).
//  처음 찾을 때 작업 스레드에서 줄이고 (사람 3.7 만 정점: 수십 ms), 다 될 때까지는 원본으로 그린다
namespace SkinnedLod
{
	constexpr int kLevels = 4;                       // 0 = 원본, 1 · 2 · 3 = 줄인 것
	constexpr float kRatio[kLevels] = { 1.0f, 0.3f, 0.1f, 0.035f };   // 남길 삼각형 비율

	// base 의 level 단계 지오메트리 (서브셋 수 · 순서 같음). 0 이거나 아직 만드는 중이거나 실패하면 nullptr (= 원본)
	//  skeleton = 본 줄이기 (Skeletal LOD — 처음 만들 때만 쓴다): 단계마다 사람 본 22 · 12 · 7 개만 남기고 (가장 먼 단계는 정점마다 본 하나)
	NOVA_API MeshGeometry* Get(const std::shared_ptr<SkinnedMesh>& base, int level, const std::shared_ptr<SkeletonAvataData>& skeleton = nullptr);
	// 만들어 두기만 (군중을 놓을 때 — 첫 프레임에 기다리지 않게)
	NOVA_API void Request(const std::shared_ptr<SkinnedMesh>& base, const std::shared_ptr<SkeletonAvataData>& skeleton = nullptr);
	NOVA_API int Bones(const std::shared_ptr<SkinnedMesh>& base, int level);   // 단계의 정점이 가리키는 본 수 (만들지 않았으면 -1)
	// 단계의 정점이 가리키는 팔레트 본 번호 (아직 없으면 nullptr). 읽기만 — Animator 가 작업 스레드에서 그 본만 계산한다
	NOVA_API const std::vector<int>* UsedBones(const SkinnedMesh* base, int level);
	// 다 만들 때까지 기다린다 (검사 · 군중 놓기)
	NOVA_API bool WaitReady(const std::shared_ptr<SkinnedMesh>& base);
	NOVA_API int Triangles(const std::shared_ptr<SkinnedMesh>& base, int level);   // 만들지 않았으면 -1
	NOVA_API void Clear();   // 캐시를 비운다

	// 메시 단계 다음 = 애니메이션 임포스터 (카메라를 보는 사각형 하나 — SkinnedInstancing 이 재생 중인 클립으로 굽는다)
	constexpr int kImpostorLevel = kLevels;
	extern NOVA_API bool Impostors;           // 기본 켬 (CLI crowd lod --impostors)
	extern NOVA_API float ImpostorScreen;     // 화면 높이 비율이 이보다 작으면 임포스터 (기본 0.035 — 1080p 에서 약 38 픽셀)

	// 화면 높이 비율 (경계 상자의 가장 긴 축 / 화면 높이 — Unity LOD Group 과 같다) → 단계 (0 ~ kImpostorLevel). 앞 단계 경계에 걸치면 바꾸지 않는다 (깜빡임 방지)
	NOVA_API int SelectLevel(float screenHeight, int previous);
	extern NOVA_API int ForceLevel;   // 0 이상이면 모든 렌더러를 이 단계로 (검사 — CLI crowd lod --force)
	extern NOVA_API float Bias;   // 화면 높이에 곱한다 (1 = 기본, 클수록 늦게 줄인다) — CLI crowd lodBias
}
