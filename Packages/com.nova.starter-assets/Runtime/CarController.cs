using NovaEngine;

namespace StarterAssets
{
    // 운전할 수 있는 차 (Starter Assets — GameObject > Starter Assets > Car, CLI `nova create car`).
    // Rigidbody 가 있는 차체 아래 Wheel Collider 4 개: W / ↑ 가속, S / ↓ 브레이크 → 멈추면 후진, A D / ← → 조향, Space 손 브레이크, R 뒤집힌 차 세우기.
    // 바퀴 그림 = Wheel Collider 이름 + " Mesh" 인 자식 (GetWorldPose 로 맞춘다). 키보드 대신 다른 입력 (UI · AI · 시험) 은 readKeyboard 를 끄고 throttle · steer · handbrake 를 넣는다.
    public class CarController : MonoBehaviour
    {
        [Header("Wheels (비우면 자식의 Wheel Collider 를 위치로 — 앞 · 뒤, 왼 · 오른)")]
        public WheelCollider frontLeft;
        public WheelCollider frontRight;
        public WheelCollider rearLeft;
        public WheelCollider rearRight;

        [Header("Drive")]
        [Tooltip("구동 바퀴 하나의 모터 토크 (N·m)")] public float motorTorque = 900f;
        [Tooltip("이 속도 (m/s) 를 넘으면 더 밀지 않는다")] public float maxSpeed = 28f;
        [Tooltip("후진 최고 속도 (m/s)")] public float maxReverseSpeed = 6f;
        [Tooltip("네 바퀴 굴림 (끄면 뒷바퀴)")] public bool allWheelDrive = false;
        [Tooltip("멈춘 상태의 조향각 (도) — 빠를수록 줄어든다")] public float maxSteerAngle = 32f;
        [Tooltip("최고 속도에서의 조향각 비율")] public float highSpeedSteer = 0.35f;
        public float brakeTorque = 3500f;
        [Tooltip("손 브레이크 (뒷바퀴, Space)")] public float handbrakeTorque = 6000f;
        [Tooltip("가속을 놓았을 때 엔진 브레이크")] public float coastBrake = 60f;
        [Tooltip("속도² 에 비례해 차를 누르는 힘 (고속에서 덜 뜬다)")] public float downforce = 3f;

        [Header("Input")]
        public bool readKeyboard = true;
        [Range(-1f, 1f)] public float throttle;
        [Range(-1f, 1f)] public float steer;
        public bool handbrake;

        [Header("State (read only)")]
        public float speed;      // 앞 (+) · 뒤 (-) 속도 (m/s)
        public float speedKmh;
        public int groundedWheels;

        Rigidbody body;
        WheelCollider[] wheels;
        Transform[] meshes;

        void Start()
        {
            body = GetComponent<Rigidbody>();
            if (frontLeft == null || frontRight == null || rearLeft == null || rearRight == null)
                FindWheels();
            wheels = new[] { frontLeft, frontRight, rearLeft, rearRight };
            meshes = new Transform[4];
            for (int i = 0; i < 4; i++)
            {
                if (wheels[i] == null) continue;
                var m = transform.Find(wheels[i].gameObject.name + " Mesh");
                meshes[i] = m;
            }
            if (body == null) Debug.LogWarning($"CarController on {name} needs a Rigidbody");
        }

        // 자식의 Wheel Collider 를 차 기준 위치로 나눈다 (z > 0 = 앞, x < 0 = 왼쪽)
        void FindWheels()
        {
            foreach (var w in GetComponentsInChildren<WheelCollider>())
            {
                var p = transform.InverseTransformPoint(w.transform.position);
                if (p.z >= 0f) { if (p.x < 0f) frontLeft = w; else frontRight = w; }
                else { if (p.x < 0f) rearLeft = w; else rearRight = w; }
            }
        }

        void Update()
        {
            if (readKeyboard)
            {
                throttle = Input.GetAxisRaw("Vertical");
                steer = Input.GetAxisRaw("Horizontal");
                handbrake = Input.GetKey(KeyCode.Space);
                if (Input.GetKeyDown(KeyCode.R)) Flip();
            }
        }

        /// <summary>뒤집힌 차를 바로 세운다 (조금 들어 올리고 진행 방향 그대로)</summary>
        public void Flip()
        {
            var yaw = Quaternion.Euler(0f, transform.eulerAngles.y, 0f);
            transform.SetPositionAndRotation(transform.position + Vector3.up * 1.0f, yaw);
            if (body != null) { body.velocity = Vector3.zero; body.angularVelocity = Vector3.zero; }
        }

        void FixedUpdate()
        {
            if (body == null || wheels == null) return;
            speed = Vector3.Dot(body.velocity, transform.forward);
            speedKmh = speed * 3.6f;
            groundedWheels = 0;
            foreach (var w in wheels) if (w != null && w.isGrounded) groundedWheels++;

            float t = Mathf.Clamp(throttle, -1f, 1f);
            float motor = 0f, brake = 0f;
            if (t > 0.01f)
            {
                if (speed < -0.5f) brake = brakeTorque * t;                 // 뒤로 가는 중에 앞으로 = 먼저 멈춤
                else if (speed < maxSpeed) motor = motorTorque * t;
            }
            else if (t < -0.01f)
            {
                if (speed > 0.5f) brake = brakeTorque * -t;                 // 앞으로 가는 중에 뒤로 = 브레이크
                else if (speed > -maxReverseSpeed) motor = motorTorque * t;  // 멈추면 후진
                else brake = brakeTorque * 0.2f;                            // 후진 최고 속도
            }
            else
                brake = coastBrake;

            // 빠를수록 조향을 줄인다 (고속에서 뒤집히지 않게)
            float k = Mathf.Clamp01(Mathf.Abs(speed) / Mathf.Max(1f, maxSpeed));
            float steerAngle = Mathf.Clamp(steer, -1f, 1f) * maxSteerAngle * Mathf.Lerp(1f, highSpeedSteer, k);

            for (int i = 0; i < 4; i++)
            {
                var w = wheels[i];
                if (w == null) continue;
                bool front = i < 2;
                w.steerAngle = front ? steerAngle : 0f;
                w.motorTorque = (allWheelDrive || !front) ? (allWheelDrive ? motor * 0.5f : motor) : 0f;
                w.brakeTorque = brake + (!front && handbrake ? handbrakeTorque : 0f);
            }
            if (downforce > 0f && groundedWheels > 0)
                body.AddForce(-transform.up * downforce * speed * speed);
        }

        void LateUpdate()
        {
            if (wheels == null) return;
            for (int i = 0; i < 4; i++)
            {
                if (wheels[i] == null || meshes[i] == null) continue;
                wheels[i].GetWorldPose(out var p, out var q);
                meshes[i].SetPositionAndRotation(p, q);
            }
        }
    }
}
