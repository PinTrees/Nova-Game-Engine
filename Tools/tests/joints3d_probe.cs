using System.Collections;
using NovaEngine;

// 3D 관절 검사 (Tools/tests/run_tests.ps1 -Only ragdoll): Character Joint (흔들기 1 · 2, 비틀기, 끊어짐) · Configurable Joint (선 한계 · 드라이브 · 회전 드라이브) ·
// 겹친 Rigidbody (부모 · 자식 모두 다이내믹)
public class Joints3DProbe : MonoBehaviour
{
    static string F(float v) => v.ToString("F1");

    // 위에 매단 막대 (길이 1, 1 kg): 꼭대기가 관절
    static Rigidbody Rod(Vector3 top, out GameObject go)
    {
        go = GameObject.CreatePrimitive(PrimitiveType.Cube);
        go.transform.position = top - new Vector3(0, 0.5f, 0);
        go.transform.localScale = new Vector3(0.2f, 1f, 0.2f);
        var rb = go.AddComponent<Rigidbody>();
        rb.mass = 1f;
        rb.drag = 2f;            // 흔들림이 가라앉아 평형 각에서 멈춘다 (평형은 감쇠와 상관없다)
        rb.angularDrag = 2f;
        return rb;
    }

    static CharacterJoint Hang(GameObject go, float swing1, float swing2, float lowTwist, float highTwist)
    {
        var j = go.AddComponent<CharacterJoint>();
        j.anchor = new Vector3(0, 0.5f, 0);
        j.axis = new Vector3(0, -1, 0);          // 비틀기 = 막대 방향
        j.swingAxis = new Vector3(1, 0, 0);      // 흔들기 1 = X 둘레, 흔들기 2 = (아래 × X) = Z 둘레
        j.swing1Limit = new SoftJointLimit { limit = swing1 };
        j.swing2Limit = new SoftJointLimit { limit = swing2 };
        j.lowTwistLimit = new SoftJointLimit { limit = lowTwist };
        j.highTwistLimit = new SoftJointLimit { limit = highTwist };
        return j;
    }

    // 막대가 아래에서 기운 각 (도)
    static float Tilt(GameObject go) => Vector3.Angle(go.transform.up, Vector3.up);

    Rigidbody m_Swing1, m_Swing2, m_Free, m_Twist;
    int m_Breaks;
    public static int Breaks;

