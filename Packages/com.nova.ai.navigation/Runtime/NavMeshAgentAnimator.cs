namespace NovaEngine.AI
{
    /// <summary>
    /// Nav Mesh Agent 의 수평 속도(m/s)를 Animator 의 Float 파라미터로 넘긴다.
    /// 컨트롤러에 Speed 1D Blend Tree(예: 0 Idle, 1.5 Walk, 5 Run)를 두면 NPC 가 움직이는 만큼 걷고 뛴다.
    /// Animator 는 같은 오브젝트나 자식에 있으면 된다. (Animation 패키지가 없어도 컴파일되도록 이름으로 찾는다)
    /// </summary>
    public class NavMeshAgentAnimator : MonoBehaviour
    {
        public string speedParameter = "Speed";
        /// <summary>값이 따라가는 시간(초) — 멈추고 출발할 때 동작이 튀지 않게</summary>
        public float damping = 0.1f;

        NavMeshAgent m_Agent;
        Component m_Animator;
        System.Reflection.MethodInfo m_SetFloat;
        float m_Speed;

        void Start()
        {
            m_Agent = GetComponent<NavMeshAgent>();
            var type = System.Type.GetType("NovaEngine.Animator") ?? FindType("NovaEngine.Animator");
            if (type == null)
                return;   // Animation 패키지가 없다
            m_Animator = Find(transform, type);
            m_SetFloat = type.GetMethod("SetFloat", new[] { typeof(string), typeof(float) });
        }

        // 자기 → 자식 순으로
        static Component Find(Transform t, System.Type type)
        {
            var c = t.gameObject.GetComponent(type);
            if (c != null) return c;
            for (int i = 0; i < t.childCount; i++)
                if (Find(t.GetChild(i), type) is Component found) return found;
            return null;
        }

        static System.Type FindType(string name)
        {
            foreach (var a in System.AppDomain.CurrentDomain.GetAssemblies())
                if (a.GetType(name) is System.Type t) return t;
            return null;
        }

        void Update()
        {
            if (m_Agent == null || m_Animator == null)
                return;
            Vector3 v = m_Agent.velocity;
            float target = new Vector3(v.x, 0, v.z).magnitude;
            m_Speed = damping > 0 ? Mathf.Lerp(m_Speed, target, 1 - Mathf.Exp(-Time.deltaTime / damping)) : target;
            m_SetFloat?.Invoke(m_Animator, new object[] { speedParameter, m_Speed });
        }
    }
}
