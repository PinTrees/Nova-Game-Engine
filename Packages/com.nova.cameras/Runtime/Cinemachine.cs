using System.Runtime.InteropServices;

namespace NovaEngine.Cinemachine
{
    // com.nova.cameras: Unity Cinemachine 3 (Unity.Cinemachine) 와 같은 이름의 C# API — C++ 컴포넌트 (Plugins/NovaCameras.dll)
    //  Unity 와 다른 점: 묶음 설정 (Lens · Target · TrackerSettings · Composition · 축 …) 은 struct 가 아니라 바로 쓰이는 class 라
    //  cam.Lens.FieldOfView = 40 · orbital.HorizontalAxis.Value += 10 이 그대로 된다. Priority 는 cam.Priority = 10 (int 로)
    internal static unsafe class CmNative
    {
        const string Dll = "NovaCameras";
        [DllImport(Dll)] internal static extern float NovaCM_GetFloat(ulong go, int kind, int prop);
        [DllImport(Dll)] internal static extern void NovaCM_SetFloat(ulong go, int kind, int prop, float v);
        [DllImport(Dll)] internal static extern int NovaCM_GetInt(ulong go, int kind, int prop);
        [DllImport(Dll)] internal static extern void NovaCM_SetInt(ulong go, int kind, int prop, int v);
        [DllImport(Dll)] internal static extern int NovaCM_GetBool(ulong go, int kind, int prop);
        [DllImport(Dll)] internal static extern void NovaCM_SetBool(ulong go, int kind, int prop, int v);
        [DllImport(Dll)] internal static extern void NovaCM_GetVector(ulong go, int kind, int prop, Vector3* v);
        [DllImport(Dll)] internal static extern void NovaCM_SetVector(ulong go, int kind, int prop, Vector3* v);
        [DllImport(Dll)] internal static extern ulong NovaCM_GetObject(ulong go, int kind, int prop);
        [DllImport(Dll)] internal static extern void NovaCM_SetObject(ulong go, int kind, int prop, ulong v);
        [DllImport(Dll)] internal static extern float NovaCM_Call(ulong go, int kind, int fn, Vector3* a, Vector3* b, float f);
        [DllImport(Dll)] internal static extern ulong NovaCM_BrainLive(ulong go);

        // 종류 번호 (Source/Package.cpp 의 Kind 와 같다)
        internal const int Camera = 0, Brain = 1, Follow = 2, Orbital = 3, ThirdPerson = 4, Composer = 5, HardLookAt = 6, RotateWithTarget = 7,
            Perlin = 8, ImpulseSource = 9, ImpulseListener = 10;

        internal static Vector3 GetVec(ulong go, int kind, int prop) { Vector3 v; NovaCM_GetVector(go, kind, prop, &v); return v; }
        internal static void SetVec(ulong go, int kind, int prop, Vector3 v) => NovaCM_SetVector(go, kind, prop, &v);
        internal static Vector2 GetVec2(ulong go, int kind, int prop) { var v = GetVec(go, kind, prop); return new Vector2(v.x, v.y); }
        internal static void SetVec2(ulong go, int kind, int prop, Vector2 v) => SetVec(go, kind, prop, new Vector3(v.x, v.y, 0f));
        internal static bool GetBool(ulong go, int kind, int prop) => NovaCM_GetBool(go, kind, prop) != 0;
        internal static void SetBool(ulong go, int kind, int prop, bool v) => NovaCM_SetBool(go, kind, prop, v ? 1 : 0);
        internal static float Call(ulong go, int kind, int fn) => NovaCM_Call(go, kind, fn, null, null, 0f);
        internal static float Call(ulong go, int kind, int fn, Vector3 a, Vector3 b, float f) => NovaCM_Call(go, kind, fn, &a, &b, f);
    }

