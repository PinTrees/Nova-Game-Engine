#pragma once
#include "EditorWindow.h"

class AudioMixer;

// Unity 의 Audio Mixer 창 (Window > Audio Mixer, .mixer 더블클릭).
//  - 위: 믹서 고르기 · 새 믹서
//  - 왼쪽: Snapshots (★ = 시작 스냅숏, 고른 스냅숏을 편집 — Play 가 아닐 때 그 값으로 들린다), Groups 트리, Exposed Parameters
//  - 오른쪽: 그룹마다 채널 스트립 (레벨 미터 · 볼륨 페이더 dB · Mute / Solo / Bypass · 이펙트 · Add Effect)
//  - 아래: 고른 그룹의 볼륨·이펙트 파라미터 (오른쪽 클릭 → 스크립트에 노출)
class AudioMixerWindow
	: public EditorWindow
{
public:
	AudioMixerWindow();

	static void Open(const std::string& mixerPath = std::string());
	static void Close();
	static bool IsOpen();

protected:
	void BeforeBegin() override;
	void PushStyle() override;
	void PopStyle() override;
	void OnRender() override;

private:
	static AudioMixerWindow* s_Instance;
	std::string m_Path;
	int m_Group = 0;
	bool m_FocusNext = false;
	int m_Renaming = -1;          // 0 그룹, 1 스냅숏, 2 노출 파라미터
	int m_RenameIndex = -1;
	char m_RenameBuf[96] = {};

	std::shared_ptr<AudioMixer> Current();
	void DrawLeft(AudioMixer& m, float height);
	void DrawStrips(AudioMixer& m, float height);
	void DrawDetails(AudioMixer& m);
	void ParamSlider(AudioMixer& m, const std::string& key, const char* label, float min, float max, const char* format);
};
