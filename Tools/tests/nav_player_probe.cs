using NovaEngine;
using NovaEngine.AI;

// 플레이어 (안드로이드 · 웹) 내비게이션 검사: 구운 NavMesh (3D 바닥 + 2D XY) 를 읽어 길을 찾고 에이전트가 벽을 돌아 도착하는가.
//  로그: "NavProbe start …" (길 꺾임 수), "NavProbe done …" (도착 위치 · 걸린 시간)
public class NavPlayerProbe : MonoBehaviour
{
    NavMeshAgent m_A3, m_A2;
    float m_T0;
    bool m_Done;
    static string F(float v) => v.ToString("F2");

    void Start()
    {
        m_A3 = GameObject.Find("Npc3D").GetComponent<NavMeshAgent>();
        m_A2 = GameObject.Find("Npc2D").GetComponent<NavMeshAgent>();
        var p3 = new NavMeshPath();
        bool ok3 = NavMesh.CalculatePath(new Vector3(-5, 0, 0), new Vector3(5, 0, 0), NavMesh.AllAreas, p3);
        var p2 = new NavMeshPath();
        bool ok2 = NavMesh.CalculatePath(new Vector3(95, 100, 0), new Vector3(105, 100, 0), NavMesh.AllAreas, p2);
        float maxZ = 0, maxY = 0;
        foreach (var c in p3.corners) maxZ = Mathf.Max(maxZ, Mathf.Abs(c.z));
        foreach (var c in p2.corners) maxY = Mathf.Max(maxY, Mathf.Abs(c.y - 100f));
        bool d3 = m_A3 != null && m_A3.SetDestination(new Vector3(5, 0, 0));
        bool d2 = m_A2 != null && m_A2.SetDestination(new Vector3(105, 100, 0));
        m_T0 = Time.time;
        Debug.Log($"NavProbe start platform={Application.platform} path3d={ok3}:{p3.corners.Length}:{F(maxZ)} path2d={ok2}:{p2.corners.Length}:{F(maxY)} dest={d3},{d2}");
    }

    void Update()
    {
        if (m_Done || m_A3 == null || m_A2 == null)
            return;
        var a = m_A3.transform.position;
        var b = m_A2.transform.position;
        bool arrived3 = Mathf.Abs(a.x - 5) < 0.2f && Mathf.Abs(a.z) < 0.2f;
        bool arrived2 = Mathf.Abs(b.x - 105) < 0.2f && Mathf.Abs(b.y - 100) < 0.2f;
        if ((arrived3 && arrived2) || Time.time - m_T0 > 12f)
        {
            m_Done = true;
            Debug.Log($"NavProbe done arrived={arrived3},{arrived2} p3={F(a.x)},{F(a.z)} p2={F(b.x)},{F(b.y)},{F(b.z)} t={F(Time.time - m_T0)}");
        }
    }
}
