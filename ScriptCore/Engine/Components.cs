using System;
using NovaEngine.Interop;

namespace NovaEngine
{
    public enum ForceMode { Force = 0, Acceleration = 5, Impulse = 1, VelocityChange = 2 }

    // ------------------------------------------------------------------ Rigidbody
    public sealed class Rigidbody : Component
    {
        internal Rigidbody() { }
        unsafe Vector3 GetV(int p) { Vector3 v; Native.Api.RB_GetVector(m_Id, p, &v); return v; }
        unsafe void SetV(int p, Vector3 v) => Native.Api.RB_SetVector(m_Id, p, &v);

        public Vector3 velocity { get => GetV(0); set => SetV(0, value); }
        public Vector3 linearVelocity { get => GetV(0); set => SetV(0, value); }
        public Vector3 angularVelocity { get => GetV(1); set => SetV(1, value); }
        public Vector3 worldCenterOfMass => GetV(2);
        public Vector3 position { get => transform.position; set => transform.position = value; }
        public Quaternion rotation { get => transform.rotation; set => transform.rotation = value; }
        public unsafe float mass { get => Native.Api.RB_GetFloat(m_Id, 0); set => Native.Api.RB_SetFloat(m_Id, 0, value); }
        public unsafe float drag { get => Native.Api.RB_GetFloat(m_Id, 1); set => Native.Api.RB_SetFloat(m_Id, 1, value); }
        public float linearDamping { get => drag; set => drag = value; }
        public unsafe float angularDrag { get => Native.Api.RB_GetFloat(m_Id, 2); set => Native.Api.RB_SetFloat(m_Id, 2, value); }
        public float angularDamping { get => angularDrag; set => angularDrag = value; }
        public unsafe bool useGravity { get => Native.Api.RB_GetBool(m_Id, 0) != 0; set => Native.Api.RB_SetBool(m_Id, 0, value ? 1 : 0); }
        public unsafe bool isKinematic { get => Native.Api.RB_GetBool(m_Id, 1) != 0; set => Native.Api.RB_SetBool(m_Id, 1, value ? 1 : 0); }
        public unsafe bool IsSleeping() => Native.Api.RB_GetBool(m_Id, 2) != 0;

        static int Mode(ForceMode m) => m switch { ForceMode.Force => 0, ForceMode.Impulse => 1, ForceMode.VelocityChange => 2, _ => 3 };
        public unsafe void AddForce(Vector3 force, ForceMode mode = ForceMode.Force) => Native.Api.RB_AddForce(m_Id, 0, &force, Mode(mode));
        public void AddForce(float x, float y, float z, ForceMode mode = ForceMode.Force) => AddForce(new Vector3(x, y, z), mode);
        public void AddRelativeForce(Vector3 force, ForceMode mode = ForceMode.Force) => AddForce(transform.rotation * force, mode);
        public unsafe void AddTorque(Vector3 torque, ForceMode mode = ForceMode.Force) => Native.Api.RB_AddForce(m_Id, 1, &torque, Mode(mode));
        public void AddRelativeTorque(Vector3 torque, ForceMode mode = ForceMode.Force) => AddTorque(transform.rotation * torque, mode);
        public unsafe void MovePosition(Vector3 p) { Vector4 v = p; Native.Api.RB_Move(m_Id, 0, &v); }
        public unsafe void MoveRotation(Quaternion q) { Vector4 v = new Vector4(q.x, q.y, q.z, q.w); Native.Api.RB_Move(m_Id, 1, &v); }
        public void Sleep() { }
        public void WakeUp() { }
    }

    // ------------------------------------------------------------------ Audio
    public sealed class AudioClip : Object
    {
        internal string m_Path;
        internal AudioClip(string path) { m_Path = path; }
        public override string name { get => System.IO.Path.GetFileNameWithoutExtension(m_Path ?? ""); set { } }
        public unsafe float length { get { fixed (byte* p = Native.Utf8(m_Path)) return Native.Api.Audio_ClipLength(p); } }
        internal override bool IsAlive() => !string.IsNullOrEmpty(m_Path);
        public override bool Equals(object o) => o is AudioClip c && c.m_Path == m_Path;
        public override int GetHashCode() => (m_Path ?? "").GetHashCode();
        public override string ToString() => $"{name} (AudioClip)";
    }

