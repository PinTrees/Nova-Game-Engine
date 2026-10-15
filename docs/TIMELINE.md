# Timeline (컷신 · 연출)

**Timeline 패키지 (`com.nova.timeline`)** 는 Unity **Timeline** 과 같은 이름 · 구조의 연출 도구입니다. 시간 축 위에 트랙 · 클립 · 마커를 놓은 **Timeline 에셋 (`.playable`)** 을 GameObject 의 **Playable Director** 가 재생합니다. 어느 오브젝트가 어느 트랙을 따를지 (바인딩) 는 에셋이 아니라 디렉터가 가지므로, 같은 Timeline 을 다른 씬 · 다른 배우로 다시 쓸 수 있습니다.

## 시작하기

1. `nova package add com.nova.timeline` (또는 Window > Package Manager). Animator 패키지 (`com.nova.animation`) 가 함께 필요합니다
2. Project 창에서 Create > **Timeline** → `New Timeline.playable`
3. 연출을 맡을 GameObject 에 Add Component > **Playable Director**, Playable 칸에 그 에셋
4. Window > **Timeline** 에서 트랙을 더하고 (+ 또는 빈 곳 오른쪽 클릭), 트랙 머리의 칸에 배우 (GameObject) 를 묶는다
5. 재생 헤드를 끌면 편집 중에도 장면이 바로 바뀝니다 (미리 보기). Play 하면 Play On Awake 로 처음부터

## 트랙

| 트랙 (Unity 이름) | 묶는 것 | 하는 일 |
|---|---|---|
| **Animation Track** | Animator 가 있는 GameObject | 클립 (FBX · GLB 안 애니메이션) 이 그 시간 동안 Animator 의 자세를 덮는다. 클립이 겹치면 자동으로 섞고 (Unity 의 겹침 블렌드), Ease In / Out 으로 컨트롤러 자세와 섞는다. 클립 밖에서는 Animator (상태 머신) 그대로 |
| **Activation Track** | GameObject | 클립 동안만 켜고, 밖에서는 끈다. Stop 하면 처음 상태로 |
| **Audio Track** | Audio Source 가 있는 GameObject | 클립 시작 시간에 그 소리 파일을 재생 |
| **Signal Track** | 스크립트가 붙은 GameObject | 마커 시간에 그 오브젝트 스크립트의 메서드를 부른다 (Unity 의 Signal Emitter + Signal Receiver 를 하나로). 인자 없음 또는 string · float · int 하나 |
| **Cinemachine Track** | (없음) | 클립 (Shot) 마다 카메라 오브젝트 (Cinemachine Camera) 를 그 동안만 켠다 — Cinemachine Brain 이 섞는다 ([CINEMACHINE.md](CINEMACHINE.md)) |

트랙은 이름 대신 **id** (`Animation1` 처럼 만들 때 정해지고 바뀌지 않는 값) 로 바인딩되므로, 트랙 이름을 바꿔도 묶음이 풀리지 않습니다. 트랙 머리의 메뉴로 Mute · Delete.

### 클립 값

| 값 | 뜻 |
|---|---|
| Start · Duration | 시간 축 위 자리 (초) |
| Clip In | 원본 클립 안에서 시작할 시간 |
| Speed | 재생 속도 |
| Ease In · Ease Out | 앞 · 뒤에서 무게 0 → 1 (Animation) |
| Loop | 클립보다 길면 되풀이 (Animation) |

### Signal 마커

| 값 | 뜻 |
|---|---|
| Time | 부를 시간 |
| Method | 메서드 이름 (`void OnCut()`, `void OnCut(string s)` …) |
| String · Float · Int | 인자 (메서드가 받는 형식 하나만 쓴다) |
| Retroactive | Play 를 그 뒤 시간에서 시작해도 부른다 (Unity 와 같은 이름) |

## Playable Director

| 값 (Inspector) | 뜻 |
|---|---|
| Playable | Timeline 에셋 (`.playable`) |
| Play On Awake | Play 가 시작될 때 재생 |
| Wrap Mode | 끝에서: **Hold** (마지막 장면에 머문다, 재생 중 그대로) · **Loop** · **None** (멈추고 되돌린다) |
| Initial Time | 재생 시작 시간 |
| Bindings | 트랙 → GameObject |

