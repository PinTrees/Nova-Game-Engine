using System.Collections;
using NovaEngine;

// Wheel Collider 검사 (Tools/tests/run_tests.ps1 -Only wheel): C# 로 차 (1500 kg, 바퀴 4 개) 를 만들어
//  쉬기 (서스펜션 = Target Position, 매달린 질량 · 하중) → 뒷바퀴 모터 → 브레이크 → 앞바퀴 조향
public class WheelProbe : MonoBehaviour
{
    static string F(float v) => v.ToString("F2");

    public static GameObject Car;
    WheelCollider m_FL, m_FR, m_RL, m_RR;
    Rigidbody m_Rb;
    readonly System.Collections.Generic.List<(WheelCollider wheel, Transform visual)> m_Visuals = new System.Collections.Generic.List<(WheelCollider, Transform)>();

    WheelCollider Wheel(string name, Vector3 local)
    {
        var w = new GameObject(name);
        w.transform.SetParent(Car.transform, false);
        w.transform.localPosition = local;
        var wc = w.AddComponent<WheelCollider>();
        wc.radius = 0.4f;
        wc.suspensionDistance = 0.3f;
        wc.suspensionSpring = new JointSpring { spring = 35000f, damper = 4500f, targetPosition = 0.5f };
        // 그림 바퀴 (Unity 처럼 GetWorldPose 로 맞춘다) — 콜라이더 없이, 차 밖에
        var vis = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        vis.name = name + "Visual";
        Destroy(vis.GetComponent<Collider>());
        vis.GetComponent<Renderer>().material.color = new Color(0.12f, 0.12f, 0.14f, 1f);
        vis.transform.localScale = new Vector3(0.8f, 0.15f, 0.8f);
        m_Visuals.Add((wc, vis.transform));
        return wc;
    }

    void LateUpdate()
    {
        foreach (var (wheel, visual) in m_Visuals)
        {
            wheel.GetWorldPose(out var p, out var q);
            visual.position = p;
            visual.rotation = q * Quaternion.Euler(0, 0, 90);   // 원기둥 (Y 축) → 바퀴 축 (X)
        }
        if (Car != null)
        {
            var cam = GameObject.Find("Main Camera");
            if (cam != null)
            {
                // 차 뒤 왼쪽 위에서 (조향한 앞바퀴가 보이게)
                cam.transform.position = Car.transform.TransformPoint(new Vector3(-3.5f, 2.6f, -4.5f));
                cam.transform.LookAt(Car.transform.TransformPoint(new Vector3(0, 0.3f, 1.2f)));
            }
        }
    }

    IEnumerator Start()
    {
        Car = new GameObject("Car");
        Car.transform.position = new Vector3(0, 1.0f, 0);
        m_Rb = Car.AddComponent<Rigidbody>();
        m_Rb.mass = 1500f;
        var body = GameObject.CreatePrimitive(PrimitiveType.Cube);
        body.name = "CarBody";
        body.transform.SetParent(Car.transform, false);
        body.transform.localPosition = new Vector3(0, 0.3f, 0);
        body.transform.localScale = new Vector3(1.8f, 0.5f, 4f);
        body.GetComponent<Renderer>().material.color = new Color(0.85f, 0.2f, 0.15f, 1f);
        m_FL = Wheel("FL", new Vector3(-0.9f, 0, 1.4f)); m_FR = Wheel("FR", new Vector3(0.9f, 0, 1.4f));
        m_RL = Wheel("RL", new Vector3(-0.9f, 0, -1.4f)); m_RR = Wheel("RR", new Vector3(0.9f, 0, -1.4f));

        // 1) 쉬기: 차 원점 = 바퀴 반지름 + 쉬는 길이 (0.4 + 0.15) 위
        yield return new WaitForSeconds(2.5f);
        int grounded = (m_FL.isGrounded ? 1 : 0) + (m_FR.isGrounded ? 1 : 0) + (m_RL.isGrounded ? 1 : 0) + (m_RR.isGrounded ? 1 : 0);
        m_FL.GetGroundHit(out var hit);
        m_FL.GetWorldPose(out var wp, out _);
        Debug.Log($"WheelProbe rest carY={F(Car.transform.position.y)} grounded={grounded} sprung={m_FL.sprungMass:F0} load={hit.force:F0} ground={(hit.collider != null ? hit.collider.gameObject.name : "none")} wheelY={F(wp.y)}");

        // 2) 뒷바퀴 모터 400 N·m × 2 → 앞 (+Z) 으로
        m_RL.motorTorque = m_RR.motorTorque = 400f;
        yield return new WaitForSeconds(4f);
        var v = m_Rb.velocity;
        Debug.Log($"WheelProbe drive speed={F(v.z)} side={F(Mathf.Abs(Car.transform.position.x))} z={F(Car.transform.position.z)} rpm={m_RL.rpm:F0} expectRpm={v.z / 0.4f * 60f / (2f * Mathf.PI):F0}");

        // 3) 브레이크
        m_RL.motorTorque = m_RR.motorTorque = 0f;
        m_FL.brakeTorque = m_FR.brakeTorque = m_RL.brakeTorque = m_RR.brakeTorque = 3000f;
        yield return new WaitForSeconds(3f);
        var bv = m_Rb.velocity;
        m_FL.GetGroundHit(out var bh);
        Debug.Log($"WheelProbe brake speed={F(bv.magnitude)} rpm={m_RL.rpm:F0} v={F(bv.x)},{F(bv.y)},{F(bv.z)} av={F(m_Rb.angularVelocity.magnitude)} fwdSlip={F(bh.forwardSlip)} sideSlip={F(bh.sidewaysSlip)} load={bh.force:F0}");

        // 4) 조향 25 도 + 모터 → 오른쪽으로 돈다
        m_FL.brakeTorque = m_FR.brakeTorque = m_RL.brakeTorque = m_RR.brakeTorque = 0f;
        m_FL.steerAngle = m_FR.steerAngle = 25f;
        m_RL.motorTorque = m_RR.motorTorque = 400f;
        float x0 = Car.transform.position.x;
        yield return new WaitForSeconds(3f);
        float heading = Mathf.DeltaAngle(0, Car.transform.eulerAngles.y);
        m_FL.GetWorldPose(out _, out var wq);
        var axle = wq * Vector3.right;   // 바퀴 축 (회전과 상관없다) → 요 각
        float wheelYaw = Mathf.Atan2(-axle.z, axle.x) * Mathf.Rad2Deg;
        Debug.Log($"WheelProbe steer heading={F(heading)} dx={F(Car.transform.position.x - x0)} wheelYaw={F(Mathf.DeltaAngle(heading, wheelYaw))}");
        Debug.Log("WheelProbe done");
    }
}
