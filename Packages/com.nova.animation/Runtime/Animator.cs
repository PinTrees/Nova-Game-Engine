using System.Runtime.InteropServices;

namespace NovaEngine
{
    // com.nova.animation: Unity 의 Animator (상태 머신 · Blend Tree · 루트 모션 · Humanoid 리타게팅)
    internal static unsafe class AnimatorNative
    {
        const string Dll = "NovaAnimation";
        [DllImport(Dll)] internal static extern void AN_SetParam(ulong go, byte* name, int kind, float v);
        [DllImport(Dll)] internal static extern float AN_GetParam(ulong go, byte* name, int kind);
        [DllImport(Dll)] internal static extern void AN_Play(ulong go, byte* state, int layer, float normalizedTime, float fade);
        [DllImport(Dll)] internal static extern float AN_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void AN_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern void AN_GetVector(ulong go, int prop, Vector3* v);
        [DllImport(Dll)] internal static extern byte* AN_GetState(ulong go, int layer, float* outValues);
        [DllImport(Dll)] internal static extern int AN_GetBone(ulong go, int bone, float* outValues);
        [DllImport(Dll)] internal static extern void AN_SetLayerWeight(ulong go, int layer, float weight);
        [DllImport(Dll)] internal static extern float AN_GetLayerWeight(ulong go, int layer);
        [DllImport(Dll)] internal static extern int AN_GetLayerIndex(ulong go, byte* name);
        [DllImport(Dll)] internal static extern void AN_SetIK(ulong go, int goal, int kind, float* v);
        [DllImport(Dll)] internal static extern void AN_GetIK(ulong go, int goal, int kind, float* outValues);
        [DllImport(Dll)] internal static extern void AN_SetLookAt(ulong go, int kind, float* v);

        internal static byte[] Utf8(string s) => System.Text.Encoding.UTF8.GetBytes((s ?? string.Empty) + "\0");
        internal static string Str(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((System.IntPtr)p);
    }

    [NativeComponent("Animator")]
    public sealed class Animator : Behaviour
    {
        internal Animator() { }

        unsafe void Set(string n, int kind, float v) { fixed (byte* p = AnimatorNative.Utf8(n)) AnimatorNative.AN_SetParam(nativeId, p, kind, v); }
        unsafe float Get(string n, int kind) { fixed (byte* p = AnimatorNative.Utf8(n)) return AnimatorNative.AN_GetParam(nativeId, p, kind); }
        public void SetFloat(string name, float value) => Set(name, 0, value);
        public void SetInteger(string name, int value) => Set(name, 1, value);
        public void SetBool(string name, bool value) => Set(name, 2, value ? 1 : 0);
        public void SetTrigger(string name) => Set(name, 3, 1);
        public void ResetTrigger(string name) => Set(name, 3, 0);
        public float GetFloat(string name) => Get(name, 0);
        public int GetInteger(string name) => (int)Get(name, 1);
        public bool GetBool(string name) => Get(name, 2) != 0;

        /// <summary>레이어 Weight (이 Animator 만 — 0 번 레이어는 늘 1)</summary>
        public void SetLayerWeight(int layerIndex, float weight) => AnimatorNative.AN_SetLayerWeight(nativeId, layerIndex, weight);
        public float GetLayerWeight(int layerIndex) => AnimatorNative.AN_GetLayerWeight(nativeId, layerIndex);
        public unsafe int GetLayerIndex(string layerName) { fixed (byte* p = AnimatorNative.Utf8(layerName)) return AnimatorNative.AN_GetLayerIndex(nativeId, p); }

