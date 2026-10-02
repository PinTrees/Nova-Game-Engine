using System.Runtime.InteropServices;

namespace NovaEngine
{
    internal static unsafe class ProceduralNative
    {
        const string Dll = "NovaAnimation";
        [DllImport(Dll)] internal static extern float LEGS_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void LEGS_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern int LEGS_GetBool(ulong go, int prop);
        [DllImport(Dll)] internal static extern void LEGS_SetBool(ulong go, int prop, int v);
        [DllImport(Dll)] internal static extern float LOOK_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void LOOK_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern ulong LOOK_GetTarget(ulong go);
        [DllImport(Dll)] internal static extern void LOOK_SetTarget(ulong go, ulong target);
        [DllImport(Dll)] internal static extern void LOOK_SetPosition(ulong go, Vector3* p, int use);
        [DllImport(Dll)] internal static extern float HANDS_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void HANDS_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern ulong HANDS_GetObject(ulong go, int hand, int which);
        [DllImport(Dll)] internal static extern void HANDS_SetObject(ulong go, int hand, int which, ulong target);
        [DllImport(Dll)] internal static extern void HANDS_SetIK(ulong go, int hand, int mode, float* v);
        [DllImport(Dll)] internal static extern void HANDS_GetHand(ulong go, int hand, Vector3* v);
        [DllImport(Dll)] internal static extern float DYN_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void DYN_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern void DYN_GetVector(ulong go, int prop, Vector3* v);
        [DllImport(Dll)] internal static extern void DYN_SetVector(ulong go, int prop, Vector3* v);
        [DllImport(Dll)] internal static extern int DYN_Info(ulong go, int what, int chain);
        [DllImport(Dll)] internal static extern int DYN_GetTail(ulong go, int chain, int joint, Vector3* v);
        [DllImport(Dll)] internal static extern void DYN_Reset(ulong go);
    }

    /// <summary>
    /// Legs Animator: 발을 실제 바닥(계단·경사)에 붙이고, 낮은 쪽 발에 맞춰 엉덩이를 내린다 (Animator 와 같은 GameObject, Play 중).
    /// </summary>
    [NativeComponent("LegsAnimator")]
    public sealed class LegsAnimator : Behaviour
    {
        internal LegsAnimator() { }
        /// <summary>0 = 애니메이션 그대로, 1 = 바닥에 완전히</summary>
        public float weight { get => ProceduralNative.LEGS_GetFloat(nativeId, 0); set => ProceduralNative.LEGS_SetFloat(nativeId, 0, value); }
        public float maxStepDown { get => ProceduralNative.LEGS_GetFloat(nativeId, 1); set => ProceduralNative.LEGS_SetFloat(nativeId, 1, value); }
        public float maxStepUp { get => ProceduralNative.LEGS_GetFloat(nativeId, 2); set => ProceduralNative.LEGS_SetFloat(nativeId, 2, value); }
        public float hipsMaxDown { get => ProceduralNative.LEGS_GetFloat(nativeId, 3); set => ProceduralNative.LEGS_SetFloat(nativeId, 3, value); }
        public bool adjustHips { get => ProceduralNative.LEGS_GetBool(nativeId, 0) != 0; set => ProceduralNative.LEGS_SetBool(nativeId, 0, value ? 1 : 0); }
        public bool alignFeet { get => ProceduralNative.LEGS_GetBool(nativeId, 1) != 0; set => ProceduralNative.LEGS_SetBool(nativeId, 1, value ? 1 : 0); }
        /// <summary>디딘 발을 그 자리에 고정 (미끄럼 방지)</summary>
        public bool footLocking { get => ProceduralNative.LEGS_GetBool(nativeId, 2) != 0; set => ProceduralNative.LEGS_SetBool(nativeId, 2, value ? 1 : 0); }
        /// <summary>경사에서 상체 기울이기 (0 ~ 1, 경사각에 곱함)</summary>
        public float bodyLean { get => ProceduralNative.LEGS_GetFloat(nativeId, 4); set => ProceduralNative.LEGS_SetFloat(nativeId, 4, value); }
        public float maxLean { get => ProceduralNative.LEGS_GetFloat(nativeId, 5); set => ProceduralNative.LEGS_SetFloat(nativeId, 5, value); }
        /// <summary>지금 기울인 각도 (도, + = 앞으로)</summary>
        public float lean => ProceduralNative.LEGS_GetFloat(nativeId, 6);
        /// <summary>그 발이 지금 고정돼 있나 (LeftFoot / RightFoot)</summary>
        public bool IsFootLocked(AvatarIKGoal foot) => ProceduralNative.LEGS_GetBool(nativeId, foot == AvatarIKGoal.RightFoot ? 11 : 10) != 0;
    }

