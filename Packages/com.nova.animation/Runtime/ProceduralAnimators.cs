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
}