    // ------------------------------------------------------------------ 공통 타입
    public enum BindingMode { LockToTargetOnAssign, LockToTargetWithWorldUp, LockToTargetNoRoll, LockToTarget, WorldSpace, LazyFollow }
    public enum AngularDampingMode { Euler, Quaternion }

    /// <summary>섞기 (Unity CinemachineBlendDefinition)</summary>
    public struct CinemachineBlendDefinition
    {
        public enum Styles { Cut, EaseInOut, EaseIn, EaseOut, HardIn, HardOut, Linear }
        public Styles Style;
        public float Time;
        public CinemachineBlendDefinition(Styles style, float time) { Style = style; Time = time; }
        public float BlendTime => Style == Styles.Cut ? 0f : Time;
    }

    /// <summary>Priority (Unity PrioritySettings) — int 로 바로: cam.Priority = 10</summary>
    public struct PrioritySettings
    {
        public bool Enabled;
        public int Value;
        public static implicit operator int(PrioritySettings p) => p.Value;
        public static implicit operator PrioritySettings(int v) => new PrioritySettings { Enabled = true, Value = v };
        public override string ToString() => Value.ToString();
    }

    /// <summary>렌즈 (Unity LensSettings). 가상 카메라에서 얻은 것은 바로 쓰인다 (cam.Lens.FieldOfView = 40)</summary>
    public sealed class LensSettings
    {
        readonly ulong m_Go;
        float m_Fov = 60f, m_Ortho = 5f, m_Near = 0.3f, m_Far = 1000f, m_Dutch;
        public LensSettings() { }
        internal LensSettings(ulong go) { m_Go = go; }
        public static LensSettings Default => new LensSettings();

        float Get(int p, float local) => m_Go != 0 ? CmNative.NovaCM_GetFloat(m_Go, CmNative.Camera, p) : local;
        void Set(int p, float v, ref float local) { local = v; if (m_Go != 0) CmNative.NovaCM_SetFloat(m_Go, CmNative.Camera, p, v); }
        public float FieldOfView { get => Get(0, m_Fov); set => Set(0, value, ref m_Fov); }
        public float OrthographicSize { get => Get(1, m_Ortho); set => Set(1, value, ref m_Ortho); }
        public float NearClipPlane { get => Get(2, m_Near); set => Set(2, value, ref m_Near); }
        public float FarClipPlane { get => Get(3, m_Far); set => Set(3, value, ref m_Far); }
        public float Dutch { get => Get(4, m_Dutch); set => Set(4, value, ref m_Dutch); }
        internal void CopyTo(LensSettings o)
        {
            o.FieldOfView = FieldOfView; o.OrthographicSize = OrthographicSize; o.NearClipPlane = NearClipPlane; o.FarClipPlane = FarClipPlane; o.Dutch = Dutch;
        }
    }

    /// <summary>따라갈 · 볼 대상 (Unity CameraTarget)</summary>
    public sealed class CameraTarget
    {
        readonly ulong m_Go;
        internal CameraTarget(ulong go) { m_Go = go; }
        static Transform T(ulong id) => id == 0 ? null : CinemachineCamera.GameObjectOf(id).transform;
        static ulong Id(Transform t) => t == null ? 0 : CinemachineCamera.IdOf(t.gameObject);
        public Transform TrackingTarget { get => T(CmNative.NovaCM_GetObject(m_Go, CmNative.Camera, 0)); set => CmNative.NovaCM_SetObject(m_Go, CmNative.Camera, 0, Id(value)); }
        public Transform LookAtTarget { get => T(CmNative.NovaCM_GetObject(m_Go, CmNative.Camera, 1)); set => CmNative.NovaCM_SetObject(m_Go, CmNative.Camera, 1, Id(value)); }
        public bool CustomLookAtTarget { get => CmNative.GetBool(m_Go, CmNative.Camera, 0); set => CmNative.SetBool(m_Go, CmNative.Camera, 0, value); }
    }