    /// <summary>Unity 의 AvatarIKGoal</summary>
    public enum AvatarIKGoal { LeftFoot = 0, RightFoot = 1, LeftHand = 2, RightHand = 3 }

    /// <summary>
    /// Hands Animator: 손을 목표에 (무기 손잡이 · 벽 짚기). 목표 오브젝트 또는 SetIKPosition / SetIKRotation (Unity 의 Animator.SetIKPosition 과 같은 생각).
    /// </summary>
    [NativeComponent("HandsAnimator")]
    public sealed class HandsAnimator : Behaviour
    {
        internal HandsAnimator() { }
        static int Hand(AvatarIKGoal g) => g == AvatarIKGoal.RightHand ? 1 : 0;
        public float weight { get => ProceduralNative.HANDS_GetFloat(nativeId, 0); set => ProceduralNative.HANDS_SetFloat(nativeId, 0, value); }
        public float blendSpeed { get => ProceduralNative.HANDS_GetFloat(nativeId, 1); set => ProceduralNative.HANDS_SetFloat(nativeId, 1, value); }
        public GameObject leftHandTarget { get => FromNativeId(ProceduralNative.HANDS_GetObject(nativeId, 0, 0)); set => ProceduralNative.HANDS_SetObject(nativeId, 0, 0, GetNativeId(value)); }
        public GameObject rightHandTarget { get => FromNativeId(ProceduralNative.HANDS_GetObject(nativeId, 1, 0)); set => ProceduralNative.HANDS_SetObject(nativeId, 1, 0, GetNativeId(value)); }
        public GameObject leftElbowHint { get => FromNativeId(ProceduralNative.HANDS_GetObject(nativeId, 0, 1)); set => ProceduralNative.HANDS_SetObject(nativeId, 0, 1, GetNativeId(value)); }
        public GameObject rightElbowHint { get => FromNativeId(ProceduralNative.HANDS_GetObject(nativeId, 1, 1)); set => ProceduralNative.HANDS_SetObject(nativeId, 1, 1, GetNativeId(value)); }
        public float GetIKPositionWeight(AvatarIKGoal goal) => ProceduralNative.HANDS_GetFloat(nativeId, 10 + Hand(goal));
        public void SetIKPositionWeight(AvatarIKGoal goal, float value) => ProceduralNative.HANDS_SetFloat(nativeId, 10 + Hand(goal), value);
        public float GetIKRotationWeight(AvatarIKGoal goal) => ProceduralNative.HANDS_GetFloat(nativeId, 20 + Hand(goal));
        public void SetIKRotationWeight(AvatarIKGoal goal, float value) => ProceduralNative.HANDS_SetFloat(nativeId, 20 + Hand(goal), value);
        /// <summary>손 목표 위치 (월드, ClearIK 까지 Target 오브젝트 대신)</summary>
        public unsafe void SetIKPosition(AvatarIKGoal goal, Vector3 position) { float* v = stackalloc float[3]; v[0] = position.x; v[1] = position.y; v[2] = position.z; ProceduralNative.HANDS_SetIK(nativeId, Hand(goal), 0, v); }
        /// <summary>손 목표 회전 (월드, 항등 = T-포즈의 손)</summary>
        public unsafe void SetIKRotation(AvatarIKGoal goal, Quaternion rotation) { float* v = stackalloc float[4]; v[0] = rotation.x; v[1] = rotation.y; v[2] = rotation.z; v[3] = rotation.w; ProceduralNative.HANDS_SetIK(nativeId, Hand(goal), 1, v); }
        public unsafe void ClearIK(AvatarIKGoal goal) => ProceduralNative.HANDS_SetIK(nativeId, Hand(goal), 2, null);
        /// <summary>지난 프레임에 손이 실제로 간 곳 (월드)</summary>
        public unsafe Vector3 GetHandPosition(AvatarIKGoal goal) { Vector3 v; ProceduralNative.HANDS_GetHand(nativeId, Hand(goal), &v); return v; }
    }

