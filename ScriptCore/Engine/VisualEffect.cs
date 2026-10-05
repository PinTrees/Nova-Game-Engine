using System.Runtime.InteropServices;
using NovaEngine.Interop;

namespace NovaEngine.VFX
{
    // Source/Effects/VfxScripting.cpp 가 이름으로 내보낸 NovaVfx_* 함수
    internal static unsafe class VfxNative
    {
        const string Lib = "NovaCore";
        [DllImport(Lib)] internal static extern int NovaVfx_Call(ulong id, int op, byte* name);
        [DllImport(Lib)] internal static extern int NovaVfx_GetProperty(ulong id, byte* name, float* value);
        [DllImport(Lib)] internal static extern int NovaVfx_SetProperty(ulong id, byte* name, float* value);
        [DllImport(Lib)] internal static extern float NovaVfx_GetFloat(ulong id, int which);
        [DllImport(Lib)] internal static extern void NovaVfx_SetFloat(ulong id, int which, float value);
        [DllImport(Lib)] internal static extern byte* NovaVfx_GetString(ulong id, int which);
        [DllImport(Lib)] internal static extern void NovaVfx_SetString(ulong id, int which, byte* value);
    }

    /// <summary>
    /// Unity 의 VisualEffect (VFX Graph 를 재생하는 컴포넌트). GPU 파티클 — 시뮬레이션 · 그리기는 엔진이 한다.
    ///   vfx.SetFloat("Spin", 90); vfx.SetVector4("Main Color", new Vector4(1, 0.3f, 0.1f, 1)); vfx.SendEvent("OnPlay");
    /// </summary>
    public sealed unsafe class VisualEffect : Component
    {
        internal VisualEffect() { }

        // 속성 종류 (NovaVfx_GetProperty 반환 - 1): 0 Float, 1 Int, 2 Bool, 3 Vector3, 4 Color
        int Get(string name, out Vector4 v)
        {
            float* f = stackalloc float[4];
            int kind;
            fixed (byte* n = Native.Utf8(name)) kind = VfxNative.NovaVfx_GetProperty(m_Id, n, f);
            v = new Vector4(f[0], f[1], f[2], f[3]);
            return kind - 1;
        }

        void Set(string name, float x, float y = 0f, float z = 0f, float w = 0f)
        {
            float* f = stackalloc float[4];
            f[0] = x; f[1] = y; f[2] = z; f[3] = w;
            fixed (byte* n = Native.Utf8(name)) VfxNative.NovaVfx_SetProperty(m_Id, n, f);
        }

        int Call(int op, string name = null)
        {
            if (name == null) return VfxNative.NovaVfx_Call(m_Id, op, null);
            fixed (byte* n = Native.Utf8(name)) return VfxNative.NovaVfx_Call(m_Id, op, n);
        }

        // ---- 재생 · 이벤트
        public void Play() => Call(0);
        public void Stop() => Call(1);
        public void Reinit() => Call(2);
        public void SendEvent(string eventName) => Call(3, eventName);
        public bool HasAnySystemAwake() => VfxNative.NovaVfx_GetFloat(m_Id, 5) != 0f;

        // ---- Exposed Property (Blackboard)
        public void SetFloat(string name, float value) => Set(name, value);
        public void SetInt(string name, int value) => Set(name, value);
        public void SetBool(string name, bool value) => Set(name, value ? 1f : 0f);
        public void SetVector2(string name, Vector2 value) => Set(name, value.x, value.y);
        public void SetVector3(string name, Vector3 value) => Set(name, value.x, value.y, value.z);
        public void SetVector4(string name, Vector4 value) => Set(name, value.x, value.y, value.z, value.w);   // Color 속성도 (Unity 와 같음)
        public float GetFloat(string name) { Get(name, out var v); return v.x; }
        public int GetInt(string name) { Get(name, out var v); return (int)v.x; }
        public bool GetBool(string name) { Get(name, out var v); return v.x != 0f; }
        public Vector2 GetVector2(string name) { Get(name, out var v); return new Vector2(v.x, v.y); }
        public Vector3 GetVector3(string name) { Get(name, out var v); return new Vector3(v.x, v.y, v.z); }
        public Vector4 GetVector4(string name) { Get(name, out var v); return v; }
        public bool HasFloat(string name) => Get(name, out _) == 0;
        public bool HasInt(string name) => Get(name, out _) == 1;
        public bool HasBool(string name) => Get(name, out _) == 2;
        public bool HasVector3(string name) => Get(name, out _) == 3;
        public bool HasVector4(string name) { int k = Get(name, out _); return k == 4 || k == 3; }
        public void ResetOverride(string name) => Call(4, name);

        // ---- 상태 · 설정
        public int aliveParticleCount => (int)VfxNative.NovaVfx_GetFloat(m_Id, 0);   // GPU 가 센 수 (몇 프레임 늦다)
        public bool culled => false;
        public float playRate { get => VfxNative.NovaVfx_GetFloat(m_Id, 1); set => VfxNative.NovaVfx_SetFloat(m_Id, 1, value); }
        public bool pause { get => VfxNative.NovaVfx_GetFloat(m_Id, 2) != 0f; set => VfxNative.NovaVfx_SetFloat(m_Id, 2, value ? 1f : 0f); }
        public uint startSeed { get => (uint)VfxNative.NovaVfx_GetFloat(m_Id, 3); set => VfxNative.NovaVfx_SetFloat(m_Id, 3, value); }
        public bool resetSeedOnPlay { get => VfxNative.NovaVfx_GetFloat(m_Id, 4) != 0f; set => VfxNative.NovaVfx_SetFloat(m_Id, 4, value ? 1f : 0f); }
        public bool enabled { get => VfxNative.NovaVfx_GetFloat(m_Id, 6) != 0f; set => VfxNative.NovaVfx_SetFloat(m_Id, 6, value ? 1f : 0f); }
        public string initialEventName
        {
            get => Native.Str(VfxNative.NovaVfx_GetString(m_Id, 1)) ?? string.Empty;
            set { fixed (byte* p = Native.Utf8(value ?? string.Empty)) VfxNative.NovaVfx_SetString(m_Id, 1, p); }
        }
        /// <summary>.vfx 에셋 경로 (Unity 의 visualEffectAsset — 바꾸면 그 에셋으로 다시 시작)</summary>
        public string visualEffectAsset
        {
            get => Native.Str(VfxNative.NovaVfx_GetString(m_Id, 0)) ?? string.Empty;
            set { fixed (byte* p = Native.Utf8(value ?? string.Empty)) VfxNative.NovaVfx_SetString(m_Id, 0, p); }
        }
    }
}
