# 씬 관리 · PlayerPrefs

Unity 의 `SceneManager` 와 같은 C# API 로 Play 중에 씬을 바꾸거나 **더해 읽고** (Additive), **작업 스레드에서 읽고** (LoadSceneAsync), 내린다 (UnloadSceneAsync).
`DontDestroyOnLoad` 오브젝트는 씬을 바꿔도 남는다. 작은 값은 `PlayerPrefs` 로 저장한다 (게임을 껐다 켜도 남는다).

## 씬 읽기

```csharp
using System.Collections;
using NovaEngine;
using NovaEngine.SceneManagement;

public class Loader : MonoBehaviour
{
    IEnumerator Start()
    {
        DontDestroyOnLoad(gameObject);                       // 이 오브젝트는 씬을 바꿔도 남는다

        // 로딩 화면: 읽기는 작업 스레드에서, 0.9 에서 기다렸다가 바꾼다
        AsyncOperation op = SceneManager.LoadSceneAsync("Level2", LoadSceneMode.Additive);
        op.allowSceneActivation = false;
        while (op.progress < 0.9f) yield return null;        // 0 ~ 0.9 = 읽는 중
        op.allowSceneActivation = true;
        yield return op;                                     // 끝날 때까지 (op.isDone)

        Scene level = SceneManager.GetSceneByName("Level2");
        SceneManager.SetActiveScene(level);                  // 새로 만드는 오브젝트는 이 씬으로
        yield return SceneManager.UnloadSceneAsync("Level1");
    }
}
```

| API | 내용 |
|---|---|
| `LoadScene(이름 \| 빌드 번호, mode)` | 이번 프레임 끝에. `Single` = 지금 씬들을 내리고 (DontDestroyOnLoad 는 남김), `Additive` = 더하기 |
| `LoadSceneAsync(…)` → `AsyncOperation` | 파일 읽기 · 해석은 작업 스레드, 오브젝트 만들기 · Awake 는 프레임 끝 (메인 스레드). `progress` · `isDone` · `allowSceneActivation` · `completed` · `yield return` |
| `UnloadSceneAsync(Scene \| 이름 \| 번호)` | 그 씬의 오브젝트만 지운다 (OnDisable · OnDestroy). 마지막으로 남은 씬은 내릴 수 없다 (Unity 와 같음) |
| `sceneCount` · `GetSceneAt` · `GetSceneByName` · `GetSceneByPath` · `GetSceneByBuildIndex` | 읽혀 있는 씬 (읽은 차례) |
| `GetActiveScene` · `SetActiveScene` | 활성 씬 = `new GameObject` · `Instantiate` 가 들어가는 씬 |
| `MoveGameObjectToScene(go, scene)` · `Object.DontDestroyOnLoad(go)` | 루트 오브젝트만 (자식이면 경고) |
| `sceneLoaded` · `sceneUnloaded` · `activeSceneChanged` | 알림. `sceneLoaded` 는 그 씬의 Awake 뒤, Start 전 |
| `Scene` | `name` · `path` · `buildIndex` · `handle` · `isLoaded` · `rootCount` · `GetRootGameObjects()`, `GameObject.scene` |

- 씬은 Build Settings 의 Scenes In Build 에 있어야 한다 (`nova build-scenes set --scenes Assets/Scenes/A.scene,Assets/Scenes/B.scene`)
- 같은 씬을 두 번 더해도 된다 (씬마다 핸들 · 오브젝트 ID 가 따로)
- Play 를 멈추면 Play 를 시작한 씬으로 돌아간다. 지난 Play 의 `sceneLoaded` 구독 · 작업은 다음 Play 에 남지 않는다

### 구조

엔진이 그리고 · 물리 · 스크립트를 돌리는 씬 객체는 하나다. 더해 읽은 씬은 오브젝트를 그 안으로 옮기고, 루트 오브젝트마다 어느 씬 것인지 (핸들) 를
기억한다 (`Source/Scene/SceneManagerRuntime.cpp`). 그래서 여러 씬의 오브젝트가 한 번에 그려지고 서로 부딪힌다.
더해 읽을 때 오브젝트 ID (fileID) 를 새로 만들고 씬 안의 참조도 함께 바꾼다 (같은 씬을 두 번 더해도 C# 이 오브젝트를 구별하게).
웹 (스레드 없음) 은 `LoadSceneAsync` 의 읽기도 그 프레임에 한다.

## PlayerPrefs

```csharp
int best = PlayerPrefs.GetInt("best", 0);
PlayerPrefs.SetInt("best", Mathf.Max(best, score));
PlayerPrefs.SetString("name", "플레이어");
PlayerPrefs.Save();                                          // 안 불러도 프레임 끝에 저장된다
```

`SetInt` · `GetInt` · `SetFloat` · `GetFloat` · `SetString` · `GetString` · `HasKey` · `DeleteKey` · `DeleteAll` · `Save` — 종류가 다른 키는 없는 것으로 (실수로 저장한 키를 `GetInt` 로 읽으면 기본값, Unity 와 같음).

| 어디서 | 저장 자리 |
|---|---|
| 에디터 | `<프로젝트>/Library/PlayerPrefs.json` (Play 를 멈춰도 남고, 빌드한 게임과 따로) |
| Windows 게임 | `Application.persistentDataPath/PlayerPrefs.json` = `%USERPROFILE%\AppData\LocalLow\<회사>\<제품>` |
| 안드로이드 | 앱 파일 폴더 |
| 웹 | 브라우저 `localStorage` (새로 고쳐도 남는다) |

`Application.persistentDataPath` · `dataPath` · `companyName` · `version` · `temporaryCachePath` 도 있다.
`Application.targetFrameRate` (Unity 와 같다): 0 이하 = 제한 없음 (기본 -1). 재생 중 (빌드된 게임 포함) 에 엔진이 프레임 사이를 기다려 그 속도에 맞추고, 에디터에서 Play 를 멈추면 -1 로 돌아간다.

## 검사

`Tools/tests/run_tests.ps1 -Only scenes` — 검사 스크립트 (`Tools/tests/scene_probe.cs`) 로 비동기 0.9 대기 · 더하기 · 같은 씬 두 번 · 내리기 · 활성 씬 · DontDestroyOnLoad · 알림 · PlayerPrefs (다음 Play 에도 남음).