    public sealed class AudioSource : Behaviour
    {
        internal AudioSource() { }
        public unsafe void Play() => Native.Api.AS_Call(m_Id, 0);
        public unsafe void Stop() => Native.Api.AS_Call(m_Id, 1);
        public unsafe void Pause() => Native.Api.AS_Call(m_Id, 2);
        public unsafe void UnPause() => Native.Api.AS_Call(m_Id, 3);
        public unsafe void PlayOneShot(AudioClip clip, float volumeScale = 1f)
        {
            if (clip is null) return;
            fixed (byte* p = Native.Utf8(clip.m_Path)) Native.Api.AS_PlayOneShot(m_Id, p, volumeScale);
        }
        public unsafe bool isPlaying => Native.Api.AS_GetBool(m_Id, 0) != 0;
        public unsafe bool loop { get => Native.Api.AS_GetBool(m_Id, 1) != 0; set => Native.Api.AS_SetBool(m_Id, 1, value ? 1 : 0); }
        public unsafe bool mute { get => Native.Api.AS_GetBool(m_Id, 2) != 0; set => Native.Api.AS_SetBool(m_Id, 2, value ? 1 : 0); }
        public unsafe bool playOnAwake { get => Native.Api.AS_GetBool(m_Id, 3) != 0; set => Native.Api.AS_SetBool(m_Id, 3, value ? 1 : 0); }
        public unsafe float volume { get => Native.Api.AS_GetFloat(m_Id, 0); set => Native.Api.AS_SetFloat(m_Id, 0, value); }
        public unsafe float pitch { get => Native.Api.AS_GetFloat(m_Id, 1); set => Native.Api.AS_SetFloat(m_Id, 1, value); }
        public unsafe float panStereo { get => Native.Api.AS_GetFloat(m_Id, 2); set => Native.Api.AS_SetFloat(m_Id, 2, value); }
        public unsafe float spatialBlend { get => Native.Api.AS_GetFloat(m_Id, 3); set => Native.Api.AS_SetFloat(m_Id, 3, value); }
        public unsafe float time => Native.Api.AS_GetFloat(m_Id, 4);
        public unsafe float dopplerLevel { get => Native.Api.AS_GetFloat(m_Id, 5); set => Native.Api.AS_SetFloat(m_Id, 5, value); }
        public unsafe float spread { get => Native.Api.AS_GetFloat(m_Id, 6); set => Native.Api.AS_SetFloat(m_Id, 6, value); }
        public unsafe float minDistance { get => Native.Api.AS_GetFloat(m_Id, 7); set => Native.Api.AS_SetFloat(m_Id, 7, value); }
        public unsafe float maxDistance { get => Native.Api.AS_GetFloat(m_Id, 8); set => Native.Api.AS_SetFloat(m_Id, 8, value); }
        /// <summary>Audio Mixer 그룹으로 보낸다 (null = 마스터로 바로)</summary>
        public unsafe NovaEngine.Audio.AudioMixerGroup outputAudioMixerGroup
        {
            get
            {
                string s = Native.Str(Native.Api.AS_GetOutput(m_Id));
                if (string.IsNullOrEmpty(s)) return null;
                int bar = s.IndexOf('|');
                return new NovaEngine.Audio.AudioMixerGroup(new NovaEngine.Audio.AudioMixer(s.Substring(0, bar)), s.Substring(bar + 1));
            }
            set
            {
                fixed (byte* m = Native.Utf8(value?.audioMixer.m_Path))
                fixed (byte* g = Native.Utf8(value?.name))
                    Native.Api.AS_SetOutput(m_Id, m, g);
            }
        }
        public unsafe AudioClip clip
        {
            get { string p = Native.Str(Native.Api.AS_GetClip(m_Id)); return string.IsNullOrEmpty(p) ? null : new AudioClip(p); }
            set { fixed (byte* p = Native.Utf8(value?.m_Path)) Native.Api.AS_SetClip(m_Id, p); }
        }
        public static unsafe void PlayClipAtPoint(AudioClip clip, Vector3 position, float volume = 1f)
        {
            // 임시 오브젝트 없이 바로 재생 (위치 감쇠는 생략)
            if (clip is null) return;
            fixed (byte* p = Native.Utf8(clip.m_Path)) Native.Api.AS_PlayOneShot(0, p, volume);
        }
    }

    public sealed class AudioListener : Behaviour { internal AudioListener() { } }