    /// <summary>입력 축 (Unity InputAxis): Orbital Follow 의 가로 · 세로 · 거리</summary>
    public sealed class InputAxis
    {
        readonly ulong m_Go; readonly int m_Value, m_Min, m_Max, m_Center, m_Wrap;
        internal InputAxis(ulong go, int value, int min, int max, int center, int wrap) { m_Go = go; m_Value = value; m_Min = min; m_Max = max; m_Center = center; m_Wrap = wrap; }
        float F(int p) => CmNative.NovaCM_GetFloat(m_Go, CmNative.Orbital, p);
        void F(int p, float v) => CmNative.NovaCM_SetFloat(m_Go, CmNative.Orbital, p, v);
        public float Value { get => F(m_Value); set => F(m_Value, value); }
        public Vector2 Range { get => new Vector2(F(m_Min), F(m_Max)); set { F(m_Min, value.x); F(m_Max, value.y); } }
        public float Center { get => F(m_Center); set => F(m_Center, value); }
        public bool Wrap { get => CmNative.GetBool(m_Go, CmNative.Orbital, m_Wrap); set => CmNative.SetBool(m_Go, CmNative.Orbital, m_Wrap, value); }
        /// <summary>범위 안 자리 (0..1)</summary>
        public float GetNormalizedValue() { var r = Range; return r.y > r.x ? (Value - r.x) / (r.y - r.x) : 0.5f; }
    }

    /// <summary>대상 따라가기 (Unity TrackerSettings)</summary>
    public sealed class TrackerSettings
    {
        readonly ulong m_Go; readonly int m_Kind, m_Binding, m_Angular, m_PosDamp, m_RotDamp, m_QuatDamp;
        internal TrackerSettings(ulong go, int kind, int binding, int angular, int posDamp, int rotDamp, int quatDamp)
        { m_Go = go; m_Kind = kind; m_Binding = binding; m_Angular = angular; m_PosDamp = posDamp; m_RotDamp = rotDamp; m_QuatDamp = quatDamp; }
        public BindingMode BindingMode { get => (BindingMode)CmNative.NovaCM_GetInt(m_Go, m_Kind, m_Binding); set => CmNative.NovaCM_SetInt(m_Go, m_Kind, m_Binding, (int)value); }
        public Vector3 PositionDamping { get => CmNative.GetVec(m_Go, m_Kind, m_PosDamp); set => CmNative.SetVec(m_Go, m_Kind, m_PosDamp, value); }
        public AngularDampingMode AngularDampingMode { get => (AngularDampingMode)CmNative.NovaCM_GetInt(m_Go, m_Kind, m_Angular); set => CmNative.NovaCM_SetInt(m_Go, m_Kind, m_Angular, (int)value); }
        public Vector3 RotationDamping { get => CmNative.GetVec(m_Go, m_Kind, m_RotDamp); set => CmNative.SetVec(m_Go, m_Kind, m_RotDamp, value); }
        public float QuaternionDamping { get => CmNative.NovaCM_GetFloat(m_Go, m_Kind, m_QuatDamp); set => CmNative.NovaCM_SetFloat(m_Go, m_Kind, m_QuatDamp, value); }
    }

    // ------------------------------------------------------------------ Brain · 가상 카메라
    /// <summary>실제 Camera 를 움직이는 것 (Main Camera 에). Live = Priority 가 가장 높은 켜진 가상 카메라</summary>
    [NativeComponent("CinemachineBrain")]
    public sealed class CinemachineBrain : Behaviour
    {
        internal CinemachineBrain() { }
        /// <summary>지금 Live 인 가상 카메라 (없으면 null)</summary>
        public CinemachineCamera ActiveVirtualCamera
        {
            get { var id = CmNative.NovaCM_BrainLive(nativeId); return id == 0 ? null : FromNativeId(id).GetComponent<CinemachineCamera>(); }
        }
        public bool IsBlending => CmNative.Call(nativeId, CmNative.Brain, 0) != 0f;
        public CinemachineBlendDefinition DefaultBlend
        {
            get => new CinemachineBlendDefinition((CinemachineBlendDefinition.Styles)CmNative.NovaCM_GetInt(nativeId, CmNative.Brain, 0), CmNative.NovaCM_GetFloat(nativeId, CmNative.Brain, 0));
            set { CmNative.NovaCM_SetInt(nativeId, CmNative.Brain, 0, (int)value.Style); CmNative.NovaCM_SetFloat(nativeId, CmNative.Brain, 0, value.Time); }
        }
        public bool IsLiveChild(CinemachineCamera vcam) => vcam != null && vcam.IsLive;
    }

