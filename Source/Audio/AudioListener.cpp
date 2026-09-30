#include "pch.h"
#include "AudioListener.h"

std::vector<AudioListener*> AudioListener::s_All;

AudioListener::AudioListener()
{
	m_InspectorTitleName = "Audio Listener";
	s_All.push_back(this);
}

AudioListener::~AudioListener()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

AudioListener* AudioListener::Active()
{
	for (AudioListener* l : s_All)
	{
		if (!l->IsEnabled() || l->GetGameObject() == nullptr)
			continue;
		bool active = true;
		for (GameObject* g = l->GetGameObject(); g != nullptr; g = g->GetParent())
			if (!g->IsActive()) { active = false; break; }
		if (active)
			return l;
	}
	return nullptr;
}

void AudioListener::OnInspectorGUI()
{
	// Unity 의 Audio Listener 는 설정 항목이 없다
}

GENERATE_COMPONENT_FUNC_TOJSON(AudioListener)
{
	json j;
	SERIALIZE_TYPE(j, AudioListener);
	j["enabled"] = m_Enabled;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(AudioListener)
{
	m_Enabled = j.value("enabled", true);
}
