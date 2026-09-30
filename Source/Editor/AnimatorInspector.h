#pragma once
#include "SelectionManager.h"

class AnimatorController;

// Animator 창에서 고른 상태 / 전이, 그리고 .controller 에셋의 Inspector (Unity 모양)
namespace AnimatorInspector
{
	void DrawSelection(const AnimatorSelection& selection);
	void DrawController(const std::shared_ptr<AnimatorController>& controller);
}
