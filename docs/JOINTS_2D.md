# Joint 2D

NOVA의 Box2D 2D 물리에 Hinge, Spring, Distance, Wheel, Fixed, Slider 조인트를 추가했다. Add Component > Physics 2D에서 붙일 수 있으며, 같은 오브젝트에 Rigidbody2D가 없으면 함께 붙는다. Inspector에 연결 몸체와 앵커, 각 조인트의 설정이 표시되고 선택한 오브젝트의 Scene 뷰에는 앵커와 연결선이 그려진다.

## 조인트와 설정

| 컴포넌트 | 동작 | 주요 C# 설정 |
|---|---|---|
| `HingeJoint2D` | 한 점을 중심으로 회전 | `useMotor`, `motor`, `useLimits`, `limits`, `jointAngle`, `jointSpeed` |
| `SpringJoint2D` | 설정한 길이로 돌아오는 스프링 | `autoConfigureDistance`, `distance`, `frequency`, `dampingRatio` |
| `DistanceJoint2D` | 두 앵커 사이의 거리 유지 | `autoConfigureDistance`, `distance`, `maxDistanceOnly` |
| `WheelJoint2D` | 서스펜션 축 이동과 바퀴 회전 | `suspension`, `useMotor`, `motor`, `jointSpeed` |
| `FixedJoint2D` | 상대 위치와 회전을 유지 | `frequency`, `dampingRatio` |
| `SliderJoint2D` | 지정 축을 따라 이동 | `autoConfigureAngle`, `angle`, `useMotor`, `motor`, `useLimits`, `limits`, `jointTranslation`, `jointSpeed` |

`Joint2D`의 공통 설정은 `connectedBody`, `enableCollision`, `breakForce`, `breakTorque`, `enabled`다. `AnchoredJoint2D`에는 `anchor`, `connectedAnchor`, `autoConfigureConnectedAnchor`가 있으며 여섯 컴포넌트가 모두 상속한다. 읽기 전용으로 `attachedRigidbody`, `reactionForce`, `reactionTorque`를 제공한다.

- `connectedBody == null`이면 월드에 연결한다. 연결 몸체가 있으면 두 앵커는 각 오브젝트의 로컬 좌표이고, 월드 연결의 `connectedAnchor`는 월드 좌표다. 자기 자신 또는 사용할 수 없는 몸체와의 연결은 생성하지 않는다.
- 자동 앵커는 생성 또는 연결 설정 변경 시 현재 앵커 위치에 맞춘다. 자동 거리는 당시 두 앵커 간 거리다. Spring/Distance의 자동 연결 앵커 기본값은 꺼짐이다.
- Hinge/Wheel 모터 속도는 도/초, Slider 모터 속도는 거리/초다. `JointMotor2D.maxMotorTorque`를 Slider에서는 최대 힘으로 사용한다.
- 각도는 도 단위다. Wheel의 `suspension.angle`과 Slider의 `angle`은 설정 시 월드 축이며, 생성된 축은 연결 몸체와 함께 회전한다.
- `frequency == 0`은 Spring/Fixed의 단단한 연결과 Wheel의 고정 서스펜션이다. `maxDistanceOnly`는 지정 길이보다 가까워질 수 있는 연결이다. Box2D의 최소 안정 거리는 0.005다.
- 반작용은 마지막 물리 단계의 힘과 토크다. `GetReactionForce/Torque(timeStep)`와 모터 힘/토크 조회의 인수는 현재 구현에서 별도 재계산에 사용하지 않는다.
- Hinge의 네이티브 제한 범위는 약 ±178.2°다. 제한은 물리 솔버의 허용 오차가 있으므로 정확한 수치 고정과 다르다. ±30° 모터 검사에서 최대 32.177°였으며 검사 허용 오차는 3°다.

## C# 사용