    /// <summary>가상 카메라 (Unity CinemachineCamera)</summary>
    [NativeComponent("CinemachineCamera")]
    public sealed class CinemachineCamera : Behaviour
    {
        internal CinemachineCamera() { }
        internal static ulong IdOf(GameObject go) => GetNativeId(go);
        internal static GameObject GameObjectOf(ulong id) => FromNativeId(id);

        public PrioritySettings Priority
        {
            get => CmNative.NovaCM_GetInt(nativeId, CmNative.Camera, 0);
            set => CmNative.NovaCM_SetInt(nativeId, CmNative.Camera, 0, value.Value);
        }
        /// <summary>렌즈 — 바로 쓰인다 (cam.Lens.FieldOfView = 40). 다른 LensSettings 를 넣으면 값을 복사</summary>
        public LensSettings Lens { get => new LensSettings(nativeId); set { if (value != null) value.CopyTo(new LensSettings(nativeId)); } }
        public CameraTarget Target => new CameraTarget(nativeId);
        /// <summary>Tracking Target</summary>
        public Transform Follow { get => Target.TrackingTarget; set => Target.TrackingTarget = value; }
        /// <summary>Look At (Custom Look At 이 아니면 Tracking Target)</summary>
        public Transform LookAt
        {
            get => Target.CustomLookAtTarget ? Target.LookAtTarget : Target.TrackingTarget;
            set { Target.CustomLookAtTarget = true; Target.LookAtTarget = value; }
        }
        /// <summary>같은 Priority 중 맨 앞으로 (가장 늦게 켜진 것처럼)</summary>
        public void Prioritize() => CmNative.Call(nativeId, CmNative.Camera, 0);
        public bool IsLive => CmNative.Call(nativeId, CmNative.Camera, 2) != 0f;
        /// <summary>false 로 두면 다음 프레임은 따라가기 (damping) 없이 바로 제자리</summary>
        public bool PreviousStateIsValid { get => true; set { if (!value) CmNative.Call(nativeId, CmNative.Camera, 1); } }
        /// <summary>대상이 순간 이동했다 — 따라가지 말고 바로</summary>
        public void OnTargetObjectWarped(Transform target, Vector3 positionDelta) => PreviousStateIsValid = false;
    }

    public static class CinemachineCore
    {
        public static bool IsLive(CinemachineCamera vcam) => vcam != null && vcam.IsLive;
    }

    // ------------------------------------------------------------------ Position Control
    [NativeComponent("CinemachineFollow")]
    public sealed class CinemachineFollow : Behaviour
    {
        internal CinemachineFollow() { }
        public Vector3 FollowOffset { get => CmNative.GetVec(nativeId, CmNative.Follow, 0); set => CmNative.SetVec(nativeId, CmNative.Follow, 0, value); }
        public TrackerSettings TrackerSettings => new TrackerSettings(nativeId, CmNative.Follow, 0, 1, 1, 2, 0);
    }

