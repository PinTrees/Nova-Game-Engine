#pragma once
#include "Component.h"

// 프레임 애니메이션 (뼈대 없는 2D — 도트 캐릭터 · 효과): 스프라이트를 차례로 바꿔 보여 준다.
//  - 클립 = `.spriteanim` (JSON: fps, loop, frames = 스프라이트 경로 목록 — 보통 잘라 놓은 "그림.png#이름")
//    텍스처 가져오기 설정 (Sprite Mode = Multiple) 의 "Create Sprite Animation" 또는 CLI `sprite-slice --animation` 으로 만든다
//  - Sprite Animator 컴포넌트: 같은 GameObject 의 Sprite Renderer 의 sprite 를 바꾼다. 클립 여러 개 (이름 = 파일 이름) 중 Play("Run")
//  - Unity 에서는 Animator + AnimationClip (sprite 키) 로 하는 일을 한 컴포넌트로 (Play 중 Update)
struct SpriteAnimClip
{
	std::string Name;                 // 파일 이름 (확장자 없이)
	float Fps = 12.0f;
	bool Loop = true;
	std::vector<std::string> Frames;  // 스프라이트 경로
	float Length() const { return Fps > 0 ? Frames.size() / Fps : 0.0f; }
};

namespace SpriteAnimClips
{
	// 파일 시각이 바뀔 때까지 캐시
	NOVA_API std::shared_ptr<const SpriteAnimClip> Load(const std::string& path);
	NOVA_API bool Save(const std::string& path, const SpriteAnimClip& clip);
	// Project 창의 .spriteanim (Create 메뉴 · Inspector) — 편집기 시작 때
	void RegisterEditorAssetType();
}

class NOVA_API SpriteAnimator : public Component
{
public:
	SpriteAnimator();

	void Start() override;
	void Update() override;

	bool Play(const std::string& clip);   // 이름 (파일 이름) — 처음부터
	void Stop() { m_Playing = false; }
	bool IsPlaying() const { return m_Playing; }
	std::string CurrentClip() const;
	int Frame() const { return m_Frame; }
	float Speed = 1.0f;
	bool PlayOnAwake = true;

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "sprite_animator"; }

private:
	std::vector<std::string> m_Clips;   // .spriteanim 경로
	int m_Default = 0;
	int m_Current = -1;
	float m_Time = 0.0f;
	int m_Frame = -1;
	bool m_Playing = false;
	void ShowFrame(const SpriteAnimClip& c, int frame);

	GENERATE_COMPONENT_BODY(SpriteAnimator)
};
REGISTER_COMPONENT(SpriteAnimator)
