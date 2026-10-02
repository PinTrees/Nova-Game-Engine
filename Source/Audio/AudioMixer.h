#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

struct IXAudio2Voice;
struct IXAudio2SubmixVoice;

// Unity 의 Audio Mixer 에셋 (.mixer, JSON).
//  - Groups: Master 아래 트리 (Music, SFX ...). 그룹마다 XAudio2 서브믹스 보이스 → 부모 → 마스터.
//    Volume(dB), Mute, Solo, Bypass Effects, 이펙트: Lowpass · Highpass (보이스 필터) / Echo · Reverb (XAudio2 이펙트)
//  - Snapshots: 모든 값(그룹 볼륨·이펙트 파라미터)을 스냅숏마다 저장. 첫 스냅숏 = 시작 스냅숏, TransitionTo 로 부드럽게 바꾼다
//  - Exposed Parameters: 이름으로 스크립트가 바꾸는 값 (SetFloat / GetFloat / ClearFloat — 스냅숏보다 우선)
//  - 값 키: 그룹 볼륨 "g<id>/vol", 이펙트 파라미터 "fx<id>/p<n>" (id 는 이름·순서가 바뀌어도 그대로)
class AudioMixer
{
public:
	enum EffectType { Lowpass = 0, Highpass = 1, Echo = 2, Reverb = 3, EffectTypeCount = 4 };

	struct Effect
	{
		int Id = 0;
		int Type = Lowpass;
		bool Bypass = false;
	};

	struct Group
	{
		int Id = 0;
		std::string Name;
		int Parent = -1;             // 그룹 인덱스 (-1 = 루트 Master)
		bool Mute = false;
		bool Solo = false;
		bool BypassEffects = false;
		std::vector<Effect> Effects;
	};

	struct Snapshot
	{
		std::string Name;
		std::map<std::string, float> Values;
	};

	struct Exposed
	{
		std::string Name;
		std::string Key;
	};

	// 이펙트 파라미터 정보 (이름 · 기본값 · 범위 · 단위)
	struct ParamInfo { const char* Name; float Default, Min, Max; const char* Unit; };
	static const char* EffectName(int type);
	static int EffectParamCount(int type);
	static const ParamInfo& EffectParam(int type, int index);
	static int ReverbPresetCount();
	static const char* ReverbPresetName(int index);

	std::string Path;                // Assets\... .mixer
	std::vector<Group> Groups;       // [0] = Master
	std::vector<Snapshot> Snapshots; // [0] = 시작 스냅숏
	std::vector<Exposed> ExposedParams;
	int NextId = 1;
	int EditSnapshot = 0;            // 편집 중인 스냅숏 (Play 가 아닐 때 이 값으로 들린다)

	std::string Name() const;
	bool Save() const;
	std::string ToJsonString() const;
	void ApplyJson(const std::string& text);

	static std::shared_ptr<AudioMixer> Load(const std::string& path);   // 같은 경로 = 같은 객체
	static std::shared_ptr<AudioMixer> Create(const std::string& path); // Master + 스냅숏 하나
	static std::vector<std::string> FindAll();                           // 프로젝트 Assets 의 .mixer

	// ---- 편집 (구조가 바뀌면 보이스를 다시 만든다)
	int FindGroup(const std::string& name) const;
	int AddGroup(int parent, const std::string& name);
	void RemoveGroup(int index);               // 자식도 함께 (Master 는 못 지움)
	bool RenameGroup(int index, const std::string& name);
	void AddEffect(int group, int type);
	void RemoveEffect(int group, int effect);
	int AddSnapshot(const std::string& name);  // 지금 편집 중인 스냅숏을 복사
	void RemoveSnapshot(int index);
	int FindSnapshot(const std::string& name) const;
	std::string Expose(const std::string& key);   // 이름을 지어 노출 ("MyExposedParam")
	void Unexpose(int index);
	int FindExposed(const std::string& name) const;
	void StructureChanged() { ++m_Structure; }

	static std::string VolumeKey(const Group& g) { return "g" + std::to_string(g.Id) + "/vol"; }
	static std::string EffectKey(const Effect& e, int param) { return "fx" + std::to_string(e.Id) + "/p" + std::to_string(param); }
	std::string KeyLabel(const std::string& key) const;   // "Music Volume", "SFX Lowpass Cutoff freq"
	float DefaultValue(const std::string& key) const;
	float GetValue(int snapshot, const std::string& key) const;
	void SetValue(int snapshot, const std::string& key, float value);   // 편집: 스냅숏 값 (Play 중이면 들리는 값도)

	// ---- 실행
	IXAudio2Voice* GroupVoice(int group);      // 그룹의 서브믹스 (없으면 만든다, 오디오가 없으면 nullptr)
	unsigned Generation() const { return m_Generation; }   // 보이스를 다시 만들 때마다 증가 → 소스가 다시 연결
	float LiveValue(const std::string& key) const;
	bool SetFloat(const std::string& exposedName, float value);
	bool GetFloat(const std::string& exposedName, float& value) const;
	bool ClearFloat(const std::string& exposedName);
	bool TransitionTo(const std::string& snapshot, float seconds);
	float GroupLevelDb(int group) const;       // 최근 출력 최대 레벨 (Audio Mixer 창 · 검사)

	static void UpdateAll(float dt);           // AudioManager::Update 에서 매 프레임
	static void ForgetAllVoices();             // 엔진을 끄기 직전 (보이스는 엔진이 같이 지운다)

	~AudioMixer();

private:
	struct GroupRuntime
	{
		IXAudio2SubmixVoice* Voice = nullptr;
		std::vector<int> ChainIndex;           // 이펙트마다 효과 체인 위치 (-1 = 필터 · 없음)
		std::vector<bool> Enabled;
		std::vector<std::vector<float>> Applied;
		float Level = 0.0f;
		double LevelTime = 0.0;
	};
	std::vector<GroupRuntime> m_Runtime;
	std::vector<IXAudio2SubmixVoice*> m_Retired;   // 소스가 다시 연결한 뒤 지운다
	int m_RetireFrames = 0;
	unsigned m_Structure = 1, m_BuiltStructure = 0, m_Generation = 0;
	bool m_LivePlaying = false;
	std::map<std::string, float> m_Live, m_Overrides, m_From, m_To;
	float m_TransTime = 0.0f, m_TransDuration = 0.0f;

	std::vector<std::string> AllKeys() const;
	const Effect* FindEffect(int id, int* group = nullptr) const;
	void BuildVoices();
	void DestroyRetired(bool force);
	void Tick(float dt);
	void ApplyToVoices();
};
