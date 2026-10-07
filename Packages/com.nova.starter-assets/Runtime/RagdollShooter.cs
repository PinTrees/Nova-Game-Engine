using NovaEngine;

namespace StarterAssets
{
    // 클릭한 곳을 쏜다 (Starter Assets — Ragdoll Target 을 만들면 Main Camera 에 붙는다).
    // 왼쪽 버튼 = 화면의 그 자리로 광선 → RagdollTarget 이면 쓰러뜨리고, 그 밖의 Rigidbody 는 맞은 자리를 민다.
    // 키보드 · 마우스 대신 (AI · 시험) 은 readMouse 를 끄고 Shoot(ray) 를 부른다.
    public class RagdollShooter : MonoBehaviour
    {
        [Tooltip("미는 힘 (N·s) — 래그돌 표적은 20 kg")] public float force = 30f;
        public float range = 200f;
        public bool readMouse = true;

        [Header("State (read only)")]
        public int shots;
        public string lastHit = "";

        Camera cam;

        void Start()
        {
            cam = GetComponent<Camera>();
            if (cam == null) cam = Camera.main;
        }

        void Update()
        {
            if (readMouse && cam != null && Input.GetMouseButtonDown(0))
                Shoot(cam.ScreenPointToRay(Input.mousePosition));
        }

        /// <summary>광선을 쏜다 — 맞은 것이 있으면 true</summary>
        public bool Shoot(Ray ray)
        {
            shots++;
            if (!Physics.Raycast(ray, out var hit, range))
            {
                lastHit = "";
                return false;
            }
            lastHit = hit.collider != null ? hit.collider.gameObject.name : "";
            var target = hit.collider != null ? hit.collider.GetComponentInParent<RagdollTarget>() : null;
            if (target != null)
            {
                target.TakeHit(hit.point, ray.direction * force);
                return true;
            }
            var rb = hit.rigidbody;
            if (rb != null && !rb.isKinematic)
                rb.AddForceAtPosition(ray.direction * force, hit.point, ForceMode.Impulse);
            return true;
        }

        /// <summary>카메라에서 그 월드 자리로 쏜다</summary>
        public bool ShootAt(Vector3 worldPoint)
        {
            var from = cam != null ? cam.transform.position : transform.position;
            return Shoot(new Ray(from, worldPoint - from));
        }
    }
}
