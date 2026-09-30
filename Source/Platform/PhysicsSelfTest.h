#pragma once

class Scene;

// (개발/검증용) 물리 자체 검사 장면.
// NOVA_PHYSICS_TEST=<로그 파일 경로> 로 에디터를 실행하면 시작 씬에 검사용 오브젝트를 추가하고(저장하지 않음),
// Play 중 고정 스텝마다 위치/속도/각속도/회전을 기록해 이론값과 비교할 수 있게 한다.
namespace PhysicsSelfTest
{
	void Build(Scene* scene);

	// (개발/검증용) NOVA_PARENT_TEST=<로그 파일>: 부모 변경 시 월드 위치 유지, 자식 저장/다시 읽기 검사
	void RunParentTest(Scene* scene, const char* logPath);

	// (개발/검증용) NOVA_ANIM_TEST=<로그 파일>: 기본 캐릭터 + Idle 을 씬에 추가하고 가져오기/스키닝 결과를 기록
	void RunAnimationTest(Scene* scene, const char* logPath);

	// (개발/검증용) NOVA_ANIMATOR_TEST=<로그 파일>: 상태 3개짜리 컨트롤러를 만들어 전이/트리거/Any State/크로스페이드를 검사
	void RunAnimatorTest(Scene* scene, const char* logPath);

	// (개발/검증용) NOVA_TERRAIN_TEST=<로그 파일>: 브러시로 지형을 만들고(언덕/고원/다듬기/텍스처) 물리 공을 올려 둔다
	void RunTerrainTest(Scene* scene, const char* logPath);
}
