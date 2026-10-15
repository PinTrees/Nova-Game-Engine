using System.Runtime.InteropServices;

namespace NovaEngine
{
    // com.nova.timeline: Unity 의 PlayableDirector (UnityEngine.Playables) — Timeline (.playable) 재생
    internal static unsafe class TimelineNative
    {
        const string Dll = "NovaTimeline";
        [DllImport(Dll)] internal static extern void TL_Call(ulong go, int op);
        [DllImport(Dll)] internal static extern double TL_Get(ulong go, int prop);
        [DllImport(Dll)] internal static extern void TL_Set(ulong go, int prop, double v);
        [DllImport(Dll)] internal static extern void TL_SetBinding(ulong go, byte* track, ulong target);
        [DllImport(Dll)] internal static extern ulong TL_GetBinding(ulong go, byte* track);
        [DllImport(Dll)] internal static extern byte* TL_GetAsset(ulong go);
        [DllImport(Dll)] internal static extern void TL_SetAsset(ulong go, byte* path);
        internal static byte[] Utf8(string s) => System.Text.Encoding.UTF8.GetBytes((s ?? string.Empty) + "\0");
        internal static string Str(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((System.IntPtr)p);
    }

    /// <summary>Unity 의 PlayState</summary>
    public enum PlayState { Paused = 0, Playing = 1 }
    /// <summary>Unity 의 DirectorWrapMode (끝에서: Hold = 마지막 장면 · Loop · None = 멈춤)</summary>
    public enum DirectorWrapMode { Hold = 0, Loop = 1, None = 2 }

    [NativeComponent("PlayableDirector")]
    public sealed class PlayableDirector : Behaviour
    {
        internal PlayableDirector() { }
        public void Play() => TimelineNative.TL_Call(nativeId, 0);
        public void Pause() => TimelineNative.TL_Call(nativeId, 1);
        public void Resume() => TimelineNative.TL_Call(nativeId, 2);
        /// <summary>멈추고 처음으로 — 켜고 끈 오브젝트 · Animator 덮기를 되돌린다</summary>
        public void Stop() => TimelineNative.TL_Call(nativeId, 3);
        /// <summary>지금 time 을 평가 (Manual 업데이트 · 일시 정지 중 시간 이동 뒤)</summary>
        public void Evaluate() => TimelineNative.TL_Call(nativeId, 4);
        public double time { get => TimelineNative.TL_Get(nativeId, 0); set => TimelineNative.TL_Set(nativeId, 0, value); }
        public double duration => TimelineNative.TL_Get(nativeId, 1);
        public PlayState state => TimelineNative.TL_Get(nativeId, 2) != 0 ? PlayState.Playing : PlayState.Paused;
        public DirectorWrapMode extrapolationMode { get => (DirectorWrapMode)(int)TimelineNative.TL_Get(nativeId, 3); set => TimelineNative.TL_Set(nativeId, 3, (int)value); }
        public double initialTime { get => TimelineNative.TL_Get(nativeId, 4); set => TimelineNative.TL_Set(nativeId, 4, value); }
        public bool playOnAwake { get => TimelineNative.TL_Get(nativeId, 5) != 0; set => TimelineNative.TL_Set(nativeId, 5, value ? 1 : 0); }
        /// <summary>NOVA: Timeline 에셋 경로 (Unity 의 playableAsset)</summary>
        public unsafe string playableAssetPath
        {
            get => TimelineNative.Str(TimelineNative.TL_GetAsset(nativeId));
            set { fixed (byte* p = TimelineNative.Utf8(value)) TimelineNative.TL_SetAsset(nativeId, p); }
        }
        /// <summary>트랙 (id · 이름 · 번호) 에 묶을 GameObject (Unity 의 SetGenericBinding)</summary>
        public unsafe void SetGenericBinding(string track, GameObject value) { fixed (byte* p = TimelineNative.Utf8(track)) TimelineNative.TL_SetBinding(nativeId, p, GetNativeId(value)); }
        public unsafe GameObject GetGenericBinding(string track) { fixed (byte* p = TimelineNative.Utf8(track)) return FromNativeId(TimelineNative.TL_GetBinding(nativeId, p)); }
    }
}
