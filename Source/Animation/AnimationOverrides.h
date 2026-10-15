#pragma once
#include <memory>
#include <vector>

class GameObject;
struct AnimationClip;

// 바깥에서 Animator 의 자세를 덮는 클립 (Timeline 의 Animation Track — Unity 의 PlayableGraph 출력과 같은 자리).
//  Timeline 패키지가 프레임마다 Set, Animator 패키지가 자세를 계산할 때 Get — 두 패키지가 서로의 DLL 을 몰라도 된다.
//  클립들은 무게대로 섞이고 (합이 1 넘으면 1 로), 그 합만큼 컨트롤러 자세 위에 덮인다 (합이 1 이면 컨트롤러 무시).
//  Animator 에 컨트롤러가 없어도 덮는 클립이 있으면 자세를 계산한다. 메인 스레드가 쓰고, 자세 평가 (작업 스레드) 가 읽는다
namespace AnimationOverrides
{
	struct Clip
	{
		std::shared_ptr<AnimationClip> Source;
		float Time = 0.0f;      // 클립 안 시간 (초)
		float Weight = 1.0f;
		bool Loop = false;
	};
	NOVA_API void Set(const GameObject* go, const std::vector<Clip>& clips);
	NOVA_API void Clear(const GameObject* go);
	NOVA_API bool Get(const GameObject* go, std::vector<Clip>& out);
	NOVA_API bool Has(const GameObject* go);
	// Set · Clear 할 때마다 늘어나는 수 (편집 중 Animator 가 미리 보기를 다시 그릴지)
	NOVA_API unsigned Revision(const GameObject* go);
}
