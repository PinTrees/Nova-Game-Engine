#pragma once
#include "Component.h"
#include "SpriteBatch.h"
#include "Anim2DDocument.h"

// 2D 뼈대 (.skel2d — Window > 2D Animator) 를 씬에서 그리고 재생한다 (Spine 의 SkeletonAnimation).
//  - 뼈대 1 픽셀 = 1 / Pixels Per Unit 월드 단위 (기본 100 = SpriteRenderer 와 같은 크기), 원점 = GameObject 위치
//  - Play 중 Update 에서 시간이 흐른다 (Time Scale, Loop). 편집 중에는 그 애니메이션의 처음 자세 (애니메이션이 없으면 셋업)
//  - 슬롯 순서대로 그림 사각형 → SpriteBatch (Order in Layer · 색 · Flip X)
//  - .skel2d 파일을 고치면 (2D Animator 에서 저장) 다시 읽는다
class SpriteSkinnedRenderer : public Component, public SpriteSource
{
public:
	SpriteSkinnedRenderer();
	~SpriteSkinnedRenderer() override;

	void Start() override { TrackTime = 0.0f; }   // Play 를 누르면 처음부터 (편집 중 미리 보기 시각은 버린다)
	void Update() override;

	// Spine 의 AnimationState.SetAnimation: 처음부터 재생 (없는 이름이면 false)
	bool Play(const std::string& animation, bool loop);
	const std::string& GetAnimation() const { return m_Animation; }
	float TrackTime = 0.0f;   // 지금 재생 시각 (초)
	float TimeScale = 1.0f;
	bool Loop = true;
	bool FlipX = false;
	int SortingOrder = 0;
	int SortingLayerId = 0;   // Tags and Layers 의 Sorting Layer
	float Color[4] = { 1, 1, 1, 1 };
	bool IsComplete() const;   // 반복이 아닌 애니메이션이 끝났나

	const std::string& GetSkeleton() const { return m_Skeleton; }
	void SetSkeleton(const std::string& path);
	Anim2D::Document* Skeleton();   // 읽은 뼈대 (없으면 nullptr)

	// SpriteSource
	GameObject* SpriteOwner() const override { return m_pGameObject; }
	bool SpriteEnabled() const override { return m_Enabled; }
	void CollectSprites(SpriteBatch& batch) override;
	bool SpriteLocalBounds(Vec3& bmin, Vec3& bmax) override;

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "sprite_skinned_renderer"; }

private:
	std::string m_Skeleton;     // 프로젝트 기준 .skel2d
	std::string m_Animation;    // 비면 첫 애니메이션
	float m_PixelsPerUnit = 100.0f;

	std::unique_ptr<Anim2D::Document> m_Doc;
	std::string m_LoadedPath;
	long long m_LoadedStamp = 0;
	unsigned long long m_CheckedAt = 0;
	std::string m_Error;

	struct Tex { ComPtr<GfxShaderResourceView> Srv; float W = 0, H = 0; bool Point = false; };
	std::map<std::string, Tex> m_Textures;
	const Tex* Texture(const std::string& image);
	void PoseNow();
	// 지금 자세의 그림 사각형 → GameObject 로컬 (Pixels Per Unit · Flip X)
	template <class Fn> void ForEachQuad(Fn fn);

	GENERATE_COMPONENT_BODY(SpriteSkinnedRenderer)
};
REGISTER_PACKAGE_COMPONENT(SpriteSkinnedRenderer)
