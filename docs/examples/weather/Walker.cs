using NovaEngine;

// 데모: 캐릭터가 원을 따라 걷는다 (눈 위에 발자국) — Animator 에 Speed · MotionSpeed · Grounded (Starter Assets 와 같은 값)
public class Walker : MonoBehaviour
{
    public Vector3 center = new Vector3(0f, 0f, 0f);
    public float radius = 6f;
    public float speed = 1.4f;        // m/s
    public float startAngle = 0f;     // 도

    Animator anim;
    float angle;

    void Start()
    {
        anim = GetComponent<Animator>();
        angle = startAngle * Mathf.Deg2Rad;
    }

    void Update()
    {
        angle += speed / Mathf.Max(radius, 0.5f) * Time.deltaTime;
        Vector3 p = center + new Vector3(Mathf.Sin(angle) * radius, 0f, Mathf.Cos(angle) * radius);
        Vector3 dir = new Vector3(Mathf.Cos(angle), 0f, -Mathf.Sin(angle));   // 원의 접선 (도는 쪽)
        transform.position = new Vector3(p.x, transform.position.y, p.z);
        transform.rotation = Quaternion.LookRotation(dir);
        if (anim != null)
        {
            anim.SetFloat("Speed", speed);
            anim.SetFloat("MotionSpeed", 1f);
            anim.SetBool("Grounded", true);
        }
    }
}
