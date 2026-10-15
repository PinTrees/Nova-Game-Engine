# ScriptableObject (데이터 에셋)

Unity **ScriptableObject** 와 같은 이름 · 동작의 데이터 에셋입니다. 씬과 따로 저장하는 값 묶음 (무기 · 아이템 · 적 스탯 · 웨이브 · 설정) 을 C# 클래스로 정의하고, Project 창에서 `.asset` 파일로 만들어 Inspector 에서 고치고, MonoBehaviour 필드가 그 에셋을 가리킵니다.

## 만들기

1. Project 창 Create > Scripting > **ScriptableObject Script** (Unity 6 템플릿) — 또는 직접:

```csharp
using System;
using System.Collections.Generic;
using NovaEngine;

public enum Rarity { Common, Rare, Epic }

[Serializable]
public struct Drop { public string item; public int count; [Range(0, 1)] public float chance; }

[CreateAssetMenu(fileName = "NewWeapon", menuName = "Game/Weapon Data", order = 1)]
public class WeaponData : ScriptableObject
{
    public string displayName = "Sword";
    [Range(0, 100)] public int damage = 10;
    public float cooldown = 0.5f;
    public Rarity rarity;
    public int[] combo = { 1, 2, 3 };
    public List<Drop> drops = new List<Drop>();
    public WeaponData upgrade;            // 다른 에셋
    public AudioClip swingSound;
}
```

2. 컴파일되면 Project 창 Create > **Game > Weapon Data** → `NewWeapon.asset` (이름 바꾸기 바로 시작)
3. 에셋을 고르면 Inspector 가 필드를 그린다 — 바꾸면 바로 저장
4. 쓰는 쪽:

```csharp
public class Player : MonoBehaviour
{
    public WeaponData weapon;             // Inspector 에 Project 의 .asset 을 끌어 놓거나 ⊙ 로 고른다
    public WeaponData[] arsenal;

    void Attack()
    {
        Debug.Log(weapon.displayName + " " + weapon.damage);
    }
}
```

## 규칙 (Unity 와 같음)

- **같은 에셋 = 같은 인스턴스**: 여러 오브젝트가 같은 `.asset` 을 가리키면 C# 에서 한 오브젝트 (`ReferenceEquals` 참). 서로 가리키는 에셋 (A ↔ B) 도 된다
- 메시지: `Awake` · `OnEnable` (처음 읽을 때 · `CreateInstance`), `OnDisable` (Play 끝), `OnDestroy` (`Destroy`), `OnValidate` (Play 중 Inspector 에서 바꿀 때)
- 직렬화 필드: `public` 또는 `[SerializeField]` (`[NonSerialized]` · `[HideInInspector]` 빼고), `[Header]` · `[Tooltip]` · `[Space]` · `[Range]`
- 필드 형식: `int` · `float` · `bool` · `string` · `Vector2/3/4` · `Color` · enum · `GameObject` · `Transform` · `AudioClip` · **ScriptableObject (하위 클래스 포함)** · **배열 `T[]` · `List<T>`** · **`[Serializable]` 클래스 · 구조체** (안에 또 배열 · 구조체, 깊이 7 까지). 배열의 배열은 Unity 처럼 안 된다
- `ScriptableObject.CreateInstance<T>()` — 파일 없는 런타임 인스턴스, `Object.Instantiate(asset)` — 값을 복사한 새 인스턴스 (이름 `(Clone)`), 원본은 그대로
- 추상 클래스는 필드 형식 · 고르기 창의 바탕으로만 (에셋은 하위 클래스로)
- **Play 마다 파일에서 새로**: Play 중 스크립트가 바꾼 값은 Play 가 끝나면 사라진다. Inspector · CLI 로 고친 값은 파일에 저장되고, Play 중이면 읽어 둔 인스턴스에도 바로 들어간다 (OnValidate). — Unity 편집기는 Play 중 스크립트가 바꾼 값까지 에셋에 남기지만, NOVA 는 남기지 않는다 (빌드된 게임과 같은 동작)

## Resources.Load

