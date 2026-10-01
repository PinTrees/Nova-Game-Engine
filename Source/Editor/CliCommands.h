#pragma once

// NOVA CLI 명령 (CliServer 에 등록). 씬·오브젝트·컴포넌트 조회/수정, 씬 열기·저장, Play, Scene 카메라, 스크린샷, 빌드 …
//  바꾸는 명령은 Undo 한 단계로 남는다 ("CLI <명령>").
namespace CliCommands
{
	void RegisterAll();
}