    [NativeComponent("CinemachineOrbitalFollow")]
    public sealed class CinemachineOrbitalFollow : Behaviour
    {
        public enum OrbitStyles { Sphere, ThreeRing }
        internal CinemachineOrbitalFollow() { }
        public OrbitStyles OrbitStyle { get => (OrbitStyles)CmNative.NovaCM_GetInt(nativeId, CmNative.Orbital, 0); set => CmNative.NovaCM_SetInt(nativeId, CmNative.Orbital, 0, (int)value); }
        public float Radius { get => CmNative.NovaCM_GetFloat(nativeId, CmNative.Orbital, 0); set => CmNative.NovaCM_SetFloat(nativeId, CmNative.Orbital, 0, value); }
        public Vector3 TargetOffset { get => CmNative.GetVec(nativeId, CmNative.Orbital, 0); set => CmNative.SetVec(nativeId, CmNative.Orbital, 0, value); }
        public TrackerSettings TrackerSettings => new TrackerSettings(nativeId, CmNative.Orbital, 1, 2, 1, 2, 1);
        public InputAxis HorizontalAxis => new InputAxis(nativeId, 2, 3, 4, 11, 0);
        public InputAxis VerticalAxis => new InputAxis(nativeId, 5, 6, 7, 12, 1);
        public InputAxis RadialAxis => new InputAxis(nativeId, 8, 9, 10, 13, 2);
        public float SplineCurvature { get => CmNative.NovaCM_GetFloat(nativeId, CmNative.Orbital, 14); set => CmNative.NovaCM_SetFloat(nativeId, CmNative.Orbital, 14, value); }
        /// <summary>Three Ring 의 고리 (높이, 반지름)</summary>
        public Vector2 TopOrbit { get => G(15); set => S(15, value); }
        public Vector2 CenterOrbit { get => G(17); set => S(17, value); }
        public Vector2 BottomOrbit { get => G(19); set => S(19, value); }
        Vector2 G(int p) => new Vector2(CmNative.NovaCM_GetFloat(nativeId, CmNative.Orbital, p), CmNative.NovaCM_GetFloat(nativeId, CmNative.Orbital, p + 1));
        void S(int p, Vector2 v) { CmNative.NovaCM_SetFloat(nativeId, CmNative.Orbital, p, v.x); CmNative.NovaCM_SetFloat(nativeId, CmNative.Orbital, p + 1, v.y); }
    }

    [NativeComponent("CinemachineThirdPersonFollow")]
    public sealed class CinemachineThirdPersonFollow : Behaviour
    {
        public sealed class ObstacleSettings
        {
            readonly ulong m_Go;
            internal ObstacleSettings(ulong go) { m_Go = go; }
            const int K = CmNative.ThirdPerson;
            public bool Enabled { get => CmNative.GetBool(m_Go, K, 0); set => CmNative.SetBool(m_Go, K, 0, value); }
            public int CollisionFilter { get => CmNative.NovaCM_GetInt(m_Go, K, 0); set => CmNative.NovaCM_SetInt(m_Go, K, 0, value); }
            public float CameraRadius { get => CmNative.NovaCM_GetFloat(m_Go, K, 3); set => CmNative.NovaCM_SetFloat(m_Go, K, 3, value); }
            public float DampingIntoCollision { get => CmNative.NovaCM_GetFloat(m_Go, K, 4); set => CmNative.NovaCM_SetFloat(m_Go, K, 4, value); }
            public float DampingFromCollision { get => CmNative.NovaCM_GetFloat(m_Go, K, 5); set => CmNative.NovaCM_SetFloat(m_Go, K, 5, value); }
        }
        internal CinemachineThirdPersonFollow() { }
        const int K = CmNative.ThirdPerson;
        public Vector3 Damping { get => CmNative.GetVec(nativeId, K, 0); set => CmNative.SetVec(nativeId, K, 0, value); }
        public Vector3 ShoulderOffset { get => CmNative.GetVec(nativeId, K, 1); set => CmNative.SetVec(nativeId, K, 1, value); }
        public float VerticalArmLength { get => CmNative.NovaCM_GetFloat(nativeId, K, 0); set => CmNative.NovaCM_SetFloat(nativeId, K, 0, value); }
        /// <summary>0 = 왼쪽 어깨, 1 = 오른쪽</summary>
        public float CameraSide { get => CmNative.NovaCM_GetFloat(nativeId, K, 1); set => CmNative.NovaCM_SetFloat(nativeId, K, 1, value); }
        public float CameraDistance { get => CmNative.NovaCM_GetFloat(nativeId, K, 2); set => CmNative.NovaCM_SetFloat(nativeId, K, 2, value); }
        public ObstacleSettings AvoidObstacles => new ObstacleSettings(nativeId);
    }

