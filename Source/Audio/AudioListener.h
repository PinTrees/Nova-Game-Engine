#pragma once
#include "Component.h"

// Unity 의 Audio Listener: 3D 사운드를 듣는 위치/방향 (보통 Main Camera 에 붙는다).
// 씬에 없으면 Game 뷰 카메라 위치를 쓴다.
class AudioListener : public Component
{
private:
	static std::vector<AudioListener*> s_All;

public:
	AudioListener();
	virtual ~AudioListener();

	// 켜져 있고 활성인 첫 리스너 (없으면 nullptr)
	static AudioListener* Active();

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "audio_listener"; }

	GENERATE_COMPONENT_BODY(AudioListener)
};

REGISTER_COMPONENT(AudioListener)
