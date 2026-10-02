#pragma once
#include <memory>
#include <string>

struct IXAudio2SourceVoice;
struct IXAudio2Voice;
struct IXAudio2;
class AudioClip;

// XAudio2 오디오 엔진 (Windows 10 기본 포함). 처음 쓸 때 초기화한다.
//  - 소스 보이스 생성, 믹스(볼륨/팬), 3D 감쇠·방향 계산
//  - 편집기: 일시정지 동안 엔진 정지, Play 종료 시 One Shot 정리, Game 뷰 Mute, 에셋 미리 듣기
//  - Stats: 출력 레벨(dB), 클리핑, DSP 부하, 재생 중인 보이스 수
namespace AudioManager
{
	bool Init();          // 실패하면 false (오디오 장치 없음 등) — 이후 모든 호출은 조용히 무시
	void Shutdown();
	bool IsAvailable();

	void Update();        // 매 프레임 (App 루프): 리스너, 일시정지, One Shot 정리, 통계, Audio Mixer

	IXAudio2* Engine();
	unsigned SampleRate();       // 마스터 출력
	unsigned OutputChannels();

	// 소스 보이스 (clip 형식으로, 최대 피치 3배). output = Audio Mixer 그룹 (nullptr = 마스터). 실패하면 nullptr
	IXAudio2SourceVoice* CreateVoice(const AudioClip& clip, IXAudio2Voice* output = nullptr);
	void SetOutput(IXAudio2SourceVoice* voice, IXAudio2Voice* output);
	void DestroyVoice(IXAudio2SourceVoice*& voice);
	// clip 전체를 제출 (loop = 무한 반복)
	bool Submit(IXAudio2SourceVoice* voice, const AudioClip& clip, bool loop);
	// 볼륨/팬(-1 왼쪽 .. 1 오른쪽) 적용. 입력 채널 수 = clip 채널, 출력 채널 = 보내는 곳 (0 = 마스터, 그룹은 2)
	void ApplyMix(IXAudio2SourceVoice* voice, int inputChannels, float volume, float pan, int outputChannels = 0);
	void SetPitch(IXAudio2SourceVoice* voice, float pitch);

	// 리스너 기준 3D: 거리 감쇠(0..1)와 좌우 팬. rolloff 0 로그, 1 선형
	void Spatialize(const Vec3& sourcePos, float minDistance, float maxDistance, int rolloff, float& gain, float& pan);
	Vec3 ListenerPosition();
	Vec3 ListenerVelocity();
	// 도플러: 소스·리스너가 서로 다가가면 > 1 (음이 높아짐), 멀어지면 < 1. level = Unity 의 Doppler Level
	float DopplerFactor(const Vec3& sourcePos, const Vec3& sourceVelocity, float level);

	// Unity 의 AudioSource.PlayOneShot / AudioSource.PlayClipAtPoint (끝나면 자동 정리)
	void PlayOneShot(const std::shared_ptr<AudioClip>& clip, float volume, float pan = 0.0f, float pitch = 1.0f, IXAudio2Voice* output = nullptr);
	void StopAllOneShots();

	// 편집기 미리 듣기 (Project 창 / Inspector)
	void PlayPreview(const std::shared_ptr<AudioClip>& clip);
	void StopPreview();
	bool IsPreviewPlaying();

	void SetMuted(bool muted);   // Game 뷰 Mute Audio
	bool IsMuted();

	struct Stats
	{
		bool Available = false;
		float LevelDb = -80.0f;     // 최근 출력 최대 레벨
		float ClippingPercent = 0.0f;
		float DspLoadPercent = 0.0f;
		int ActiveVoices = 0;
	};
	Stats GetStats();
}
