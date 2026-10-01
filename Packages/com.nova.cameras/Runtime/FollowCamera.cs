using System.Runtime.InteropServices;

namespace NovaEngine
{
    // com.nova.cameras: 3인칭 따라가기 카메라 (C++ 컴포넌트 FollowCamera, Plugins/NovaCameras.dll)
    [NativeComponent("FollowCamera")]
    public sealed class FollowCamera : Component
    {
        const string Dll = "NovaCameras";
        [DllImport(Dll)] static extern float NovaCameras_GetFloat(ulong go, int prop);
        [DllImport(Dll)] static extern void NovaCameras_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] static extern int NovaCameras_GetBool(ulong go, int prop);
        [DllImport(Dll)] static extern void NovaCameras_SetBool(ulong go, int prop, int v);
        [DllImport(Dll)] static extern ulong NovaCameras_GetTarget(ulong go);
        [DllImport(Dll)] static extern void NovaCameras_SetTarget(ulong go, ulong target);
        [DllImport(Dll)] static extern void NovaCameras_Snap(ulong go);

        internal FollowCamera() { }

        /// <summary>따라갈 대상 (null = 없음)</summary>
        public GameObject target
        {
            get => FromNativeId(NovaCameras_GetTarget(nativeId));
            set => NovaCameras_SetTarget(nativeId, value == null ? 0 : GetNativeId(value));
        }
        public float distance { get => NovaCameras_GetFloat(nativeId, 0); set => NovaCameras_SetFloat(nativeId, 0, value); }
        public float height { get => NovaCameras_GetFloat(nativeId, 1); set => NovaCameras_SetFloat(nativeId, 1, value); }
        public float lookAtHeight { get => NovaCameras_GetFloat(nativeId, 2); set => NovaCameras_SetFloat(nativeId, 2, value); }
        public float damping { get => NovaCameras_GetFloat(nativeId, 3); set => NovaCameras_SetFloat(nativeId, 3, value); }
        public float yaw { get => NovaCameras_GetFloat(nativeId, 4); set => NovaCameras_SetFloat(nativeId, 4, value); }
        public float obstaclePadding { get => NovaCameras_GetFloat(nativeId, 5); set => NovaCameras_SetFloat(nativeId, 5, value); }
        public bool followTargetRotation { get => NovaCameras_GetBool(nativeId, 0) != 0; set => NovaCameras_SetBool(nativeId, 0, value ? 1 : 0); }
        public bool avoidObstacles { get => NovaCameras_GetBool(nativeId, 1) != 0; set => NovaCameras_SetBool(nativeId, 1, value ? 1 : 0); }

        /// <summary>다음 프레임에 부드럽게 따라가지 않고 바로 제자리로</summary>
        public void Snap() => NovaCameras_Snap(nativeId);
    }
}
