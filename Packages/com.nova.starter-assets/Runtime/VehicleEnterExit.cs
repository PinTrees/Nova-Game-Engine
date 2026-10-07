using NovaEngine;

namespace StarterAssets
{
    // 3인칭 캐릭터가 차에 타고 내린다 (Starter Assets). 플레이어 (ThirdPersonController) 에 붙인다.
    // E = 가까운 차 (enterDistance 안의 CarController) 에 탄다: 캐릭터를 숨기고 Character Controller · 조작을 끄고, 차가 키보드를 읽고,
    // Follow Camera 를 차 뒤로 (차를 따라 돈다). 다시 E = 운전석 쪽 (차의 왼쪽) 땅에 내려 원래 카메라로.
    // 장면의 차는 플레이어가 탈 때까지 키보드를 읽지 않고 손 브레이크로 서 있다. 키보드 대신 (UI · 시험) 은 readKeyboard 를 끄고 Enter · Exit 를 부른다.
    public class VehicleEnterExit : MonoBehaviour
    {
        [Tooltip("이 거리 (m) 안의 차에 탄다")] public float enterDistance = 3.5f;
        [Tooltip("내리는 자리: 차 중심에서 왼쪽으로 (m)")] public float exitSideOffset = 1.8f;
        public KeyCode key = KeyCode.E;
        public bool readKeyboard = true;

        [Header("Camera while driving (Follow Camera)")]
        public float carDistance = 7f;
        public float carHeight = 2.6f;
        public float carLookAtHeight = 0.9f;

        [Header("State (read only)")]
        public CarController car;   // 탄 차 (null = 걷는 중)
        public bool driving;

        CharacterController controller;
        ThirdPersonController thirdPerson;
        StarterAssetsInputs inputs;
        FollowCamera followCamera;
        SkinnedMeshRenderer[] skinned;
        MeshRenderer[] meshes;
        float camDistance, camHeight, camLookAt;

        void Start()
        {
            controller = GetComponent<CharacterController>();
            thirdPerson = GetComponent<ThirdPersonController>();
            inputs = GetComponent<StarterAssetsInputs>();
            if (Camera.main != null) followCamera = Camera.main.GetComponent<FollowCamera>();
            // 걷는 동안 WASD 로 차가 같이 움직이지 않게
            foreach (var c in FindObjectsOfType<CarController>())
                Park(c);
        }

        void Update()
        {
            if (!readKeyboard || !Input.GetKeyDown(key)) return;
            if (car != null) Exit();
            else Enter(NearestCar());
        }

        void LateUpdate()
        {
            // 숨은 캐릭터는 운전석에 따라간다 (내릴 때 · 다른 스크립트가 위치를 읽을 때)
            if (car != null)
                transform.SetPositionAndRotation(car.transform.position, Quaternion.Euler(0f, car.transform.eulerAngles.y, 0f));
        }

        /// <summary>enterDistance 안에서 가장 가까운 차 (없으면 null)</summary>
        public CarController NearestCar()
        {
            CarController best = null;
            float bestD = enterDistance * enterDistance;
            foreach (var c in FindObjectsOfType<CarController>())
            {
                Vector3 d = c.transform.position - transform.position;
                d.y = 0f;
                if (d.sqrMagnitude <= bestD) { bestD = d.sqrMagnitude; best = c; }
            }
            return best;
        }

        public bool Enter(CarController c)
        {
            if (c == null || car != null) return false;
            car = c;
            driving = true;
            if (controller != null) controller.enabled = false;
            if (thirdPerson != null) thirdPerson.enabled = false;
            if (inputs != null) { inputs.move = Vector2.zero; inputs.jump = false; }
            SetVisible(false);
            car.readKeyboard = readKeyboard;
            car.handbrake = false;
            if (followCamera != null)
            {
                camDistance = followCamera.distance;
                camHeight = followCamera.height;
                camLookAt = followCamera.lookAtHeight;
                followCamera.target = car.gameObject;
                followCamera.distance = carDistance;
                followCamera.height = carHeight;
                followCamera.lookAtHeight = carLookAtHeight;
                followCamera.followTargetRotation = true;
            }
            return true;
        }

        public bool Exit()
        {
            if (car == null) return false;
            // 운전석 쪽 (왼쪽) 땅 위에 내린다
            Vector3 p = car.transform.position - car.transform.right * exitSideOffset;
            if (Physics.Raycast(p + Vector3.up * 2f, Vector3.down, out RaycastHit hit, 10f))
                p.y = hit.point.y;
            float yaw = car.transform.eulerAngles.y;
            transform.SetPositionAndRotation(p, Quaternion.Euler(0f, yaw, 0f));
            Park(car);
            car = null;
            driving = false;
            SetVisible(true);
            if (controller != null) controller.enabled = true;
            if (thirdPerson != null) thirdPerson.enabled = true;
            if (inputs != null) { inputs.move = Vector2.zero; inputs.jump = false; }
            if (followCamera != null)
            {
                followCamera.target = gameObject;
                followCamera.distance = camDistance;
                followCamera.height = camHeight;
                followCamera.lookAtHeight = camLookAt;
                followCamera.followTargetRotation = false;   // 걷기는 카메라 기준 (ThirdPersonController 와 같이)
                followCamera.yaw = yaw;
            }
            return true;
        }

        static void Park(CarController c)
        {
            c.readKeyboard = false;
            c.throttle = 0f;
            c.steer = 0f;
            c.handbrake = true;
        }

        void SetVisible(bool visible)
        {
            if (skinned == null) skinned = GetComponentsInChildren<SkinnedMeshRenderer>();
            if (meshes == null) meshes = GetComponentsInChildren<MeshRenderer>();
            foreach (var r in skinned) r.enabled = visible;
            foreach (var r in meshes) r.enabled = visible;
        }
    }
}