    // ------------------------------------------------------------------ Physics
    public class Collider : Component
    {
        internal Collider() { }
        internal Collider(ulong id) { m_Id = id; }
        public Rigidbody attachedRigidbody => GetComponent<Rigidbody>();
        public bool enabled { get => true; set { } }
    }
    public sealed class BoxCollider : Collider { internal BoxCollider() { } }
    public sealed class SphereCollider : Collider { internal SphereCollider() { } }
    public sealed class CapsuleCollider : Collider { internal CapsuleCollider() { } }

    [Flags]
    public enum CollisionFlags { None = 0, Sides = 1, Above = 2, Below = 4, CollidedSides = 1, CollidedAbove = 2, CollidedBelow = 4 }

    // Unity 의 CharacterController: Move(이동량) 로 벽을 따라 미끄러지고 계단을 오른다. 중력은 직접 더한다 (SimpleMove 는 포함)
    public sealed class CharacterController : Collider
    {
        internal CharacterController() { }
        unsafe float GetF(int p) => Native.Api.CC_GetFloat(m_Id, p);
        unsafe void SetF(int p, float v) => Native.Api.CC_SetFloat(m_Id, p, v);
        unsafe Vector3 GetV(int p) { Vector3 v; Native.Api.CC_GetVector(m_Id, p, &v); return v; }

        public unsafe CollisionFlags Move(Vector3 motion) => (CollisionFlags)Native.Api.CC_Move(m_Id, &motion, Time.deltaTime, 0);
        public unsafe bool SimpleMove(Vector3 speed) => Native.Api.CC_Move(m_Id, &speed, Time.deltaTime, 1) != 0;
        public unsafe bool isGrounded => Native.Api.CC_GetInt(m_Id, 1) != 0;
        public unsafe CollisionFlags collisionFlags => (CollisionFlags)Native.Api.CC_GetInt(m_Id, 0);
        public Vector3 velocity => GetV(0);
        public unsafe Vector3 center { get => GetV(1); set => Native.Api.CC_SetVector(m_Id, 1, &value); }
        public float slopeLimit { get => GetF(0); set => SetF(0, value); }
        public float stepOffset { get => GetF(1); set => SetF(1, value); }
        public float skinWidth { get => GetF(2); set => SetF(2, value); }
        public float minMoveDistance { get => GetF(3); set => SetF(3, value); }
        public float radius { get => GetF(4); set => SetF(4, value); }
        public float height { get => GetF(5); set => SetF(5, value); }
        public unsafe bool detectCollisions { get => Native.Api.CC_GetInt(m_Id, 2) != 0; set => Native.Api.CC_SetInt(m_Id, 2, value ? 1 : 0); }
        public bool enableOverlapRecovery { get; set; } = true;
    }

    // ------------------------------------------------------------------ Joints (Unity: Joint, FixedJoint, HingeJoint, SpringJoint)
    public struct JointSpring { public float spring, damper, targetPosition; }
    public struct JointMotor { public float targetVelocity, force; public bool freeSpin; }
    public struct JointLimits { public float min, max, bounciness, bounceMinVelocity, contactDistance; }

    public class Joint : Component
    {
        internal Joint() { }
        internal virtual int Kind => 0;
        internal unsafe float F(int p) => Native.Api.JT_GetFloat(m_Id, Kind, p);
        internal unsafe void SetF(int p, float v) => Native.Api.JT_SetFloat(m_Id, Kind, p, v);
        unsafe Vector3 V(int p) { Vector3 v; Native.Api.JT_GetVector(m_Id, Kind, p, &v); return v; }
        unsafe void SetV(int p, Vector3 v) => Native.Api.JT_SetVector(m_Id, Kind, p, &v);

        /// <summary>이은 Rigidbody (null = 월드에 고정)</summary>
        public unsafe Rigidbody connectedBody
        {
            get { ulong id = Native.Api.JT_GetConnected(m_Id, Kind); return id == 0 ? null : new GameObject(id).GetComponent<Rigidbody>(); }
            set => Native.Api.JT_SetConnected(m_Id, Kind, value == null ? 0 : value.m_Id);
        }
        public Vector3 anchor { get => V(0); set => SetV(0, value); }
        public Vector3 axis { get => V(1); set => SetV(1, value); }
        public Vector3 connectedAnchor { get => V(2); set => SetV(2, value); }
        public bool autoConfigureConnectedAnchor { get => F(3) != 0; set => SetF(3, value ? 1 : 0); }
        public float breakForce { get => F(0); set => SetF(0, value); }
        public float breakTorque { get => F(1); set => SetF(1, value); }
        public bool enableCollision { get => F(2) != 0; set => SetF(2, value ? 1 : 0); }
    }

