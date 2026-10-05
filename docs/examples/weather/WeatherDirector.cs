using NovaEngine;

// 날씨 시네마틱 데모 (com.nova.weather): Play 하면 맑음 → 흐림 → 비 → 폭풍 (번개) → 눈 → 눈보라 → 다시 맑음,
//   카메라는 숲 · 집 둘레를 천천히 돈다 (Camera Rig 가 있으면 그것을, 없으면 Main Camera)
//   1 ~ 6 = 프로필 바로 (Clear, Cloudy, Rain, Storm, Snow, Blizzard), L = 번개, Space = 시간표 멈춤 / 다시, C = 카메라 멈춤 / 다시
public class WeatherDirector : MonoBehaviour
{
    public bool autoPlay = true;
    public bool moveCamera = true;
    public Vector3 orbitCenter = new Vector3(0f, 1.5f, 0f);
    public float orbitRadius = 22f;
    public float orbitHeight = 5f;
    public float orbitSpeed = 3f;      // 도 / 초

    // 시간표: (시작 초, 프로필, 넘어가는 초)
    static readonly float[] kAt = { 0f, 8f, 16f, 28f, 46f, 62f, 80f };
    static readonly string[] kProfile = { "Clear", "Cloudy", "Rain", "Storm", "Snow", "Blizzard", "Clear" };
    static readonly float[] kSeconds = { 0f, 6f, 6f, 5f, 8f, 6f, 10f };
    static readonly string[] kKeys = { "Clear", "Cloudy", "Rain", "Storm", "Snow", "Blizzard" };

    float clock;
    int next;
    bool timelineOn;
    float angle = 200f;
    Transform cam;

    void Start()
    {
        timelineOn = autoPlay;
        var rig = GameObject.Find("Camera Rig");
        if (rig == null) rig = GameObject.Find("Main Camera");
        if (rig != null) cam = rig.transform;
    }

    void Update()
    {
        // 손으로
        if (Input.GetKeyDown(KeyCode.Alpha1)) SetNow(0);
        if (Input.GetKeyDown(KeyCode.Alpha2)) SetNow(1);
        if (Input.GetKeyDown(KeyCode.Alpha3)) SetNow(2);
        if (Input.GetKeyDown(KeyCode.Alpha4)) SetNow(3);
        if (Input.GetKeyDown(KeyCode.Alpha5)) SetNow(4);
        if (Input.GetKeyDown(KeyCode.Alpha6)) SetNow(5);
        if (Input.GetKeyDown(KeyCode.L)) Weather.Strike();
        if (Input.GetKeyDown(KeyCode.Space)) timelineOn = !timelineOn;
        if (Input.GetKeyDown(KeyCode.C)) moveCamera = !moveCamera;

        // 시간표
        if (timelineOn)
        {
            clock += Time.deltaTime;
            while (next < kAt.Length && clock >= kAt[next])
            {
                Weather.Set(kProfile[next], kSeconds[next]);
                next++;
            }
            if (next >= kAt.Length && clock > kAt[kAt.Length - 1] + 14f)
            {
                clock = 0f;   // 처음부터 다시
                next = 0;
            }
        }

        // 카메라: 천천히 돌며 높이가 오르내린다
        if (moveCamera && cam != null)
        {
            angle += orbitSpeed * Time.deltaTime;
            float a = angle * Mathf.Deg2Rad;
            float h = orbitHeight + 1.5f * Mathf.Sin(Time.time * 0.11f);
            float r = orbitRadius + 3f * Mathf.Sin(Time.time * 0.07f);
            cam.position = orbitCenter + new Vector3(Mathf.Sin(a) * r, h, Mathf.Cos(a) * r);
            cam.LookAt(orbitCenter + new Vector3(0f, 1.2f, 0f));
        }
    }

    void SetNow(int i)
    {
        timelineOn = false;
        Weather.Set(kKeys[i], 3f);
    }
}