    /// <summary>
    /// Look Animator: 머리·목·가슴·척추가 대상 쪽으로 돌아간다 (각도 제한, 뒤쪽이면 정면으로).
    /// </summary>
    [NativeComponent("LookAnimator")]
    public sealed class LookAnimator : Behaviour
    {
        internal LookAnimator() { }
        public float weight { get => ProceduralNative.LOOK_GetFloat(nativeId, 0); set => ProceduralNative.LOOK_SetFloat(nativeId, 0, value); }
        public float maxYaw { get => ProceduralNative.LOOK_GetFloat(nativeId, 1); set => ProceduralNative.LOOK_SetFloat(nativeId, 1, value); }
        public float speed { get => ProceduralNative.LOOK_GetFloat(nativeId, 2); set => ProceduralNative.LOOK_SetFloat(nativeId, 2, value); }
        /// <summary>볼 대상 (null = 보지 않음)</summary>
        public GameObject target
        {
            get => FromNativeId(ProceduralNative.LOOK_GetTarget(nativeId));
            set => ProceduralNative.LOOK_SetTarget(nativeId, GetNativeId(value));
        }
        /// <summary>오브젝트 대신 이 위치를 본다 (ClearLookAtPosition 까지)</summary>
        public unsafe void SetLookAtPosition(Vector3 position) => ProceduralNative.LOOK_SetPosition(nativeId, &position, 1);
        public unsafe void ClearLookAtPosition() => ProceduralNative.LOOK_SetPosition(nativeId, null, 0);
    }

    /// <summary>
    /// Dynamic Bone: 머리카락 · 치마 · 꼬리 · 끈이 움직임 · 중력 · 바람에 흔들리고 충돌체(몸) 밖으로 밀린다 (Play 중, Animator 와 같은 GameObject).
    /// VRM 캐릭터는 파일의 Spring Bone 설정으로 자동으로 붙는다.
    /// </summary>
    [NativeComponent("DynamicBone")]
    public sealed class DynamicBone : Behaviour
    {
        internal DynamicBone() { }
        /// <summary>0 = 애니메이션 그대로, 1 = 흔들림 전부</summary>
        public float weight { get => ProceduralNative.DYN_GetFloat(nativeId, 0); set => ProceduralNative.DYN_SetFloat(nativeId, 0, value); }
        public float stiffnessScale { get => ProceduralNative.DYN_GetFloat(nativeId, 1); set => ProceduralNative.DYN_SetFloat(nativeId, 1, value); }
        public float gravityScale { get => ProceduralNative.DYN_GetFloat(nativeId, 2); set => ProceduralNative.DYN_SetFloat(nativeId, 2, value); }
        public float dragScale { get => ProceduralNative.DYN_GetFloat(nativeId, 3); set => ProceduralNative.DYN_SetFloat(nativeId, 3, value); }
        public float windTurbulence { get => ProceduralNative.DYN_GetFloat(nativeId, 4); set => ProceduralNative.DYN_SetFloat(nativeId, 4, value); }
        /// <summary>바람 (월드, m/s)</summary>
        public unsafe Vector3 wind
        {
            get { Vector3 v; ProceduralNative.DYN_GetVector(nativeId, 0, &v); return v; }
            set => ProceduralNative.DYN_SetVector(nativeId, 0, &value);
        }
        public int chainCount => ProceduralNative.DYN_Info(nativeId, 0, 0);
        public int colliderCount => ProceduralNative.DYN_Info(nativeId, 2, 0);
        public int GetBoneCount(int chain) => ProceduralNative.DYN_Info(nativeId, 1, chain);
        /// <summary>마지막 프레임에 흔들린 꼬리 위치 (월드)</summary>
        public unsafe bool TryGetTailPosition(int chain, int bone, out Vector3 position)
        {
            Vector3 v;
            int ok = ProceduralNative.DYN_GetTail(nativeId, chain, bone, &v);
            position = v;
            return ok != 0;
        }
        /// <summary>흔들림을 지금 자세로 되돌린다 (순간 이동 뒤)</summary>
        public void ResetSimulation() => ProceduralNative.DYN_Reset(nativeId);
    }
}
