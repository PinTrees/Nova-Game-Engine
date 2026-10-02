namespace NovaEngine.AI
{
    /// <summary>
    /// Nav Mesh Agent 의 수평 속도(m/s)를 Animator 의 Float 파라미터로 넘긴다.
    /// 컨트롤러에 Speed 1D Blend Tree(예: 0 Idle, 1.5 Walk, 5 Run)를 두면 NPC 가 움직이는 만큼 걷고 뛴다.
    /// Animator 는 같은 오브젝트나 자식에 있으면 된다.
    /// </summary>
    public class NavMeshAgentAnimator : MonoBehaviour
    {
        public string speedParameter = "Speed";
        /// <summary>값이 따라가는 시간(초) — 멈추고 출발할 때 동작이 튀지 않게</summary>
        public float damping = 0.1f;

        NavMeshAgent m_Agent;
        Animator m_Animator;
        float m_Speed;

        void Start()
        {
            m_Agent = GetComponent<NavMeshAgent>();
            m_Animator = GetComponent<Animator>() ?? GetComponentInChildren<Animator>();
        }

        void Update()
        {
            if (m_Agent == null || m_Animator == null)
                return;
            Vector3 v = m_Agent.velocity;
            float target = new Vector3(v.x, 0, v.z).magnitude;
            m_Speed = damping > 0 ? Mathf.Lerp(m_Speed, target, 1 - Mathf.Exp(-Time.deltaTime / damping)) : target;
            m_Animator.SetFloat(speedParameter, m_Speed);
        }
    }
}
