#pragma once
#include <string>
#include <memory>

class VolumeProfile;

// 프로젝트별 그래픽 설정 (Unity: Project Settings > Graphics > URP).
// <프로젝트>/ProjectSettings/GraphicsSettings.json 에 저장한다.
//  - Default Volume Profile: 씬에 Volume 이 없어도 항상 먼저 적용되는 기본 효과 값
//  - Rendering Path (URP Universal Renderer 와 같은 이름): Forward (빛 4 개) · Forward+ (클러스터 — 기본) · Deferred (G-버퍼 + 클러스터)
namespace RenderPipelineSettings
{
	enum class RenderingPath { Forward = 0, ForwardPlus = 1, Deferred = 2 };
	RenderingPath GetRenderingPath();
	// Multithreaded Rendering (Unity Player Settings 와 같은 이름): 그리기 명령을 렌더 스레드가 실행 · Present (DirectX 11, RenderThread.h)
	bool MultithreadedRendering();
	void SetMultithreadedRendering(bool on);   // 저장
	void SetRenderingPath(RenderingPath path);   // 저장 + Forward+ 클러스터 켜기 · 끄기
	const char* RenderingPathName(RenderingPath path);   // "Forward" · "Forward+" · "Deferred"
	bool RenderingPathFromName(const std::string& name, RenderingPath& out);
	void RegisterEditor();   // CLI: nova renderpath [get|set --path Forward|Forward+|Deferred]

	const std::string& DefaultVolumeProfilePath();
	void SetDefaultVolumeProfilePath(const std::string& path);
	std::shared_ptr<VolumeProfile> DefaultVolumeProfile();

	// 기본 프로파일이 없으면 모든 효과(기본값)를 담은 Assets/Settings/DefaultVolumeProfile.volumeprofile 을 만들어 지정한다
	std::shared_ptr<VolumeProfile> EnsureDefaultVolumeProfile();
	// 새 씬의 Global Volume 이 쓰는 프로파일 (Unity URP 의 SampleSceneProfile 처럼 Bloom 이 켜져 있다).
	// Assets/Settings/SampleSceneProfile.volumeprofile 이 없으면 만든다. 경로를 돌려준다
	std::string EnsureSampleSceneProfile();

	void Reload();   // 프로젝트를 연 뒤 한 번 (파일에서 다시 읽기)
}
