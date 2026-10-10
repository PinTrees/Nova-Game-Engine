#pragma once
#include <memory>
#include <string>
#include <vector>
#include "NovaApi.h"

class SkinnedMesh;
class SkeletonAvataData;
class UMaterial;
struct AnimationClip;

// 군중 애니메이션 굽기 (docs/BATTLE.md): 클립을 프레임마다 본 팔레트로 미리 계산해 GPU 버퍼 하나에 둔다
//  (GPU Gems 3 「Animated Crowd Rendering」 의 팔레트 스키닝 + 인스턴싱 — 병사마다 CPU 자세 계산 없이 시간만 넘긴다).
//  멀리서는 같은 프레임들을 8 방향에서 찍은 애니메이션 임포스터 (알베도 · 법선 아틀라스) 로 — 따로 설정하지 않아도 함께 굽는다.
namespace CrowdAnimation
{
	struct ClipSource
	{
		std::string Name;     // 전투 상태 이름 (Idle · Walk · Attack · Die)
		std::string Path;     // 클립 파일 (프로젝트 · 엔진 상대)
		int Index = 0;        // 파일 안 클립 번호
		bool Loop = true;
		bool Reverse = false; // 거꾸로 (일어나기 → 쓰러지기)
		std::shared_ptr<AnimationClip> Source;   // 있으면 Path 대신 이 클립 (재생 중인 클립)
	};

	struct Clip
	{
		std::string Name;
		uint32_t FirstFrame = 0;   // 팔레트 버퍼의 프레임 번호
		uint32_t Frames = 0;
		float Duration = 1.0f;     // 초
		bool Loop = true;
		uint32_t ImpostorRow = 0;  // 임포스터 아틀라스의 첫 줄
	};

	struct Baked
	{
		std::shared_ptr<SkinnedMesh> Mesh;
		std::shared_ptr<SkeletonAvataData> Skeleton;
		std::vector<std::shared_ptr<UMaterial>> Materials;   // 서브셋 재질 칸 (모델의 기본 재질)
		uint32_t Bones = 0;
		float Fps = 30.0f;
		std::vector<Clip> Clips;
		uint32_t TotalFrames = 0;
		ComPtr<GfxBuffer> Palettes;                 // float4 × 3 × Bones × TotalFrames (바뀌지 않음)
		ComPtr<GfxShaderResourceView> PaletteSrv;
		float Height = 1.8f, HalfWidth = 0.5f, CenterY = 0.9f;   // 모든 프레임을 덮는 크기 (m) — 임포스터 칸 · 컬링 구

		// 임포스터: 가로 = 방향 (ImpostorYaws), 세로 = 클립마다 ImpostorFrames 줄
		static constexpr int ImpostorYaws = 8;
		static constexpr int ImpostorFrames = 8;
		static constexpr int CellW = 64, CellH = 128;
		uint32_t ImpostorRows = 0;
		ComPtr<GfxShaderResourceView> ImpostorAlbedo, ImpostorNormal;
		bool ImpostorTried = false;

		int FindClip(const std::string& name) const;
		uint32_t FrameElement(uint32_t frame) const { return frame * Bones * 3; }   // 팔레트 버퍼의 float4 칸
	};

	// 모델 (스킨 메시 index 번째) 과 클립들을 굽는다 (같은 인자면 캐시). 실패하면 nullptr
	NOVA_API std::shared_ptr<Baked> Bake(const std::string& modelPath, int meshIndex, const std::vector<ClipSource>& clips);
	// 이미 읽은 메시 · 스켈레톤 · 재질로 (자동 임포스터 — 렌더러가 가진 것). 같은 인자면 캐시
	NOVA_API std::shared_ptr<Baked> Bake(const std::shared_ptr<SkinnedMesh>& mesh, const std::shared_ptr<SkeletonAvataData>& skeleton,
		const std::vector<std::shared_ptr<UMaterial>>& materials, const std::vector<ClipSource>& clips, int bindMode = -1);
	// 임포스터 아틀라스에서 클립 · 시간의 줄
	NOVA_API float ImpostorRow(const Baked& baked, int clip, float time);
	// 기본 캐릭터 + 전투 클립 (Idle · Walk · Attack = Wave · Die = GetUpBack 거꾸로)
	NOVA_API std::vector<ClipSource> DefaultBattleClips();
	// 임포스터 아틀라스 굽기는 SkinnedInstancing::BakeImpostor (같은 셰이더)
	NOVA_API void Clear();
}