    // ------------------------------------------------------------------ Rotation Control
    [NativeComponent("CinemachineRotationComposer")]
    public sealed class CinemachineRotationComposer : Behaviour
    {
        /// <summary>화면 구도 (Unity ScreenComposerSettings): 가운데 0, 가장자리 ±0.5, 크기는 화면 = 1</summary>
        public sealed class ScreenComposerSettings
        {
            readonly ulong m_Go;
            internal ScreenComposerSettings(ulong go) { m_Go = go; }
            const int K = CmNative.Composer;
            public Vector2 ScreenPosition { get => CmNative.GetVec2(m_Go, K, 2); set => CmNative.SetVec2(m_Go, K, 2, value); }
            public DeadZoneSettings DeadZone => new DeadZoneSettings(m_Go);
            public HardLimitSettings HardLimits => new HardLimitSettings(m_Go);
        }
        public sealed class DeadZoneSettings
        {
            readonly ulong m_Go;
            internal DeadZoneSettings(ulong go) { m_Go = go; }
            public bool Enabled { get => CmNative.GetBool(m_Go, CmNative.Composer, 0); set => CmNative.SetBool(m_Go, CmNative.Composer, 0, value); }
            public Vector2 Size { get => CmNative.GetVec2(m_Go, CmNative.Composer, 3); set => CmNative.SetVec2(m_Go, CmNative.Composer, 3, value); }
        }
        public sealed class HardLimitSettings
        {
            readonly ulong m_Go;
            internal HardLimitSettings(ulong go) { m_Go = go; }
            public bool Enabled { get => CmNative.GetBool(m_Go, CmNative.Composer, 1); set => CmNative.SetBool(m_Go, CmNative.Composer, 1, value); }
            public Vector2 Size { get => CmNative.GetVec2(m_Go, CmNative.Composer, 4); set => CmNative.SetVec2(m_Go, CmNative.Composer, 4, value); }
            public Vector2 Offset { get => CmNative.GetVec2(m_Go, CmNative.Composer, 5); set => CmNative.SetVec2(m_Go, CmNative.Composer, 5, value); }
        }
        internal CinemachineRotationComposer() { }
        public Vector3 TargetOffset { get => CmNative.GetVec(nativeId, CmNative.Composer, 0); set => CmNative.SetVec(nativeId, CmNative.Composer, 0, value); }
        public Vector2 Damping { get => CmNative.GetVec2(nativeId, CmNative.Composer, 1); set => CmNative.SetVec2(nativeId, CmNative.Composer, 1, value); }
        public ScreenComposerSettings Composition => new ScreenComposerSettings(nativeId);
        public bool CenterOnActivate { get => CmNative.GetBool(nativeId, CmNative.Composer, 2); set => CmNative.SetBool(nativeId, CmNative.Composer, 2, value); }
    }

    [NativeComponent("CinemachineHardLookAt")]
    public sealed class CinemachineHardLookAt : Behaviour
    {
        internal CinemachineHardLookAt() { }
        public Vector3 LookAtOffset { get => CmNative.GetVec(nativeId, CmNative.HardLookAt, 0); set => CmNative.SetVec(nativeId, CmNative.HardLookAt, 0, value); }
    }

