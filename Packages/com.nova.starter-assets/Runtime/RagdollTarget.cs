using NovaEngine;

namespace StarterAssets
{
    // 맞으면 래그돌로 쓰러지는 캐릭터 (Starter Assets — GameObject > Starter Assets > Ragdoll Target, CLI `nova create ragdoll-target`).
    // 캐릭터에 Ragdoll (꺼짐 = 애니메이션을 따라 서 있음) 이 있어야 한다. TakeHit 이 Ragdoll 을 켜고 (Animator 는 자동으로 꺼진다)
    // 맞은 자리에 가장 가까운 바디를 그 방향으로 민다. Character Controller · ThirdPersonController 도 끈다 (쓰러진 몸을 끌고 다니지 않게).
    // Recover = 일어나기: 등을 대고 누웠으면 GetUpBack, 엎드렸으면 GetUpFront 상태 (기본 캐릭터 컨트롤러) 를 재생하고 Ragdoll 이
    // 쓰러진 자세에서 그 클립으로 섞는다 (Blend Time). getUpTime 뒤에 Character Controller · 조작을 다시 켠다.
    public class RagdollTarget : MonoBehaviour
    {
        [Tooltip("몇 번 맞으면 쓰러지나")] public int hitsToFall = 1;
        [Tooltip("쓰러진 뒤 다시 서기까지 (초, 0 = 그대로)")] public float recoverAfter = 0f;

        [Header("Get Up")]
        [Tooltip("등을 대고 누웠을 때 재생할 Animator 상태 (비우면 섞기만)")] public string getUpBackState = "GetUpBack";
        [Tooltip("엎드렸을 때 재생할 Animator 상태")] public string getUpFrontState = "GetUpFront";
        [Tooltip("일어나는 데 걸리는 시간 (초) — 끝나면 Character Controller · 조작을 다시 켠다")] public float getUpTime = 2.3f;

        [Header("State (read only)")]
        public int hits;
        public bool down;
        public string lastBody = "";
        public bool gettingUp;
        public string lastGetUp = "";

        Ragdoll ragdoll;
        Animator animator;
        float downTime, getUpStart;
        // 켜자마자 바디는 다음 물리 스텝에 다이내믹이 된다 → 충격은 그때 준다
        Rigidbody pendingBody;
        Vector3 pendingImpulse, pendingPoint;
        int pendingSteps;

        void Start()
        {
            ragdoll = GetComponent<Ragdoll>();
            animator = GetComponent<Animator>();
            if (animator == null) animator = GetComponentInChildren<Animator>();
            if (ragdoll == null) Debug.LogWarning($"RagdollTarget on {name} needs a Ragdoll (GameObject > 3D Object > Ragdoll...)");
        }

        /// <summary>맞음: point = 맞은 자리 (월드), impulse = 미는 힘 (N·s, 방향 포함)</summary>
        public void TakeHit(Vector3 point, Vector3 impulse)
        {
            hits++;
            var rb = NearestBody(point);
            lastBody = rb != null ? rb.gameObject.name : "";
            if (!down && hits >= hitsToFall)
                Fall();
            if (rb != null)
            {
                pendingBody = rb;
                pendingImpulse = impulse;
                pendingPoint = point;
                pendingSteps = 2;
            }
        }

        public void Fall()
        {
            if (ragdoll == null) return;
            down = true;
            gettingUp = false;
            downTime = Time.time;
            var cc = GetComponent<CharacterController>();
            if (cc != null) cc.enabled = false;
            var tpc = GetComponent<ThirdPersonController>();
            if (tpc != null) tpc.enabled = false;
            ragdoll.active = true;
        }

        /// <summary>일어난다: 누운 방향에 맞는 일어나기 클립 + 쓰러진 자세에서 섞기 (바디는 다시 애니메이션을 따라간다)</summary>
        public void Recover()
        {
            if (ragdoll == null) return;
            bool faceUp = ragdoll.isFaceUp;
            ragdoll.active = false;   // 이번 프레임 끝에 루트를 골반 자리 · 일어서는 방향으로 옮기고 섞기 시작
            down = false;
            hits = 0;
            lastGetUp = faceUp ? getUpBackState : getUpFrontState;
            if (animator != null && !string.IsNullOrEmpty(lastGetUp))
                animator.Play(lastGetUp, 0, 0f);
            gettingUp = true;
            getUpStart = Time.time;
            if (getUpTime <= 0f) StandUp();
        }

        void StandUp()
        {
            gettingUp = false;
            var cc = GetComponent<CharacterController>();
            if (cc != null) cc.enabled = true;
            var tpc = GetComponent<ThirdPersonController>();
            if (tpc != null) tpc.enabled = true;
        }

        Rigidbody NearestBody(Vector3 point)
        {
            Rigidbody best = null;
            float bestD = float.MaxValue;
            foreach (var rb in GetComponentsInChildren<Rigidbody>())
            {
                float d = (rb.worldCenterOfMass - point).sqrMagnitude;
                if (d < bestD) { bestD = d; best = rb; }
            }
            return best;
        }

        void FixedUpdate()
        {
            if (pendingBody != null && --pendingSteps <= 0)
            {
                pendingBody.AddForceAtPosition(pendingImpulse, pendingPoint, ForceMode.Impulse);
                pendingBody = null;
            }
            if (down && recoverAfter > 0f && Time.time - downTime > recoverAfter)
                Recover();
            if (gettingUp && Time.time - getUpStart >= getUpTime)
                StandUp();
        }
    }
}
