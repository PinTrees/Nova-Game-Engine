using NovaEngine;

namespace StarterAssets
{
    // 3인칭 캐릭터 조작 (Unity Starter Assets 의 ThirdPersonController 와 같은 흐름).
    // Character Controller 가 있는 GameObject 에 붙인다. 카메라가 보는 방향 기준으로 움직이고, 움직이는 쪽으로 부드럽게 돈다.
    public class ThirdPersonController : MonoBehaviour
    {
        [Header("Player")]
        [Tooltip("걷기 속도 (m/s)")] public float moveSpeed = 2.0f;
        [Tooltip("달리기 속도 (m/s, Left Shift)")] public float sprintSpeed = 5.335f;
        [Range(0.0f, 0.3f)] public float rotationSmoothTime = 0.12f;
        public float speedChangeRate = 10.0f;

        [Header("Jump & Gravity")]
        public float jumpHeight = 1.2f;
        public float gravity = -15.0f;
        public float jumpTimeout = 0.50f;
        public float fallTimeout = 0.15f;

        [Header("Camera")]
        [Tooltip("움직임 기준 카메라 (비우면 Main Camera)")] public Transform cameraTransform;
        [Tooltip("오른쪽 버튼을 끌 때 카메라가 도는 빠르기 (도/픽셀)")] public float lookSensitivity = 0.25f;

        [Header("State (read only)")]
        public bool grounded = true;
        public float currentSpeed;

        CharacterController controller;
        StarterAssetsInputs input;
        Animator animator;
        FollowCamera followCamera;
        float animationBlend, targetRotation, rotationVelocity, verticalVelocity;
        float jumpTimeoutDelta, fallTimeoutDelta;
        const float terminalVelocity = 53.0f;

        void Start()
        {
            controller = GetComponent<CharacterController>();
            input = GetComponent<StarterAssetsInputs>();
            if (input == null) input = gameObject.AddComponent<StarterAssetsInputs>();
            animator = GetComponentInChildren<Animator>();
            if (cameraTransform == null && Camera.main != null) cameraTransform = Camera.main.transform;
            // Follow Camera: 카메라 기준으로 움직이므로 카메라가 캐릭터를 따라 돌면 맴돈다 → 방향은 고정하고 마우스로 돌린다 (Cinemachine 처럼)
            if (cameraTransform != null) followCamera = cameraTransform.GetComponent<FollowCamera>();
            if (followCamera != null && followCamera.followTargetRotation)
            {
                followCamera.followTargetRotation = false;
                followCamera.yaw = transform.eulerAngles.y;
            }
            targetRotation = transform.eulerAngles.y;
            jumpTimeoutDelta = jumpTimeout;
            fallTimeoutDelta = fallTimeout;
            if (controller == null) Debug.LogWarning($"ThirdPersonController on {name} needs a Character Controller");
        }

        void Update()
        {
            if (controller == null || input == null) return;
            grounded = controller.isGrounded;
            if (followCamera != null && input.look.x != 0.0f)
                followCamera.yaw += input.look.x * lookSensitivity;
            JumpAndGravity();
            Move();
            if (animator != null)
            {
                animator.SetBool("Grounded", grounded);
            }
        }

        void Move()
        {
            float targetSpeed = input.sprint ? sprintSpeed : moveSpeed;
            if (input.move == Vector2.zero) targetSpeed = 0.0f;
            float inputMagnitude = Mathf.Clamp01(input.move.magnitude);

            // 지금 수평 속도에서 목표 속도로 가속·감속
            Vector3 v = controller.velocity;
            float currentHorizontal = new Vector3(v.x, 0.0f, v.z).magnitude;
            const float offset = 0.1f;
            if (currentHorizontal < targetSpeed - offset || currentHorizontal > targetSpeed + offset)
                currentSpeed = Mathf.Lerp(currentHorizontal, targetSpeed * inputMagnitude, Time.deltaTime * speedChangeRate);
            else
                currentSpeed = targetSpeed;
            animationBlend = Mathf.Lerp(animationBlend, targetSpeed, Time.deltaTime * speedChangeRate);
            if (animationBlend < 0.01f) animationBlend = 0.0f;

            // 카메라 기준 방향으로 돌기
            if (input.move != Vector2.zero)
            {
                float camYaw = cameraTransform != null ? cameraTransform.eulerAngles.y : 0.0f;
                targetRotation = Mathf.Atan2(input.move.x, input.move.y) * Mathf.Rad2Deg + camYaw;
                float rotation = Mathf.SmoothDampAngle(transform.eulerAngles.y, targetRotation, ref rotationVelocity, rotationSmoothTime);
                transform.rotation = Quaternion.Euler(0.0f, rotation, 0.0f);
            }
            Vector3 direction = Quaternion.Euler(0.0f, targetRotation, 0.0f) * Vector3.forward;
            controller.Move(direction.normalized * (currentSpeed * Time.deltaTime) + new Vector3(0.0f, verticalVelocity, 0.0f) * Time.deltaTime);

            if (animator != null)
            {
                animator.SetFloat("Speed", animationBlend);
                animator.SetFloat("MotionSpeed", inputMagnitude);
            }
        }

        void JumpAndGravity()
        {
            if (grounded)
            {
                fallTimeoutDelta = fallTimeout;
                if (animator != null) { animator.SetBool("Jump", false); animator.SetBool("FreeFall", false); }
                if (verticalVelocity < 0.0f) verticalVelocity = -2.0f;   // 땅에 붙여 둔다
                if (input.jump && jumpTimeoutDelta <= 0.0f)
                {
                    verticalVelocity = Mathf.Sqrt(jumpHeight * -2.0f * gravity);   // v = √(2gh)
                    if (animator != null) animator.SetBool("Jump", true);
                }
                if (jumpTimeoutDelta >= 0.0f) jumpTimeoutDelta -= Time.deltaTime;
            }
            else
            {
                jumpTimeoutDelta = jumpTimeout;
                if (fallTimeoutDelta >= 0.0f) fallTimeoutDelta -= Time.deltaTime;
                else if (animator != null) animator.SetBool("FreeFall", true);
                input.jump = false;   // 공중에서 누른 점프는 버린다
            }
            if (verticalVelocity < terminalVelocity) verticalVelocity += gravity * Time.deltaTime;
        }
    }
}