    [NativeComponent("CinemachineRotateWithFollowTarget")]
    public sealed class CinemachineRotateWithFollowTarget : Behaviour
    {
        internal CinemachineRotateWithFollowTarget() { }
        public float Damping { get => CmNative.NovaCM_GetFloat(nativeId, CmNative.RotateWithTarget, 0); set => CmNative.NovaCM_SetFloat(nativeId, CmNative.RotateWithTarget, 0, value); }
    }

    // ------------------------------------------------------------------ 흔들림 · 충격
    /// <summary>미리 만든 흔들림 프로필 (Unity 의 Noise Settings 에셋 이름)</summary>
    public enum NoiseProfiles { None, SixDShake, Handheld_normal_mild, Handheld_normal_strong, Handheld_normal_extreme, Handheld_tele_mild, Handheld_tele_strong, Handheld_wideangle_mild, Handheld_wideangle_strong }

    [NativeComponent("CinemachineBasicMultiChannelPerlin")]
    public sealed class CinemachineBasicMultiChannelPerlin : Behaviour
    {
        internal CinemachineBasicMultiChannelPerlin() { }
        const int K = CmNative.Perlin;
        public NoiseProfiles NoiseProfile { get => (NoiseProfiles)CmNative.NovaCM_GetInt(nativeId, K, 0); set => CmNative.NovaCM_SetInt(nativeId, K, 0, (int)value); }
        public Vector3 PivotOffset { get => CmNative.GetVec(nativeId, K, 0); set => CmNative.SetVec(nativeId, K, 0, value); }
        public float AmplitudeGain { get => CmNative.NovaCM_GetFloat(nativeId, K, 0); set => CmNative.NovaCM_SetFloat(nativeId, K, 0, value); }
        public float FrequencyGain { get => CmNative.NovaCM_GetFloat(nativeId, K, 1); set => CmNative.NovaCM_SetFloat(nativeId, K, 1, value); }
        public void ReSeed() => CmNative.Call(nativeId, K, 0);
    }

    /// <summary>충격 정의 (Unity CinemachineImpulseDefinition)</summary>
    public sealed class CinemachineImpulseDefinition
    {
        public enum ImpulseShapes { Recoil, Bump, Explosion, Rumble }
        public enum ImpulseTypes { Uniform, Dissipating, Propagating }
        readonly ulong m_Go;
        internal CinemachineImpulseDefinition(ulong go) { m_Go = go; }
        const int K = CmNative.ImpulseSource;
        public int ImpulseChannel { get => CmNative.NovaCM_GetInt(m_Go, K, 0); set => CmNative.NovaCM_SetInt(m_Go, K, 0, value); }
        public ImpulseShapes ImpulseShape { get => (ImpulseShapes)CmNative.NovaCM_GetInt(m_Go, K, 1); set => CmNative.NovaCM_SetInt(m_Go, K, 1, (int)value); }
        public ImpulseTypes ImpulseType { get => (ImpulseTypes)CmNative.NovaCM_GetInt(m_Go, K, 2); set => CmNative.NovaCM_SetInt(m_Go, K, 2, (int)value); }
        public float ImpulseDuration { get => CmNative.NovaCM_GetFloat(m_Go, K, 0); set => CmNative.NovaCM_SetFloat(m_Go, K, 0, value); }
        public float DissipationRate { get => CmNative.NovaCM_GetFloat(m_Go, K, 1); set => CmNative.NovaCM_SetFloat(m_Go, K, 1, value); }
        public float DissipationDistance { get => CmNative.NovaCM_GetFloat(m_Go, K, 2); set => CmNative.NovaCM_SetFloat(m_Go, K, 2, value); }
        public float PropagationSpeed { get => CmNative.NovaCM_GetFloat(m_Go, K, 3); set => CmNative.NovaCM_SetFloat(m_Go, K, 3, value); }
    }