편집 중 재생 헤드를 옮기면 Animation · Activation · Cinemachine 트랙이 그 시간으로 평가됩니다 (소리 · 신호는 Play 에서만). **Stop** 은 켜고 끈 오브젝트를 처음 상태로 되돌리고 Animator 를 놓아줍니다.

## 창 (Window > Timeline)

- 위 도구 막대: 디렉터 이름 · 에셋, ▶ Play · ‖ Pause · ■ Stop, 시간 / 길이
- 눈금자를 클릭 · 끌면 재생 헤드 (편집 중 미리 보기)
- 클립: 가운데를 끌어 옮기고, 양 끝을 끌어 길이 (프레임에 맞춘다), Delete 로 지우기
- 빈 곳 오른쪽 클릭: Add Animation Clip (프로젝트의 모델) · Add Audio Clip · Add Activation Clip · Add Cinemachine Shot · Add Signal Marker
- 오른쪽 칸: 고른 클립 · 마커 값

## C# (Unity 와 같은 이름)

```csharp
using NovaEngine;

public class CutsceneTrigger : MonoBehaviour
{
    public PlayableDirector director;
    public GameObject hero;

    void OnTriggerEnter(Collider other)
    {
        director.SetGenericBinding("Hero", hero);   // 트랙 id · 이름 · 번호
        director.extrapolationMode = DirectorWrapMode.Hold;
        director.Play();
    }

    void Update()
    {
        if (director.state == PlayState.Playing && director.time >= director.duration)
            director.Stop();
    }
}

// Signal 트랙이 묶은 오브젝트
public class CutsceneEvents : MonoBehaviour
{
    void OnCutSignal(string shot) { Debug.Log("shot " + shot); }
}
```

`PlayableDirector`: `Play` · `Pause` · `Resume` · `Stop` · `Evaluate`, `time` · `duration` · `state` · `extrapolationMode` · `initialTime` · `playOnAwake`, `SetGenericBinding` · `GetGenericBinding`, `playableAssetPath` (NOVA — Unity 의 `playableAsset` 자리).

## CLI (`nova timeline`)

```
nova timeline create --path Assets/Cut.playable
nova timeline add-track --path Assets/Cut.playable --type Animation --name Hero
nova timeline add-clip --path Assets/Cut.playable --track Hero --start 0 --duration 1.2 --asset Assets/Hero.fbx --clip 2 --ease-out 0.2
nova timeline add-marker --path Assets/Cut.playable --track Cues --time 1.5 --method OnCutSignal --string go
nova timeline director --director Director --path Assets/Cut.playable --wrap Hold
nova timeline bind --director Director --track Hero --target Hero
nova timeline time --director Director --value 0.6
nova timeline info --director Director
```

`add-track --type`: `Animation | Activation | Audio | Signal | Cinemachine`. `add-clip --object GO` 는 Cinemachine Shot 의 카메라. `play | pause | resume | stop --director GO`. `nova timeline help` 가 모든 연산을 보여 줍니다.

## 구조

- `Source/Animation/AnimationOverrides` (코어): 바깥에서 Animator 자세를 덮는 클립 목록. Timeline 이 프레임마다 넣고, Animator 패키지가 자세를 계산할 때 읽는다 — 두 패키지 DLL 이 서로를 몰라도 된다. 클립 무게 합만큼 컨트롤러 자세 위에 덮이고 (1 이면 컨트롤러 무시), 컨트롤러가 없는 Animator 도 덮는 클립이 있으면 움직인다. 편집 중에는 바뀔 때마다 (Revision) 자세를 다시 계산한다
- `Packages/com.nova.timeline/Source/TimelineAsset` (`.playable` JSON) · `PlayableDirector` (평가 · 바인딩 · 되돌리기) · `TimelineWindow` · `Package.cpp` (CLI · 에셋 형식 · C# 내보내기)
- 검사: `Tools/tests/run_tests.ps1 -Only timeline` (트랙 · 바인딩, 편집 중 미리 보기, Activation, Stop 되돌림, Play On Awake + Signal, Hold)

## 아직 없는 것

- Control Track (하위 Timeline · 파티클), Animation Track 의 녹화 (Record) · 무한 클립 · 오프셋, 트랙 그룹, Audio 의 볼륨 곡선 · 섞기
- Unity `.playable` (YAML) 가져오기
