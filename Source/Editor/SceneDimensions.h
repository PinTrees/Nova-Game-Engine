#pragma once

// Scene 뷰 치수: 선택한 GameObject (자식 포함) 의 메시 월드 범위에 너비 · 높이 · 깊이 (m) 를 모서리 선과 글자로.
//  블록아웃에서 문 높이 · 점프 높이 · 통로 폭을 바로 읽는다 (Scene 뷰 Gizmos 메뉴의 Selection Dimensions)
namespace SceneDimensions
{
	void Draw();
	// 선택한 오브젝트의 월드 범위 (메시가 없으면 false) — CLI · 검사
	bool SelectionBounds(Vec3& mn, Vec3& mx);
}