    public sealed class FixedJoint : Joint { internal FixedJoint() { } internal override int Kind => 0; }

    public sealed class HingeJoint : Joint
    {
        internal HingeJoint() { }
        internal override int Kind => 1;
        public bool useSpring { get => F(10) != 0; set => SetF(10, value ? 1 : 0); }
        public JointSpring spring
        {
            get => new JointSpring { spring = F(11), damper = F(12), targetPosition = F(13) };
            set { SetF(11, value.spring); SetF(12, value.damper); SetF(13, value.targetPosition); }
        }
        public bool useMotor { get => F(14) != 0; set => SetF(14, value ? 1 : 0); }
        public JointMotor motor
        {
            get => new JointMotor { targetVelocity = F(15), force = F(16), freeSpin = F(17) != 0 };
            set { SetF(15, value.targetVelocity); SetF(16, value.force); SetF(17, value.freeSpin ? 1 : 0); }
        }
        public bool useLimits { get => F(18) != 0; set => SetF(18, value ? 1 : 0); }
        public JointLimits limits
        {
            get => new JointLimits { min = F(19), max = F(20), bounciness = F(21) };
            set { SetF(19, value.min); SetF(20, value.max); SetF(21, value.bounciness); }
        }
        /// <summary>현재 각도 (도, Play 중)</summary>
        public float angle => F(30);
        /// <summary>현재 각속도 (도/초, Play 중)</summary>
        public float velocity => F(31);
    }

    public sealed class SpringJoint : Joint
    {
        internal SpringJoint() { }
        internal override int Kind => 2;
        public float spring { get => F(40); set => SetF(40, value); }
        public float damper { get => F(41); set => SetF(41, value); }
        public float minDistance { get => F(42); set => SetF(42, value); }
        public float maxDistance { get => F(43); set => SetF(43, value); }
    }

    // OnControllerColliderHit(ControllerColliderHit hit) 로 받는 충돌 정보
    public class ControllerColliderHit
    {
        internal unsafe ControllerColliderHit(ulong self, int index)
        {
            m_Self = self;
            ControllerHitData d;
            if (Native.Api.CC_GetHit(self, index, &d) != 0)
            {
                point = d.point; normal = d.normal; moveDirection = d.moveDirection; moveLength = d.moveLength; m_Other = d.gameObject;
            }
        }
        readonly ulong m_Self, m_Other;
        public CharacterController controller => new GameObject(m_Self).GetComponent<CharacterController>();
        public Collider collider => m_Other == 0 ? null : new Collider(m_Other);
        public GameObject gameObject => m_Other == 0 ? null : new GameObject(m_Other);
        public Transform transform => gameObject?.transform;
        public Rigidbody rigidbody => gameObject?.GetComponent<Rigidbody>();
        public Vector3 point { get; }
        public Vector3 normal { get; }
        public Vector3 moveDirection { get; }
        public float moveLength { get; }
    }

    // Unity 의 Collision: 부딪힌 상대
    public class Collision
    {
        internal Collision(ulong other) { gameObject = new GameObject(other); collider = new Collider(other); }
        public GameObject gameObject { get; }
        public Collider collider { get; }
        public Transform transform => gameObject.transform;
        public Rigidbody rigidbody => gameObject.GetComponent<Rigidbody>();
        public Vector3 relativeVelocity => Vector3.zero;
    }

    public struct RaycastHit
    {
        public Vector3 point;
        public Vector3 normal;
        public float distance;
        internal ulong m_GameObject;
        public Collider collider => m_GameObject == 0 ? null : new Collider(m_GameObject);
        public Transform transform => m_GameObject == 0 ? null : new Transform(m_GameObject);
        public Rigidbody rigidbody => m_GameObject == 0 ? null : new GameObject(m_GameObject).GetComponent<Rigidbody>();
    }

    public struct Ray
    {
        public Vector3 origin, direction;
        public Ray(Vector3 origin, Vector3 direction) { this.origin = origin; this.direction = direction.normalized; }
        public Vector3 GetPoint(float distance) => origin + direction * distance;
    }

    public static class Physics
    {
        /// <summary>Ignore Raycast (레이어 2) 를 뺀 모두 — Raycast 의 기본 layerMask (Unity 와 같음)</summary>
        public const int DefaultRaycastLayers = ~(1 << 2);
        public const int AllLayers = ~0;
        public const int IgnoreRaycastLayer = 1 << 2;

