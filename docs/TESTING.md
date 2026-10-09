# 자동 검사 (Tools/tests)

`Tools/tests/run_tests.ps1` 이 창 없는 검사 에디터 (`nova open --background --hidden`) 를 띄우고, 모든 조작을 `nova` CLI 로 한다.
마우스 · 키보드 입력은 흉내 내지 않는다 (같은 PC 를 사람이 쓰고 있다).

```bash
powershell -File Tools/tests/run_tests.ps1 -Only physics,physicssync
```

- `-Only` 를 빼면 전체 회귀 (릴리즈 전). 평소에는 바꾼 기능의 스위트만 돌린다.
- 결과는 `TestResults/<시각>/results.json` 과 스위트별 그림 · 스크립트.

## 시간에 기대지 않기

2026-10-09 (사용자 요청: "검사 안정성 · 시간 의존 검사 정리 — 고정 대기를 조건이 될 때까지 폴링으로"). 전체 회귀에서 다시 돌리면 통과하는 실패가 1 ~ 2 개씩 나왔다.

| 실패 | 원인 | 고침 |
|------|------|------|
| model: VRM 다시 가져오기 로그가 없음 | 다시 내보낸 뒤 고정 2.5 초. 바쁜 PC 에서 파일 감시 · 다시 가져오기가 늦었다 | 로그가 나올 때까지 (최대 20 초) 기다린 뒤, 씬이 새 메시를 쓸 때까지 |
| daynight: APV 프로브 -1 | 고정 240 프레임. 볼륨이 아직 지어지는 중이면 프로브를 읽지 못한다 (-1). 폴링으로 바꾼 뒤에도 전체 회귀 (66 분) 후반에 한 번 정오 · 자정 모두 -1 — 그림은 같았다 (열린 땅이라 APV 가 꺼져도 비슷하다). APV 는 GPU 자원을 한 번 못 만들면 그 세션 내내 꺼진 채였다 | 프로브가 읽히고 30 프레임 사이 값이 2 % 안으로 멈출 때까지. 엔진: 자원 실패 뒤 놓고 2 · 4 · 6 … 초 뒤 다시 (5 번까지), 실패한 HRESULT 를 Editor.log 에. 검사가 실패하면 `probevolume info` 를 결과에 남긴다 (원인은 다음에 나면 확인) |
| ssao: `Not enough memory resources …` (스위트 전체가 멈춤) · 이어서 motionvectors 빈 값 · DEVICE_HUNG | `nova.exe` 를 띄우지 못한 일시 오류로 스위트가 예외로 멈추고, 그 검사 에디터가 남아 다음 스위트까지 망쳤다 | 아래 두 가지 |

공용 도구 (`common.ps1`):

- `Wait-Until { 조건 } -Seconds 20 [-MinSeconds 1] [-Frames 5]` — 조건이 참이 될 때까지 에디터 프레임을 조금씩 돌리며 다시 본다. 빠르면 바로 끝난다.
- `Invoke-Nova` — `nova.exe` 를 띄우지 못하면 1 초 뒤 두 번 더 한다.
- `Stop-StrayTestEditors` — 명령줄에 검사 프로젝트와 `--hidden` 이 함께 있는 에디터만 끈다 (사용자의 에디터는 건드리지 않는다).

스위트가 예외로 멈추면 (검사 실패가 아니라 환경): 남은 검사 에디터를 끄고, 그 스위트의 이번 결과를 지운 뒤 한 번 다시 돌린다.
다시 통과하면 `suite ran (again after an exception on the first try)` 에 첫 예외와 그 줄 번호가 남는다. 다시 해도 멈추면 `suite ran` 실패.
이 경로는 `NOVA_TEST_THROW_ONCE=<스위트>` 로 일부러 한 번 예외를 내어 확인한다. APV 다시 짓기는 `NOVA_DEV_APV_FAIL=N` (처음 N 번 실패) 으로 확인한다.

2026-10-09 전체 회귀: 709 개 중 708 통과 (66 분), 실패는 위 APV 하나 — 고친 뒤 probevolume · renderingdebug · daynight 통과.

`Wait-Sec` 처럼 시간을 쓰는 곳은 대부분 게임 시간 자체를 재는 검사다 (공이 떨어지는 시간, 애니메이션 길이). 이런 곳은 그대로 둔다.
새 검사는 '무엇이 될 때까지' 를 먼저 생각하고, 고정 대기는 그 일이 정말 시간일 때만 쓴다.
