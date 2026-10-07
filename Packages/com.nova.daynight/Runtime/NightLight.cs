namespace NovaEngine
{
    // 밤에 저절로 켜지는 빛 (가로등 · 창문 · 간판): 장면의 Day Night Cycle 의 해 높이로 켜고 끈다.
    //  이 오브젝트와 자식의 Light 를 모두 (켜질 때의 세기까지 서서히), Emissive 렌더러 (비우면 이 오브젝트의 렌더러) 는 발광 색을 함께.
    //  해가 Turn On Below 아래로 지면 켜고 Turn Off Above 위로 뜨면 끈다 (사이에서는 그대로 — 깜빡이지 않게).
    //  가로등이 한꺼번에가 아니라 하나씩 켜지게 Random Delay 만큼 늦춘다. Day Night Cycle 이 없으면 늘 켜 둔다.
    public class NightLight : MonoBehaviour
    {
        [Tooltip("해가 이 높이 (도) 아래로 지면 켠다")] public float turnOnBelow = -2f;
        [Tooltip("해가 이 높이 (도) 위로 뜨면 끈다")] public float turnOffAbove = 2f;
        [Tooltip("켜지고 꺼지는 데 걸리는 시간 (초)")] public float fadeSeconds = 1.5f;
        [Tooltip("켜고 끄는 때를 0 ~ 이 시간 (초) 늦춘다 — 하나씩 켜진다")] public float randomDelay = 1.5f;

        [Header("Emission (전등갓 · 창문)")]
        [Tooltip("밤에 빛날 렌더러 (비우면 이 오브젝트의 렌더러 — 전구 · 창문에 붙이면 그대로)")] public Renderer emissive;
        [Tooltip("켜졌을 때 발광 색 (HDR — 1 넘는 값 = 더 밝게)")] public Color emissionColor = new Color(3.0f, 2.2f, 1.2f, 1f);

        [Header("State (read only)")]
        public bool isOn;
        [Range(0f, 1f)] public float level;

        Light[] lights;
        float[] fullIntensity;
        Material emissiveMaterial;
        float delay, pending = -1f;
        bool target, started;

        void Start()
        {
            lights = GetComponentsInChildren<Light>();
            fullIntensity = new float[lights.Length];
            for (int i = 0; i < lights.Length; i++) fullIntensity[i] = lights[i].intensity;
            if (emissive == null) emissive = GetComponent<Renderer>();
            if (emissive != null) emissiveMaterial = emissive.material;   // 이 렌더러만의 사본
            delay = Random.Range(0f, Mathf.Max(0f, randomDelay));
            // 처음은 기다리지 않고 지금 상태로
            target = WantOn(true);
            level = target ? 1f : 0f;
            isOn = target;
            Apply();
            started = true;
        }

        bool WantOn(bool fresh)
        {
            if (!DayNight.exists) return true;
            float e = DayNight.sunElevation;
            if (e < turnOnBelow) return true;
            if (e > turnOffAbove) return false;
            return fresh ? e < 0.5f * (turnOnBelow + turnOffAbove) : target;
        }

        void Update()
        {
            if (!started) return;
            bool want = WantOn(false);
            if (want != target)
            {
                // 바뀐 뒤 delay 초 기다렸다가
                if (pending < 0f) pending = delay;
                pending -= Time.deltaTime;
                if (pending <= 0f) { target = want; pending = -1f; }
            }
            else
                pending = -1f;
            float step = fadeSeconds > 0f ? Time.deltaTime / fadeSeconds : 1f;
            float next = Mathf.MoveTowards(level, target ? 1f : 0f, step);
            if (next != level)
            {
                level = next;
                Apply();
            }
            isOn = level > 0.5f;
        }

        void Apply()
        {
            for (int i = 0; i < lights.Length; i++)
            {
                if (lights[i] == null) continue;
                lights[i].intensity = fullIntensity[i] * level;
                lights[i].enabled = level > 0.001f;
            }
            if (emissiveMaterial != null)
                emissiveMaterial.SetColor("_EmissionColor", emissionColor * level);
        }
    }
}
