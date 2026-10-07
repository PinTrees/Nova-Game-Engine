using NovaEngine;

// activeInHierarchy 검사 (Tools/tests/run_tests.ps1 -Only behaviour): 부모를 끄면 자식의 Update 가 멈추고 OnDisable, 다시 켜면 OnEnable.
//  꺼진 오브젝트는 GameObject.Find 로 못 찾으므로 (Unity) 부모 · 소리를 정적 칸에 기억한다
public class ActiveProbe : MonoBehaviour
{
    public static int updates, enables, disables;
    public static GameObject parent;
    public static AudioSource source;

    void Awake()
    {
        parent = transform.parent != null ? transform.parent.gameObject : null;
        source = GetComponent<AudioSource>();
    }

    void OnEnable() { enables++; }
    void OnDisable() { disables++; }
    void Update() { updates++; }

    public static string State() => updates + " " + enables + " " + disables + " " + (source != null && source.isPlaying);
}