프로젝트 아무 곳의 **Resources** 폴더 안 파일을 그 폴더 기준 경로 (확장자 없이) 로 읽습니다. 빌드는 Resources 폴더를 통째로 넣습니다 (씬이 가리키지 않아도).

```csharp
// Assets/Data/Resources/Items/Elixir.asset
var elixir = Resources.Load<PotionData>("Items/Elixir");
ItemData[] all = Resources.LoadAll<ItemData>("Items");        // 그 폴더 아래 전부
TextAsset table = Resources.Load<TextAsset>("Tables/loot");    // .txt .json .csv .bytes …
string csv = table.text;
```

| 파일 | 형식 |
|---|---|
| `.asset` | ScriptableObject (그 클래스 또는 하위 클래스만) |
| `.txt` `.json` `.csv` `.bytes` `.xml` `.yaml` `.md` | `TextAsset` (`text` · `bytes`) |
| `.mat` | `Material` |
| `.wav` `.ogg` `.mp3` | `AudioClip` |

형식이 맞지 않으면 `null`. 같은 에셋은 같은 인스턴스 (`Resources.UnloadAsset` 으로 놓으면 다음 Load 가 파일에서 새로).

## 파일 형식

```json
{
  "type": "ScriptableObject",
  "class": "WeaponData",
  "fields": {
    "displayName": "Sword",
    "damage": 42,
    "rarity": 2,
    "combo": [1, 2, 3],
    "drops": [ { "item": "Gem", "count": 2, "chance": 0.5 } ],
    "upgrade": "Assets/Data/Great.asset",
    "swingSound": "Assets/Audio/swing.wav"
  }
}
```

에셋 참조는 경로 글자 — 씬 · 에셋 JSON 의 경로를 빌드가 따라가 같이 넣는다. 클래스에서 사라진 필드 값도 파일에 남는다 (이름을 되돌리면 값이 돌아온다).

## CLI (`nova scriptable`)

```
nova scriptable classes                                          # 클래스 · CreateAssetMenu · 필드 형식 (배열 원소 · 구조체 안까지)
nova scriptable create --class WeaponData --path Assets/Data     # 폴더면 fileName 으로, 파일 이름도 가능
nova scriptable create --class WeaponData --path Assets/Data/Sword.asset --values "{\"damage\":42}"
nova scriptable get --path Assets/Data/Sword.asset              # 기본값까지 채운 전체 값
nova scriptable set --path Assets/Data/Sword.asset --values "{\"damage\":50}"   # Play 중이면 바로 반영
nova scriptable list [--class ItemData]                          # 그 클래스 (하위 포함) 에셋
nova scriptable resources --path Items [--all]                   # Resources.Load 가 찾는 파일
```

MonoBehaviour 필드에 에셋 넣기: `nova set Player --component Player --values "{\"weapon\":\"Assets/Data/Sword.asset\"}"`.

## 구조

- C#: `ScriptCore/Engine/ScriptableObject.cs` (`ScriptableObject` · `CreateAssetMenuAttribute` · `TextAsset` · `Resources`, 읽기 · 캐시 `ScriptableAssets`), `ScriptCore/Interop/BridgeFields.cs` (필드 직렬화 — MonoBehaviour 와 같이 쓴다: 배열 · List · Serializable · 에셋 참조)
- C++: `Source/Scripting/ScriptableAssets` (.asset 읽기 · 쓰기, Resources 목록, `.asset` Inspector, C# 용 `NovaAsset_*`), `ScriptFieldGUI` (필드 Inspector — 컴포넌트 · 에셋 공용), `ScriptEngine::ScriptableClasses` · `NotifyAssetChanged`, Project 창 Create 메뉴, `BuildPipeline` 이 Resources 폴더를 넣는다
- 검사: `Tools/tests/run_tests.ps1 -Only scriptable`

## 아직 없는 것

- 다른 ScriptableObject 를 안에 품는 에셋 (서브 에셋 · `AssetDatabase.AddObjectToAsset`), 편집기 스크립트 (`AssetDatabase.CreateAsset` · 사용자 Inspector)
- Unity `.asset` (YAML) 가져오기, `Resources.LoadAsync`, Addressables