```csharp
using NovaEngine;

public class MotorArm : MonoBehaviour
{
    void Start()
    {
        var joint = gameObject.AddComponent<HingeJoint2D>();
        // 자동으로 붙은 Rigidbody2D에 바로 접근할 수 있다.
        joint.attachedRigidbody.gravityScale = 0;
        joint.useMotor = true;
        joint.motor = new JointMotor2D { motorSpeed = 90, maxMotorTorque = 100 };
        joint.useLimits = true;
        joint.limits = new JointAngleLimits2D { min = -30, max = 30 };
        joint.breakForce = 1000;
    }

    void OnJointBreak2D(Joint2D joint)
    {
        Debug.Log(joint.gameObject.name + " force=" + joint.reactionForce.magnitude);
    }
}
```

반작용이 `breakForce` 또는 `breakTorque`를 넘으면 네이티브 연결을 끊고 `OnJointBreak2D(Joint2D)`를 한 번 보낸다. 콜백 안에서는 해당 조인트의 종류와 반작용을 읽을 수 있고, 콜백 후 컴포넌트를 제거한다. 임계값의 기본값은 Infinity다. 같은 오브젝트의 여러 조인트는 각각 조회·설정·제거할 수 있다.

## 저장과 통합

설정은 씬 JSON, Undo/Redo, Play/Stop, 복제와 프리팹에 포함한다. `connectedBody`는 GameObject의 fileID이고 복제 및 프리팹 인스턴스 내부 연결은 새 ID로 변환한다. 네이티브 Box2D 핸들과 반작용 값은 저장하지 않는다. JSON에서 무한대 임계값은 `"Infinity"`로 저장한다. Rigidbody2D를 재생성하면 연결도 다시 만든다.

C# 네이티브 표 끝에 아래 8개를 네이티브와 관리 코드의 같은 순서로 추가했다. 다른 기능은 이 뒤에 추가하고 엔진과 `NovaScriptCore.dll`을 함께 빌드한다.

`J2_GetFloat` → `J2_SetFloat` → `J2_GetVec` → `J2_SetVec` → `J2_GetConnected` → `J2_SetConnected` → `J2_Find` → `J2_Remove`

## 검증 기록

2026년 10월 3일, 기준 커밋 `673c3b3`에 Codex의 담당 변경만 반영한 독립 Debug 엔진에서 다음 검사를 통과했다. Claude의 진행 중인 Decal 변경을 합친 실행 결과는 아니다.

| 검사 | 결과 | 결과 파일 |
|---|---|---|
| Joint 2D | 42/42 | `TestResults/CodexJoints2D/joints-results.json` |
| 기존 Physics 2D | 8/8 | `TestResults/CodexJoints2D/physics-regression2/results.json` |
| 씬 동작 | 44/44 | `TestResults/CodexJoints2D/scene-results.json` |

Joint 검사는 모터·제한, 흔들리는 힌지, 스프링 진동과 무진동 감쇠, 함께 떨어지는 두 몸체의 거리, 느슨한 거리 연결, 바퀴 두 개의 차량 주행, Fixed/Slider, 힘·토크 끊어짐 콜백, 저장·복원·Undo·복제·프리팹 및 빌드한 게임 실행을 포함한다. Inspector의 설정 경로와 컴포넌트 순서는 검사했고 버튼 클릭과 앵커 표시의 시각 검사는 아직 수행하지 않았다.

전용 검사는 `Tools/tests/joints2d.ps1 -EngineRoot <빌드한 엔진 루트> -Out <새 폴더>`로 실행한다. 검사 스크립트는 엔진을 복사하고 새 프로젝트와 자신이 시작한 프로세스만 사용한다. 기존 공용 검사 실행기는 설치된 엔진을 먼저 찾을 수 있으므로 독립 빌드의 Physics 2D 회귀 검사에는 `NOVA_ENGINE` 환경 변수로 해당 실행 파일을 명시한다.

관련 구현: `Source/Physics2D/Physics2DJoints.h/.cpp`, `ScriptCore/Engine/Physics2D.cs`. 공동 작업 범위와 통합 인계는 [공동 명세](AI_COLLABORATION.md) 및 [Codex 상태](ai-status/CODEX.md)를 확인한다.