    [NativeComponent("CinemachineImpulseSource")]
    public sealed class CinemachineImpulseSource : Behaviour
    {
        internal CinemachineImpulseSource() { }
        const int K = CmNative.ImpulseSource;
        public CinemachineImpulseDefinition ImpulseDefinition => new CinemachineImpulseDefinition(nativeId);
        public Vector3 DefaultVelocity { get => CmNative.GetVec(nativeId, K, 0); set => CmNative.SetVec(nativeId, K, 0, value); }

        public void GenerateImpulseAtPositionWithVelocity(Vector3 position, Vector3 velocity) => CmNative.Call(nativeId, K, 0, position, velocity, 0f);
        public void GenerateImpulseWithVelocity(Vector3 velocity) => CmNative.Call(nativeId, K, 1, Vector3.zero, velocity, 0f);
        public void GenerateImpulseWithForce(float force) => CmNative.Call(nativeId, K, 2, Vector3.zero, Vector3.zero, force);
        public void GenerateImpulse() => GenerateImpulseWithForce(1f);
        public void GenerateImpulse(float force) => GenerateImpulseWithForce(force);
        public void GenerateImpulse(Vector3 velocity) => GenerateImpulseWithVelocity(velocity);
        public void GenerateImpulseAt(Vector3 position, Vector3 velocity) => GenerateImpulseAtPositionWithVelocity(position, velocity);
    }

    [NativeComponent("CinemachineImpulseListener")]
    public sealed class CinemachineImpulseListener : Behaviour
    {
        internal CinemachineImpulseListener() { }
        const int K = CmNative.ImpulseListener;
        public int ChannelMask { get => CmNative.NovaCM_GetInt(nativeId, K, 0); set => CmNative.NovaCM_SetInt(nativeId, K, 0, value); }
        public float Gain { get => CmNative.NovaCM_GetFloat(nativeId, K, 0); set => CmNative.NovaCM_SetFloat(nativeId, K, 0, value); }
        public bool Use2DDistance { get => CmNative.GetBool(nativeId, K, 0); set => CmNative.SetBool(nativeId, K, 0, value); }
        public bool UseCameraSpace { get => CmNative.GetBool(nativeId, K, 1); set => CmNative.SetBool(nativeId, K, 1, value); }
    }

    // ------------------------------------------------------------------ 입력
    /// <summary>
    /// 마우스로 Orbital Follow 의 축을 돌린다 (Unity CinemachineInputAxisController 를 단순하게). 가상 카메라에 붙인다.
    /// Mouse X → 가로 축 (도), Mouse Y → 세로 축, 휠 → 거리 축. Mouse Button 이 0 이상이면 그 버튼을 누른 동안만.
    /// </summary>
    public class CinemachineInputAxisController : MonoBehaviour
    {
        public float HorizontalGain = 3f;
        public float VerticalGain = 1.5f;
        public bool InvertY;
        public float RadialGain;
        /// <summary>-1 = 늘, 0 왼쪽 · 1 오른쪽 · 2 가운데 버튼을 누른 동안만</summary>
        public int MouseButton = -1;

        CinemachineOrbitalFollow m_Orbital;

        void Update()
        {
            if (m_Orbital == null) m_Orbital = GetComponent<CinemachineOrbitalFollow>();
            if (m_Orbital == null) return;
            if (MouseButton >= 0 && !Input.GetMouseButton(MouseButton)) return;
            float x = Input.GetAxis("Mouse X"), y = Input.GetAxis("Mouse Y");
            if (x != 0f) m_Orbital.HorizontalAxis.Value += x * HorizontalGain;
            if (y != 0f) m_Orbital.VerticalAxis.Value += (InvertY ? y : -y) * VerticalGain;
            if (RadialGain != 0f)
            {
                float w = Input.GetAxis("Mouse ScrollWheel");
                if (w != 0f) m_Orbital.RadialAxis.Value -= w * RadialGain;
            }
        }
    }
}