        // ---- Unity Animator IK: 레이어의 IK Pass 를 켜면 OnAnimatorIK(int layerIndex) 안에서 프레임마다 정한다 (무게는 프레임마다 0 으로 돌아간다)
        unsafe void IK(AvatarIKGoal goal, int kind, float a, float b = 0, float c = 0, float d = 0) { float* v = stackalloc float[4]; v[0] = a; v[1] = b; v[2] = c; v[3] = d; AnimatorNative.AN_SetIK(nativeId, (int)goal, kind, v); }
        unsafe float[] IKGet(int goal, int kind) { float* v = stackalloc float[4]; AnimatorNative.AN_GetIK(nativeId, goal, kind, v); return new[] { v[0], v[1], v[2], v[3] }; }
        /// <summary>손 · 발 목표 위치 (월드)</summary>
        public void SetIKPosition(AvatarIKGoal goal, Vector3 goalPosition) => IK(goal, 0, goalPosition.x, goalPosition.y, goalPosition.z);
        /// <summary>손 · 발 목표 회전 (월드, 항등 = T-포즈의 손 · 발)</summary>
        public void SetIKRotation(AvatarIKGoal goal, Quaternion goalRotation) => IK(goal, 1, goalRotation.x, goalRotation.y, goalRotation.z, goalRotation.w);
        public void SetIKPositionWeight(AvatarIKGoal goal, float value) => IK(goal, 2, value);
        public void SetIKRotationWeight(AvatarIKGoal goal, float value) => IK(goal, 3, value);
        public Vector3 GetIKPosition(AvatarIKGoal goal) { var v = IKGet((int)goal, 0); return new Vector3(v[0], v[1], v[2]); }
        public Quaternion GetIKRotation(AvatarIKGoal goal) { var v = IKGet((int)goal, 1); return new Quaternion(v[0], v[1], v[2], v[3]); }
        public float GetIKPositionWeight(AvatarIKGoal goal) => IKGet((int)goal, 2)[0];
        public float GetIKRotationWeight(AvatarIKGoal goal) => IKGet((int)goal, 3)[0];
        /// <summary>무릎 · 팔꿈치가 향할 곳 (월드)</summary>
        public void SetIKHintPosition(AvatarIKHint hint, Vector3 hintPosition) => IK((AvatarIKGoal)(int)hint, 4, hintPosition.x, hintPosition.y, hintPosition.z);
        public void SetIKHintPositionWeight(AvatarIKHint hint, float value) => IK((AvatarIKGoal)(int)hint, 5, value);
        public Vector3 GetIKHintPosition(AvatarIKHint hint) { var v = IKGet((int)hint, 4); return new Vector3(v[0], v[1], v[2]); }
        public float GetIKHintPositionWeight(AvatarIKHint hint) => IKGet((int)hint, 5)[0];
        /// <summary>시선 (월드)</summary>
        public unsafe void SetLookAtPosition(Vector3 lookAtPosition) { float* v = stackalloc float[5]; v[0] = lookAtPosition.x; v[1] = lookAtPosition.y; v[2] = lookAtPosition.z; AnimatorNative.AN_SetLookAt(nativeId, 0, v); }
        /// <summary>시선 무게: 전체 · 몸 (척추 · 가슴) · 머리 · 눈 · 제한 (0 없음 … 1 = 돌지 않음, 0.5 = 90°)</summary>
        public unsafe void SetLookAtWeight(float weight, float bodyWeight = 0f, float headWeight = 1f, float eyesWeight = 0f, float clampWeight = 0.5f)
        { float* v = stackalloc float[5]; v[0] = weight; v[1] = bodyWeight; v[2] = headWeight; v[3] = eyesWeight; v[4] = clampWeight; AnimatorNative.AN_SetLookAt(nativeId, 1, v); }

        /// <summary>재생 속도 배율 (1 = 보통)</summary>
        public float speed { get => AnimatorNative.AN_GetFloat(nativeId, 0); set => AnimatorNative.AN_SetFloat(nativeId, 0, value); }
        /// <summary>애니메이션의 이동(Hips)을 오브젝트 이동으로 쓴다</summary>
        public bool applyRootMotion { get => AnimatorNative.AN_GetFloat(nativeId, 1) != 0; set => AnimatorNative.AN_SetFloat(nativeId, 1, value ? 1 : 0); }
        /// <summary>마지막 프레임에 루트 모션이 옮긴 월드 이동</summary>
        public unsafe Vector3 deltaPosition { get { Vector3 v; AnimatorNative.AN_GetVector(nativeId, 0, &v); return v; } }
        public unsafe Vector3 velocity { get { Vector3 v; AnimatorNative.AN_GetVector(nativeId, 1, &v); return v; } }

