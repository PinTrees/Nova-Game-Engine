# NOVA CLI

터미널이나 AI 에이전트가 **실행 중인 NOVA 에디터를 명령으로 다루는** 도구입니다 (Unity CLI 와 같은 역할).
에디터 창을 앞으로 가져오거나 마우스로 조작하지 않아도 됩니다. 포커스를 잃어 멈춰 있던 에디터도 요청이 오면 깨어나 처리합니다.

## 설치

NOVA Hub > **설치** 탭 > **NOVA CLI** > **설치**.

- `nova.exe` 를 `%LOCALAPPDATA%\NOVA\CLI` 에 복사하고 그 폴더를 사용자 PATH 에 더합니다 → **새로 연** 터미널에서 `nova` 로 실행.
- 엔진을 다시 빌드해 새 버전이 생기면 카드에 "업데이트" 가 뜹니다. **제거** 는 파일과 PATH 항목을 지웁니다.
- 자동화: `NovaEngine.exe --install-cli` / `--uninstall-cli` (결과는 `Binaries\hub_log.txt`).
- 개발 중에는 `Binaries\nova.exe` 를 바로 써도 됩니다.

## 동작 방식

- 에디터는 시작할 때 이름 있는 파이프 `\\.\pipe\nova-editor-<pid>` 를 열고, 파이프 이름과 임의 토큰을
  `%LOCALAPPDATA%\NOVA\Instances\<pid>.json` 에 적습니다 (이 PC, 같은 사용자만). 네트워크 포트는 쓰지 않습니다.
- `nova` 는 그 파일로 에디터를 찾습니다: `--project <폴더|이름>`, `--pid <pid>`, 환경 변수 `NOVA_PROJECT`,
  지금 폴더가 들어 있는 프로젝트, 하나뿐인 에디터 순.
- 요청·응답은 한 줄 JSON. 명령은 에디터의 메인 스레드에서 실행되고, 바꾸는 명령은 **Undo 한 단계**("CLI ...")로 남습니다.
- 종료 코드: 0 성공, 1 명령 실패(이유는 stderr), 2 에디터를 찾지 못함, 3 사용법 오류. `--json` 이면 결과를 JSON 그대로 출력.

## 명령

| 분류 | 명령 |
|---|---|
| 에디터 | `status`, `open <프로젝트> [--background]`, `quit [--force]`, `info`, `log [-n 40] [--grep 글자] [--errors] [--follow]` |
| 오브젝트 | `hierarchy [--components] [--depth N] [--root <대상>]`, `find [이름] [--component 종류]`, `get <대상> [--component 종류]` |
| 수정 | `set <대상> [--name] [--active] [--tag] [--layer] [--static] [--position x,y,z] [--rotation x,y,z] [--scale x,y,z] [--world-position x,y,z] [Component.field=value ...]` |
| 만들기 | `create <종류> [--name] [--parent] [--position] [--rotation] [--scale] [--preset N]`, `delete <대상>`, `add-component <대상> <종류> [--values '{...}']`, `remove-component <대상> <종류>`, `parent <대상> <새 부모> \| --root`, `select <대상> \| --none` |
| 씬·Play | `scene open <Assets/...scene> [--force]`, `scene save`, `play`, `stop`, `pause [on\|off]`, `step`, `undo`, `redo` |
| 보기 | `camera [--position x,y,z --target x,y,z \| --frame <대상> [--distance d]]`, `screenshot <파일.png> [--view scene\|game]` |
| 기타 | `assets [폴더] [--pattern 글자]`, `build <출력 폴더> [--run]`, `build-status [--wait]`, `call <명령> [json]`, `ai-guide`, `help`, `nova help --editor`(에디터가 아는 명령) |

**대상** = 이름, `부모/자식` 경로, 또는 `#id` (`hierarchy` 가 보여 주는 fileID). 같은 이름이 여럿이면 오류와 함께 후보 id 를 알려 줍니다.

**만들 수 있는 종류**: empty, cube, sphere, capsule, cylinder, plane, quad, directional-light, point-light, spot-light, camera,
terrain, tree, rock, rock-scatter, ocean, lake, river, particle-system, audio-source, volume.

## 예

```bash
nova open D:\NovaProjects\MyGame --background
nova hierarchy --components
nova create cube --name Box --position 0,1,0 --scale 2,1,2
nova set Box MeshRenderer.castShadows=1 --rotation 0,30,0
nova add-component Box RigidBody --values "{\"mass\": 5}"
nova camera --frame Box --distance 6
nova screenshot box.png
nova play
nova get Box --json
nova stop
nova log --errors
nova scene save
```

## AI 에이전트에게

`nova ai-guide` 가 에이전트용 사용 안내(작업 순서·규칙)를 출력합니다. Hub 카드의 **AI 안내 복사** 는 에이전트에게 붙여 넣을 한 줄을 복사합니다.
