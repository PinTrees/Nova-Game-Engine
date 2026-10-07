using NovaEngine;

// activeInHierarchy 검사 (Tools/tests/run_tests.ps1 -Only behaviour): 부모를 끄면 자식의 Update 가 멈추고 OnDisable, 다시 켜면 OnEnable.
//  같은 부모 아래 소리 · 파티클 · 캐릭터 (Animator) 도 — 꺼지면 소리 멈춤 · 입자 지움, 다시 켜면 재생 · Animator 는 처음 상태부터.
//  꺼진 오브젝트는 GameObject.Find 로 못 찾으므로 (Unity) 정적 칸에 기억한다
public class ActiveProbe : MonoBehaviour
{
    public static int updates, enables, disables;
    public static GameObject parent;
    public static AudioSource source;
    public static ParticleSystem particles;
    public static Animator animator;

    void Awake()
    {
        parent = transform.parent != null ? transform.parent.gameObject : null;
        source = GetComponent<AudioSource>();
        if (parent != null)
        {
            particles = parent.GetComponentInChildren<ParticleSystem>();
            animator = parent.GetComponentInChildren<Animator>();
        }
    }

    void OnEnable() { enables++; }
    void OnDisable() { disables++; }
    void Update() { updates++; }

    public static string State() => updates + " " + enables + " " + disables + " " + (source != null && source.isPlaying);
    // 파티클 수 · 재생 중, Animator 상태 시각 (정규화)
    public static string Extra() => (particles != null ? particles.particleCount : -1) + " " + (particles != null && particles.isPlaying) + " " +
        (animator != null ? animator.GetCurrentAnimatorStateInfo(0).normalizedTime.ToString("F3", System.Globalization.CultureInfo.InvariantCulture) : "-1");
}