        public unsafe void Play(string stateName, int layer = -1, float normalizedTime = float.NegativeInfinity)
        {
            fixed (byte* p = AnimatorNative.Utf8(stateName)) AnimatorNative.AN_Play(nativeId, p, layer, normalizedTime, -1);
        }
        public unsafe void CrossFade(string stateName, float normalizedTransitionDuration, int layer = -1)
        {
            // NOVA: 길이는 초 (Unity 의 CrossFadeInFixedTime 과 같다)
            fixed (byte* p = AnimatorNative.Utf8(stateName)) AnimatorNative.AN_Play(nativeId, p, layer, 0, Mathf.Max(0, normalizedTransitionDuration));
        }
        public void CrossFadeInFixedTime(string stateName, float fixedTransitionDuration, int layer = -1) => CrossFade(stateName, fixedTransitionDuration, layer);

        public unsafe AnimatorStateInfo GetCurrentAnimatorStateInfo(int layerIndex)
        {
            float* v = stackalloc float[3];
            string name = AnimatorNative.Str(AnimatorNative.AN_GetState(nativeId, layerIndex, v));
            return new AnimatorStateInfo(name, v[0], v[1]);
        }
        public unsafe bool IsInTransition(int layerIndex)
        {
            float* v = stackalloc float[3];
            AnimatorNative.AN_GetState(nativeId, layerIndex, v);
            return v[2] != 0;
        }

        /// <summary>NOVA: 사람 본의 월드 위치 (마지막 포즈, Legs/Look Animator 포함). Unity 의 GetBoneTransform(bone).position 대신 — 본은 GameObject 가 아니다</summary>
        public unsafe Vector3 GetBonePosition(HumanBodyBones bone)
        {
            float* v = stackalloc float[7];
            return AnimatorNative.AN_GetBone(nativeId, (int)bone, v) != 0 ? new Vector3(v[0], v[1], v[2]) : Vector3.zero;
        }
        /// <summary>NOVA: 사람 본의 월드 회전 (GetBoneTransform(bone).rotation 대신)</summary>
        public unsafe Quaternion GetBoneRotation(HumanBodyBones bone)
        {
            float* v = stackalloc float[7];
            return AnimatorNative.AN_GetBone(nativeId, (int)bone, v) != 0 ? new Quaternion(v[3], v[4], v[5], v[6]) : Quaternion.identity;
        }
    }

    /// <summary>Unity 의 HumanBodyBones (NOVA 아바타에 있는 본만 값을 돌려준다: 몸통·팔·다리·발가락)</summary>
    public enum HumanBodyBones
    {
        Hips = 0, LeftUpperLeg = 1, RightUpperLeg = 2, LeftLowerLeg = 3, RightLowerLeg = 4, LeftFoot = 5, RightFoot = 6,
        Spine = 7, Chest = 8, Neck = 9, Head = 10, LeftShoulder = 11, RightShoulder = 12, LeftUpperArm = 13, RightUpperArm = 14,
        LeftLowerArm = 15, RightLowerArm = 16, LeftHand = 17, RightHand = 18, LeftToes = 19, RightToes = 20,
        LeftEye = 21, RightEye = 22, Jaw = 23, UpperChest = 54, LastBone = 55
    }

    /// <summary>Unity 의 AnimatorStateInfo (이름·정규화 시간·길이)</summary>
    public struct AnimatorStateInfo
    {
        readonly string m_Name;
        public float normalizedTime { get; }
        public float length { get; }
        public int shortNameHash => m_Name == null ? 0 : m_Name.GetHashCode();
        internal AnimatorStateInfo(string name, float normalizedTime, float length) { m_Name = name; this.normalizedTime = normalizedTime; this.length = length; }
        public bool IsName(string name) => m_Name != null && (m_Name == name || name.EndsWith("." + m_Name));
        /// <summary>NOVA: 상태 이름</summary>
        public string name => m_Name;
    }
}
