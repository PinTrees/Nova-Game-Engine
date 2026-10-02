#pragma once
#include "SelectionManager.h"

class AnimatorController;

// Animator 창 선택 (State >= 0 이면 상태, Transition >= 0 이면 전이)
struct AnimatorSelection
{
	std::shared_ptr<AnimatorController> Controller;
	int Layer = 0;
	int State = -1;
	int Transition = -1;
};

// Animator 창에서 고른 상태 / 전이, 그리고 .controller 에셋의 Inspector (Unity 모양)
namespace AnimatorInspector
{
	void DrawSelection(const AnimatorSelection& selection);
	void DrawController(const std::shared_ptr<AnimatorController>& controller);
	// 고르기 = 엔진의 "사용자 정의 선택" (Inspector 가 DrawSelection 으로 그린다, 되돌리기 감시 포함)
	void Select(const std::shared_ptr<AnimatorController>& controller, int layer, int state, int transition);
	// 지금 선택이 Animator 창 것이면 (아니면 nullptr)
	const AnimatorSelection* Current();
}
