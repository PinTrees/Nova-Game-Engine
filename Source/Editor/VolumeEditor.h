#pragma once
#include <memory>
#include <string>

class VolumeProfile;

// Volume Profile Inspector (Unity 의 VolumeProfileEditor / VolumeComponentEditor).
// Volume 컴포넌트, Project 창에서 고른 .volumeprofile, Project Settings > Graphics 의 기본 프로파일이 함께 쓴다.
namespace VolumeEditor
{
	// 효과 목록(헤더 + 파라미터) + Add Override.
	// isDefault = 기본 프로파일: Override 체크 없이 모든 값이 기준값으로 쓰인다 (Unity 6 의 Default Volume Profile).
	// 편집이 끝나면 파일로 저장하고 Undo 에 기록한다.
	void DrawProfile(const std::shared_ptr<VolumeProfile>& profile, bool isDefault = false);

	// Profile 행: [이름 ⊙][New][Clone]. ⊙ = 프로젝트의 .volumeprofile 목록, Project 창에서 끌어 놓기 가능.
	// newBaseName = New 로 만들 파일 이름. 경로가 바뀌면 true.
	bool ProfileField(const char* label, std::string& path, const std::string& newBaseName = "New Volume Profile", bool showNewClone = true);
}