    IEnumerator Start()
    {
        // 1) 흔들기: 옆으로 mg·tan60 (자유면 60 도에서 멈춘다) → 흔들기 1 (30) · 흔들기 2 (15) 에서 멈춘다
        m_Swing1 = Rod(new Vector3(0, 6, 0), out var s1); Hang(s1, 30, 30, -10, 10);
        m_Swing2 = Rod(new Vector3(2, 6, 0), out var s2); Hang(s2, 30, 15, -10, 10);
        m_Free = Rod(new Vector3(4, 6, 0), out var fr); Hang(fr, 90, 90, -10, 10);
        // 2) 비틀기: 막대 축으로 비틀면 -10 ~ 10 에서 멈춘다
        m_Twist = Rod(new Vector3(6, 6, 0), out var tw); Hang(tw, 30, 30, -10, 10);
        // 3) 끊어짐: 1 kg 의 무게 (9.8 N) > Break Force 5
        var br = Rod(new Vector3(8, 6, 0), out var brk);
        Hang(brk, 30, 30, -10, 10).breakForce = 5f;
        brk.AddComponent<BreakCounter>();

        // 4) Configurable: Y 만 Limited 0.5 (나머지 Locked) → 0.5 떨어져 멈춘다
        var lin = GameObject.CreatePrimitive(PrimitiveType.Cube);
        lin.transform.position = new Vector3(10, 6, 0);
        lin.AddComponent<Rigidbody>();
        var cl = lin.AddComponent<ConfigurableJoint>();
        cl.xMotion = ConfigurableJointMotion.Locked; cl.zMotion = ConfigurableJointMotion.Locked; cl.yMotion = ConfigurableJointMotion.Limited;
        cl.angularXMotion = cl.angularYMotion = cl.angularZMotion = ConfigurableJointMotion.Locked;
        cl.linearLimit = new SoftJointLimit { limit = 0.5f };

        // 5) Configurable X 드라이브: targetPosition (1, 0, 0) → Unity 와 같이 -1 쪽으로
        var drv = GameObject.CreatePrimitive(PrimitiveType.Cube);
        drv.transform.position = new Vector3(12, 6, 0);
        var drb = drv.AddComponent<Rigidbody>();
        drb.useGravity = false;
        var cd = drv.AddComponent<ConfigurableJoint>();
        cd.yMotion = cd.zMotion = ConfigurableJointMotion.Locked;
        cd.angularXMotion = cd.angularYMotion = cd.angularZMotion = ConfigurableJointMotion.Locked;
        cd.xDrive = new JointDrive { positionSpring = 200f, positionDamper = 30f, maximumForce = float.MaxValue };
        cd.targetPosition = new Vector3(1, 0, 0);

        // 6) Configurable 회전 드라이브 (Slerp): targetRotation = X 30 도 → 반대 (-30)
        var rot = GameObject.CreatePrimitive(PrimitiveType.Cube);
        rot.transform.position = new Vector3(14, 6, 0);
        var rrb = rot.AddComponent<Rigidbody>();
        rrb.useGravity = false;
        var cr = rot.AddComponent<ConfigurableJoint>();
        cr.xMotion = cr.yMotion = cr.zMotion = ConfigurableJointMotion.Locked;
        cr.angularYMotion = cr.angularZMotion = ConfigurableJointMotion.Locked;
        cr.rotationDriveMode = RotationDriveMode.Slerp;
        cr.slerpDrive = new JointDrive { positionSpring = 50f, positionDamper = 5f, maximumForce = float.MaxValue };
        cr.targetRotation = Quaternion.Euler(30, 0, 0);

        // 7) 겹친 다이내믹 바디: 부모 (자유 낙하) 아래 자식이 Fixed Joint 로 붙어 같이 떨어진다 → 간격 그대로
        var parent = GameObject.CreatePrimitive(PrimitiveType.Cube);
        parent.transform.position = new Vector3(16, 8, 0);
        parent.AddComponent<Rigidbody>();
        var child = GameObject.CreatePrimitive(PrimitiveType.Cube);
        child.transform.SetParent(parent.transform, false);
        child.transform.localPosition = new Vector3(0, -1.5f, 0);
        child.AddComponent<Rigidbody>().mass = 0.5f;
        child.AddComponent<FixedJoint>().connectedBody = parent.GetComponent<Rigidbody>();

        yield return new WaitForSeconds(0.6f);
        float gapFalling = Vector3.Distance(parent.transform.position, child.transform.position);
        float parentY = parent.transform.position.y;
        yield return new WaitForSeconds(2.4f);

        float twist = Vector3.SignedAngle(Vector3.forward, tw.transform.forward, Vector3.up);
        Debug.Log($"Joints3DProbe swing swing1={F(Tilt(s1))} swing2={F(Tilt(s2))} free={F(Tilt(fr))}");
        Debug.Log($"Joints3DProbe twist={F(Mathf.Abs(twist))} break={Breaks} jointGone={brk.GetComponent<CharacterJoint>() == null}");
        Debug.Log($"Joints3DProbe configurable linearY={lin.transform.position.y:F2} linearX={lin.transform.position.x:F2} driveX={drv.transform.position.x:F2} rotX={F(Mathf.DeltaAngle(0, rot.transform.eulerAngles.x))}");
        Debug.Log($"Joints3DProbe nested gap={gapFalling:F2} parentFell={parentY < 7.5f}");
        Debug.Log("Joints3DProbe done");
    }

    void FixedUpdate()
    {
        if (m_Swing1 == null) return;
        float side = 1f * 9.81f * 1.732f;   // mg·tan60
        m_Swing1.AddForce(new Vector3(0, 0, side));
        m_Swing2.AddForce(new Vector3(side, 0, 0));
        m_Free.AddForce(new Vector3(0, 0, side));
        m_Twist.AddTorque(new Vector3(0, 3f, 0));
    }
}

public class BreakCounter : MonoBehaviour
{
    void OnJointBreak(float force) => Joints3DProbe.Breaks++;
}