        /// <summary>중력 (Project Settings > Physics 의 값으로 시작, Play 중 바꾸면 그 Play 동안만)</summary>
        public static unsafe Vector3 gravity
        {
            get { Vector3 g; Native.Api.PH_GetGravity(&g); return g; }
            set { Native.Api.PH_SetGravity(&value); }
        }

        public static unsafe bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance, int layerMask, QueryTriggerInteraction queryTriggerInteraction = QueryTriggerInteraction.UseGlobal)
        {
            RaycastData d;
            if (float.IsInfinity(maxDistance)) maxDistance = 100000f;
            int triggers = queryTriggerInteraction == QueryTriggerInteraction.Collide ? 1 : 0;
            int r = Native.Api.PH_RaycastMask(&origin, &direction, maxDistance, layerMask, triggers, &d);
            hitInfo = new RaycastHit { point = d.point, normal = d.normal, distance = d.distance, m_GameObject = d.gameObject };
            return r != 0;
        }
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance = float.PositiveInfinity) => Raycast(origin, direction, out hitInfo, maxDistance, DefaultRaycastLayers);
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers) => Raycast(origin, direction, out _, maxDistance, layerMask);
        public static bool Raycast(Ray ray, out RaycastHit hitInfo, float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers) => Raycast(ray.origin, ray.direction, out hitInfo, maxDistance, layerMask);
        public static bool Raycast(Ray ray, float maxDistance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers) => Raycast(ray.origin, ray.direction, out _, maxDistance, layerMask);

        /// <summary>두 레이어가 부딪히지 않게 (Layer Collision Matrix 를 실행 중에만 바꾼다 — Play 를 멈추면 설정 값으로)</summary>
        public static unsafe void IgnoreLayerCollision(int layer1, int layer2, bool ignore = true) => Native.Api.PH_IgnoreLayer(layer1, layer2, ignore ? 1 : 0);
        public static unsafe bool GetIgnoreLayerCollision(int layer1, int layer2) => Native.Api.PH_GetIgnoreLayer(layer1, layer2) != 0;
    }

    public enum QueryTriggerInteraction { UseGlobal = 0, Ignore = 1, Collide = 2 }

    /// <summary>Unity 의 LayerMask: 비트 i = 레이어 i. int 와 서로 바뀐다</summary>
    public struct LayerMask
    {
        private int m_Mask;
        public int value { get => m_Mask; set => m_Mask = value; }
        public static implicit operator int(LayerMask mask) => mask.m_Mask;
        public static implicit operator LayerMask(int mask) => new LayerMask { m_Mask = mask };

        public static unsafe int NameToLayer(string layerName)
        {
            fixed (byte* p = Native.Utf8(layerName)) return Native.Api.LM_NameToLayer(p);
        }
        public static unsafe string LayerToName(int layer) => Native.Str(Native.Api.LM_LayerToName(layer)) ?? "";
        /// <summary>이름들의 마스크 (없는 이름은 무시)</summary>
        public static int GetMask(params string[] layerNames)
        {
            int mask = 0;
            if (layerNames != null)
                foreach (string n in layerNames)
                {
                    int l = NameToLayer(n);
                    if (l >= 0) mask |= 1 << l;
                }
            return mask;
        }
        public override string ToString() => m_Mask.ToString();
    }

    // ------------------------------------------------------------------ Camera / Light / Renderer
    public sealed class Camera : Behaviour
    {
        internal Camera() { }
        public static unsafe Camera main
        {
            get { ulong id = Native.Api.Camera_Main(); return id == 0 ? null : new Camera { m_Id = id }; }
        }
        /// <summary>이 카메라가 그리는 레이어 (비트 = 레이어, LayerMask.GetMask)</summary>
        public unsafe int cullingMask { get => Native.Api.CL_GetMask(m_Id, 0); set => Native.Api.CL_SetMask(m_Id, 0, value); }
    }
    public sealed unsafe class Light : Behaviour
    {
        internal Light() { }
        /// <summary>이 빛이 비추는 레이어 (비트 = 레이어). 안 비추는 레이어는 이 빛의 그림자도 드리우지 않는다</summary>
        public int cullingMask { get => Native.Api.CL_GetMask(m_Id, 1); set => Native.Api.CL_SetMask(m_Id, 1, value); }
    }
    public sealed class MeshRenderer : Component { internal MeshRenderer() { } }

    /// <summary>Unity 의 SkinnedMeshRenderer: BlendShape (모프 타깃) 가중치 0..100</summary>
    public sealed class SkinnedMeshRenderer : Component
    {
        internal SkinnedMeshRenderer() { }
        public unsafe float GetBlendShapeWeight(int index) => Native.Api.SMR_GetWeight(m_Id, index);
        public unsafe void SetBlendShapeWeight(int index, float value) => Native.Api.SMR_SetWeight(m_Id, index, value);
        /// <summary>이 렌더러의 메시 (BlendShape 이름 · 수)</summary>
        public Mesh sharedMesh => new Mesh(m_Id);
    }

    /// <summary>Unity 의 SpriteRenderer: 그림 · 색 · 뒤집기 · Order in Layer</summary>
    public sealed unsafe class SpriteRenderer : Component
    {
        internal SpriteRenderer() { }
        public Color color
        {
            get { float* c = stackalloc float[4]; return Native.Api.SR_GetColor(m_Id, c) != 0 ? new Color(c[0], c[1], c[2], c[3]) : Color.white; }
            set { float* c = stackalloc float[4] { value.r, value.g, value.b, value.a }; Native.Api.SR_SetColor(m_Id, c); }
        }
        public bool flipX { get => Native.Api.SR_GetInt(m_Id, 0) != 0; set => Native.Api.SR_SetInt(m_Id, 0, value ? 1 : 0); }
        public bool flipY { get => Native.Api.SR_GetInt(m_Id, 1) != 0; set => Native.Api.SR_SetInt(m_Id, 1, value ? 1 : 0); }
        public int sortingOrder { get => Native.Api.SR_GetInt(m_Id, 2); set => Native.Api.SR_SetInt(m_Id, 2, value); }
        /// <summary>Sorting Layer 이름 (Project Settings > Tags and Layers). 없는 이름은 무시</summary>
        public string sortingLayerName
        {
            get => Native.Str(Native.Api.SR_GetSortingLayer(m_Id)) ?? "Default";
            set { fixed (byte* p = Native.Utf8(value)) Native.Api.SR_SetSortingLayer(m_Id, p); }
        }
        /// <summary>그림 (null = 없음). Sprite.FromPath("Assets/…png") 또는 "builtin:Square"</summary>
        public Sprite sprite
        {
            get { string p = Native.Str(Native.Api.SR_GetSprite(m_Id)); return string.IsNullOrEmpty(p) ? null : new Sprite(p); }
            set { fixed (byte* p = Native.Utf8(value?.m_Path)) Native.Api.SR_SetSprite(m_Id, p); }
        }
    }

    /// <summary>프레임 애니메이션: .spriteanim 클립의 스프라이트를 같은 GameObject 의 SpriteRenderer 에 차례로</summary>
    public sealed unsafe class SpriteAnimator : Behaviour
    {
        internal SpriteAnimator() { }
        /// <summary>클립 이름 (= .spriteanim 파일 이름) 을 처음부터. 없으면 false</summary>
        public bool Play(string clip) { fixed (byte* p = Native.Utf8(clip)) return Native.Api.SA_Play(m_Id, p) != 0; }
        public void Stop() => Native.Api.SA_Stop(m_Id);
        public bool isPlaying => Native.Api.SA_GetInt(m_Id, 0) != 0;
        public int frame => Native.Api.SA_GetInt(m_Id, 1);
        public string currentClip => Native.Str(Native.Api.SA_Clip(m_Id)) ?? "";
        public float speed { get => Native.Api.SA_GetSpeed(m_Id); set => Native.Api.SA_SetSpeed(m_Id, value); }
    }

    /// <summary>Unity 의 Mesh 중 BlendShape 정보 (SkinnedMeshRenderer.sharedMesh)</summary>
    public sealed class Mesh
    {
        private readonly ulong m_Owner;
        internal Mesh(ulong owner) { m_Owner = owner; }
        public unsafe int blendShapeCount => Native.Api.SMR_Count(m_Owner);
        public unsafe string GetBlendShapeName(int index) => Native.Str(Native.Api.SMR_Name(m_Owner, index));
        public unsafe int GetBlendShapeIndex(string name)
        {
            fixed (byte* p = Native.Utf8(name)) return Native.Api.SMR_Index(m_Owner, p);
        }
    }
}
