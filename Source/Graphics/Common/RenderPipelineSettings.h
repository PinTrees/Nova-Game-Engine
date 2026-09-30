#pragma once
#include <string>
#include <memory>

class VolumeProfile;

// 프로젝트별 그래픽 설정 (Unity: Project Settings > Graphics > URP).
// <프로젝트>/ProjectSettings/GraphicsSettings.json 에 저장한다.
//  - Default Volume Profile: 씬에 Volume 이 없어도 항상 먼저 적용되는 기본 효과 값
namespace RenderPipelineSettings
{
	const std::string& DefaultVolumeProfilePath();
	void SetDefaultVolumeProfilePath(const std::string& path);
	std::shared_ptr<VolumeProfile> DefaultVolumeProfile();

	// 기본 프로파일이 없으면 모든 효과(기본값)를 담은 Assets/Settings/DefaultVolumeProfile.volumeprofile 을 만들어 지정한다
	std::shared_ptr<VolumeProfile> EnsureDefaultVolumeProfile();

	void Reload();   // 프로젝트를 연 뒤 한 번 (파일에서 다시 읽기)
}
