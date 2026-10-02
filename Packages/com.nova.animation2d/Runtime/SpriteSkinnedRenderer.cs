using System.Runtime.InteropServices;

namespace NovaEngine
{
    internal static unsafe class Animation2DNative
    {
        const string Dll = "NovaAnimation2D";
        [DllImport(Dll)] internal static extern int SSR_Play(ulong go, byte* animation, int loop);
        [DllImport(Dll)] internal static extern byte* SSR_Animation(ulong go);
        [DllImport(Dll)] internal static extern float SSR_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void SSR_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern int SSR_GetInt(ulong go, int prop);
        [DllImport(Dll)] internal static extern void SSR_SetInt(ulong go, int prop, int v);
    }

    /// <summary>
    /// 2D 뼈대 렌더러 (.skel2d — Window > 2D Animator). Spine 의 SkeletonAnimation 처럼 Play("walk") 로 애니메이션을 바꾼다.
    /// </summary>
    [NativeComponent("SpriteSkinnedRenderer")]
    public sealed unsafe class SpriteSkinnedRenderer : Behaviour
    {
        internal SpriteSkinnedRenderer() { }

        /// <summary>처음부터 재생 (없는 이름이면 false)</summary>
        public bool Play(string animation, bool loop = true)
        {
            byte[] b = System.Text.Encoding.UTF8.GetBytes((animation ?? "") + "\0");
            fixed (byte* p = b) return Animation2DNative.SSR_Play(nativeId, p, loop ? 1 : 0) != 0;
        }
        /// <summary>지금 애니메이션 이름 (비면 뼈대의 첫 애니메이션)</summary>
        public string animationName
        {
            get
            {
                byte* p = Animation2DNative.SSR_Animation(nativeId);
                return p == null ? "" : Marshal.PtrToStringUTF8((System.IntPtr)p);
            }
        }
        /// <summary>지금 재생 시각 (초)</summary>
        public float time { get => Animation2DNative.SSR_GetFloat(nativeId, 0); set => Animation2DNative.SSR_SetFloat(nativeId, 0, value); }
        public float timeScale { get => Animation2DNative.SSR_GetFloat(nativeId, 1); set => Animation2DNative.SSR_SetFloat(nativeId, 1, value); }
        /// <summary>반복이 아닌 애니메이션이 끝났나</summary>
        public bool isComplete => Animation2DNative.SSR_GetFloat(nativeId, 2) != 0;
        public bool loop { get => Animation2DNative.SSR_GetInt(nativeId, 0) != 0; set => Animation2DNative.SSR_SetInt(nativeId, 0, value ? 1 : 0); }
        public bool flipX { get => Animation2DNative.SSR_GetInt(nativeId, 1) != 0; set => Animation2DNative.SSR_SetInt(nativeId, 1, value ? 1 : 0); }
        public int sortingOrder { get => Animation2DNative.SSR_GetInt(nativeId, 2); set => Animation2DNative.SSR_SetInt(nativeId, 2, value); }
        public Color color
        {
            get => new Color(Animation2DNative.SSR_GetFloat(nativeId, 10), Animation2DNative.SSR_GetFloat(nativeId, 11), Animation2DNative.SSR_GetFloat(nativeId, 12), Animation2DNative.SSR_GetFloat(nativeId, 13));
            set { Animation2DNative.SSR_SetFloat(nativeId, 10, value.r); Animation2DNative.SSR_SetFloat(nativeId, 11, value.g); Animation2DNative.SSR_SetFloat(nativeId, 12, value.b); Animation2DNative.SSR_SetFloat(nativeId, 13, value.a); }
        }
    }
}
