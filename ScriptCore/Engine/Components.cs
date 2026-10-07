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
        /// <summary>그 자리 (월드) 에 힘 — 질량 중심에서 벗어난 만큼 돌림힘도 (Unity 와 같다)</summary>
        public void AddForceAtPosition(Vector3 force, Vector3 position, ForceMode mode = ForceMode.Force)
        {
            AddForce(force, mode);
            AddTorque(Vector3.Cross(position - worldCenterOfMass, force), mode);
        }
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
        /// <summary>끄면 부딪히지 않는다 (트리거도) — Inspector 의 체크 상자와 같은 값</summary>
        public unsafe bool enabled
        {
            get { fixed (byte* p = Native.Utf8(NativeTypeName(GetType()) ?? "Collider")) return Native.Api.Comp_GetEnabled(m_Id, p) != 0; }
            set { fixed (byte* p = Native.Utf8(NativeTypeName(GetType()) ?? "Collider")) Native.Api.Comp_SetEnabled(m_Id, p, value ? 1 : 0); }
        }
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
    // Character · Configurable Joint (Unity 와 같은 이름)
    public struct SoftJointLimit { public float limit, bounciness, contactDistance; }
    public struct SoftJointLimitSpring { public float spring, damper; }
    public struct JointDrive { public float positionSpring, positionDamper, maximumForce; public bool useAcceleration; }
    public enum ConfigurableJointMotion { Locked = 0, Limited = 1, Free = 2 }
    public enum RotationDriveMode { XYAndZ = 0, Slerp = 1 }

    public class Joint : Component
    {
        internal Joint() { }
        internal virtual int Kind => 0;
        internal unsafe float F(int p) => Native.Api.JT_GetFloat(m_Id, Kind, p);
        internal unsafe void SetF(int p, float v) => Native.Api.JT_SetFloat(m_Id, Kind, p, v);
        internal unsafe Vector3 V(int p) { Vector3 v; Native.Api.JT_GetVector(m_Id, Kind, p, &v); return v; }
        internal unsafe void SetV(int p, Vector3 v) => Native.Api.JT_SetVector(m_Id, Kind, p, &v);
        internal SoftJointLimit Lim(int p) => new SoftJointLimit { limit = F(p), bounciness = F(p + 1), contactDistance = F(p + 2) };
        internal void SetLim(int p, SoftJointLimit l) { SetF(p, l.limit); SetF(p + 1, l.bounciness); SetF(p + 2, l.contactDistance); }
        internal SoftJointLimitSpring Spr(int p) => new SoftJointLimitSpring { spring = F(p), damper = F(p + 1) };
        internal void SetSpr(int p, SoftJointLimitSpring s) { SetF(p, s.spring); SetF(p + 1, s.damper); }

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

    /// <summary>래그돌 관절: axis = 비틀기 (low · highTwistLimit), swingAxis = 흔들기 1 (swing1Limit), 둘의 외적 = 흔들기 2 (swing2Limit)</summary>
    [NativeComponent("CharacterJoint")]
    public sealed class CharacterJoint : Joint
    {
        internal CharacterJoint() { }
        internal override int Kind => 3;
        public Vector3 swingAxis { get => V(3); set => SetV(3, value); }
        public SoftJointLimitSpring twistLimitSpring { get => Spr(50); set => SetSpr(50, value); }
        public SoftJointLimit lowTwistLimit { get => Lim(52); set => SetLim(52, value); }
        public SoftJointLimit highTwistLimit { get => Lim(55); set => SetLim(55, value); }
        public SoftJointLimitSpring swingLimitSpring { get => Spr(58); set => SetSpr(58, value); }
        public SoftJointLimit swing1Limit { get => Lim(60); set => SetLim(60, value); }
        public SoftJointLimit swing2Limit { get => Lim(63); set => SetLim(63, value); }
        public bool enableProjection { get => F(66) != 0; set => SetF(66, value ? 1 : 0); }
        public float projectionDistance { get => F(67); set => SetF(67, value); }
        public float projectionAngle { get => F(68); set => SetF(68, value); }
    }

    /// <summary>축마다 Locked / Limited / Free + 한계 · 드라이브. 조인트 틀: X = axis, Y = secondaryAxis, Z = X × Y</summary>
    [NativeComponent("ConfigurableJoint")]
    public sealed class ConfigurableJoint : Joint
    {
        internal ConfigurableJoint() { }
        internal override int Kind => 4;
        ConfigurableJointMotion M(int p) => (ConfigurableJointMotion)(int)F(p);
        void SetM(int p, ConfigurableJointMotion m) => SetF(p, (int)m);
        JointDrive D(int d) => new JointDrive { positionSpring = F(130 + 3 * d), positionDamper = F(131 + 3 * d), maximumForce = F(132 + 3 * d) };
        void SetD(int d, JointDrive v) { SetF(130 + 3 * d, v.positionSpring); SetF(131 + 3 * d, v.positionDamper); SetF(132 + 3 * d, v.maximumForce); }

        public Vector3 secondaryAxis { get => V(3); set => SetV(3, value); }
        public ConfigurableJointMotion xMotion { get => M(100); set => SetM(100, value); }
        public ConfigurableJointMotion yMotion { get => M(101); set => SetM(101, value); }
        public ConfigurableJointMotion zMotion { get => M(102); set => SetM(102, value); }
        public ConfigurableJointMotion angularXMotion { get => M(103); set => SetM(103, value); }
        public ConfigurableJointMotion angularYMotion { get => M(104); set => SetM(104, value); }
        public ConfigurableJointMotion angularZMotion { get => M(105); set => SetM(105, value); }
        public SoftJointLimitSpring linearLimitSpring { get => Spr(106); set => SetSpr(106, value); }
        public SoftJointLimit linearLimit { get => Lim(108); set => SetLim(108, value); }
        public SoftJointLimitSpring angularXLimitSpring { get => Spr(111); set => SetSpr(111, value); }
        public SoftJointLimit lowAngularXLimit { get => Lim(113); set => SetLim(113, value); }
        public SoftJointLimit highAngularXLimit { get => Lim(116); set => SetLim(116, value); }
        public SoftJointLimitSpring angularYZLimitSpring { get => Spr(119); set => SetSpr(119, value); }
        public SoftJointLimit angularYLimit { get => Lim(121); set => SetLim(121, value); }
        public SoftJointLimit angularZLimit { get => Lim(124); set => SetLim(124, value); }
        /// <summary>Unity 와 같이 이은 바디 쪽 목표 — 이 바디는 반대 방향으로 간다</summary>
        public Vector3 targetPosition { get => V(4); set => SetV(4, value); }
        public Vector3 targetVelocity { get => V(5); set => SetV(5, value); }
        public JointDrive xDrive { get => D(0); set => SetD(0, value); }
        public JointDrive yDrive { get => D(1); set => SetD(1, value); }
        public JointDrive zDrive { get => D(2); set => SetD(2, value); }
        public Quaternion targetRotation
        {
            get => new Quaternion(F(160), F(161), F(162), F(163));
            set { SetF(160, value.x); SetF(161, value.y); SetF(162, value.z); SetF(163, value.w); }
        }
        public Vector3 targetAngularVelocity { get => V(6); set => SetV(6, value); }
        public RotationDriveMode rotationDriveMode { get => (RotationDriveMode)(int)F(150); set => SetF(150, (int)value); }
        public JointDrive angularXDrive { get => D(3); set => SetD(3, value); }
        public JointDrive angularYZDrive { get => D(4); set => SetD(4, value); }
        public JointDrive slerpDrive { get => D(5); set => SetD(5, value); }
        /// <summary>아직 조인트 틀은 늘 로컬 (true 는 무시)</summary>
        public bool configuredInWorldSpace { get; set; }
        public bool swapBodies { get; set; }
    }

    /// <summary>NOVA 래그돌 (GameObject > 3D Object > Ragdoll 로 만든다): active = 물리가 뼈대를 움직인다 (Animator 끔), 끄면 바디가 애니메이션을 따라간다</summary>
    [NativeComponent("Ragdoll")]
    public sealed unsafe class Ragdoll : Component
    {
        internal Ragdoll() { }
        public bool active { get => Native.Api.RD_Get(m_Id, 0) != 0; set => Native.Api.RD_Set(m_Id, 0, value ? 1f : 0f); }
        public int bodyCount => (int)Native.Api.RD_Get(m_Id, 1);
    }

    // ------------------------------------------------------------------ WheelCollider (Unity 와 같은 API)
    public struct WheelFrictionCurve { public float extremumSlip, extremumValue, asymptoteSlip, asymptoteValue, stiffness; }
    public struct WheelHit
    {
        public Collider collider;
        public Vector3 point, normal, forwardDir, sidewaysDir;
        public float force, forwardSlip, sidewaysSlip;
    }

    /// <summary>차량 바퀴: 레이 서스펜션 + 미끄러짐 곡선 타이어. 부모 (차체) 에 Rigidbody</summary>
    [NativeComponent("WheelCollider")]
    public sealed unsafe class WheelCollider : Collider
    {
        internal WheelCollider() { }
        float F(int p) => Native.Api.WC_GetFloat(m_Id, p);
        void S(int p, float v) => Native.Api.WC_SetFloat(m_Id, p, v);
        WheelFrictionCurve Curve(int b) => new WheelFrictionCurve { extremumSlip = F(b), extremumValue = F(b + 1), asymptoteSlip = F(b + 2), asymptoteValue = F(b + 3), stiffness = F(b + 4) };
        void SetCurve(int b, WheelFrictionCurve c) { S(b, c.extremumSlip); S(b + 1, c.extremumValue); S(b + 2, c.asymptoteSlip); S(b + 3, c.asymptoteValue); S(b + 4, c.stiffness); }

        public Vector3 center { get { Vector3 v; Native.Api.WC_GetCenter(m_Id, &v); return v; } set => Native.Api.WC_SetCenter(m_Id, &value); }
        public float mass { get => F(0); set => S(0, value); }
        public float radius { get => F(1); set => S(1, value); }
        public float wheelDampingRate { get => F(2); set => S(2, value); }
        public float suspensionDistance { get => F(3); set => S(3, value); }
        public float forceAppPointDistance { get => F(4); set => S(4, value); }
        public JointSpring suspensionSpring
        {
            get => new JointSpring { spring = F(5), damper = F(6), targetPosition = F(7) };
            set { S(5, value.spring); S(6, value.damper); S(7, value.targetPosition); }
        }
        public WheelFrictionCurve forwardFriction { get => Curve(10); set => SetCurve(10, value); }
        public WheelFrictionCurve sidewaysFriction { get => Curve(20); set => SetCurve(20, value); }
        public float motorTorque { get => F(30); set => S(30, value); }
        public float brakeTorque { get => F(31); set => S(31, value); }
        /// <summary>조향 (도, + = 오른쪽)</summary>
        public float steerAngle { get => F(32); set => S(32, value); }
        public float rpm => F(40);
        public bool isGrounded => F(41) != 0;
        public float sprungMass => F(42);

        /// <summary>바퀴 그림이 놓일 자리 (서스펜션 · 조향 · 회전)</summary>
        public void GetWorldPose(out Vector3 pos, out Quaternion quat)
        {
            Vector3 p; Vector4 q;
            Native.Api.WC_GetPose(m_Id, &p, &q);
            pos = p;
            quat = new Quaternion(q.x, q.y, q.z, q.w);
        }

        public bool GetGroundHit(out WheelHit hit)
        {
            WheelHitData d;
            bool ok = Native.Api.WC_GetHit(m_Id, &d) != 0;
            hit = ok ? new WheelHit { point = d.point, normal = d.normal, forwardDir = d.forwardDir, sidewaysDir = d.sidewaysDir, force = d.force,
                forwardSlip = d.forwardSlip, sidewaysSlip = d.sidewaysSlip, collider = d.gameObject == 0 ? null : new Collider(d.gameObject) } : default;
            return ok;
        }

        /// <summary>Unity 호환 (NOVA 는 고정 스텝마다 계산 — 하는 일 없음)</summary>
        public void ConfigureVehicleSubsteps(float speedThreshold, int stepsBelowThreshold, int stepsAboveThreshold) { }
    }

    // ------------------------------------------------------------------ Cloth (Unity 와 같은 이름)
    public enum ClothPinMode { None = 0, TopEdge = 1, TopCorners = 2 }

    /// <summary>같은 오브젝트의 메시 (Plane 등) 를 천으로 (Play 중). 장면의 콜라이더와 부딪힌다</summary>
    [NativeComponent("Cloth")]
    public sealed unsafe class Cloth : Component
    {
        internal Cloth() { }
        float F(int p) => Native.Api.CL_GetFloat(m_Id, p);
        void S(int p, float v) => Native.Api.CL_SetFloat(m_Id, p, v);
        Vector3 Vec(int p) { Vector3 v; Native.Api.CL_GetVector(m_Id, p, &v); return v; }
        void SetVec(int p, Vector3 v) => Native.Api.CL_SetVector(m_Id, p, &v);

        public float stretchingStiffness { get => F(0); set => S(0, value); }
        public float bendingStiffness { get => F(1); set => S(1, value); }
        public bool useGravity { get => F(2) != 0; set => S(2, value ? 1f : 0f); }
        public float damping { get => F(3); set => S(3, value); }
        public float friction { get => F(4); set => S(4, value); }
        /// <summary>콜라이더에서 떨어지는 거리 (m, NOVA)</summary>
        public float thickness { get => F(5); set => S(5, value); }
        public float clothSolverFrequency { get => F(6); set => S(6, value); }
        public ClothPinMode pin { get => (ClothPinMode)(int)F(7); set => S(7, (int)value); }
        public Vector3 externalAcceleration { get => Vec(0); set => SetVec(0, value); }
        public Vector3 randomAcceleration { get => Vec(1); set => SetVec(1, value); }
        /// <summary>시뮬레이션 중 (Play)</summary>
        public bool isSimulating => F(8) != 0;
        /// <summary>이번 스텝의 오브젝트 움직임을 천 전체에 그대로 (순간 이동 — 휘날리지 않게). 1 m 넘게 한 번에 옮기면 저절로</summary>
        public void ClearTransformMotion() => S(20, 1f);

        /// <summary>천 정점마다 (같은 자리 메시 정점은 하나) 스킨 제한 — maxDistance 0 = 피부 (Skinned Mesh Renderer 의 자세) 에 붙음,
        /// float.MaxValue = 자유. collisionSphereDistance = 피부 안쪽으로 들어갈 수 있는 거리 (치마가 다리를 뚫지 않게)</summary>
        public ClothSkinningCoefficient[] coefficients
        {
            get
            {
                int n = Native.Api.CL_GetCoefficients(m_Id, null, 0);
                var f = new float[n * 2];
                if (n > 0) fixed (float* p = f) Native.Api.CL_GetCoefficients(m_Id, p, n);
                var a = new ClothSkinningCoefficient[n];
                for (int i = 0; i < n; ++i) a[i] = new ClothSkinningCoefficient { maxDistance = f[i * 2], collisionSphereDistance = f[i * 2 + 1] };
                return a;
            }
            set
            {
                int n = value?.Length ?? 0;
                var f = new float[n * 2];
                for (int i = 0; i < n; ++i) { f[i * 2] = value[i].maxDistance; f[i * 2 + 1] = value[i].collisionSphereDistance; }
                fixed (float* p = f) Native.Api.CL_SetCoefficients(m_Id, p, n);
            }
        }

        /// <summary>메시 정점마다 지금 위치 (오브젝트 로컬)</summary>
        public Vector3[] vertices
        {
            get
            {
                int n = Native.Api.CL_GetVertices(m_Id, null, 0);
                var a = new Vector3[n];
                if (n > 0) fixed (Vector3* p = a) Native.Api.CL_GetVertices(m_Id, p, n);
                return a;
            }
        }
    }

    /// <summary>Unity 의 ClothSkinningCoefficient: 천 정점 하나의 스킨 제한</summary>
    public struct ClothSkinningCoefficient
    {
        public float maxDistance;
        public float collisionSphereDistance;
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
        /// <summary>두 콜라이더 (의 Rigidbody) 끼리 부딪히지 않게 (Play 중에만)</summary>
        public static unsafe void IgnoreCollision(Collider collider1, Collider collider2, bool ignore = true)
        {
            if (collider1 == null || collider2 == null) return;
            Native.Api.PH_IgnoreCollision(collider1.m_Id, collider2.m_Id, ignore ? 1 : 0);
        }
        public static unsafe bool GetIgnoreCollision(Collider collider1, Collider collider2) =>
            collider1 != null && collider2 != null && Native.Api.PH_GetIgnoreCollision(collider1.m_Id, collider2.m_Id) != 0;
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
        /// <summary>세로 시야각 (도)</summary>
        public unsafe float fieldOfView { get => Native.Api.Cam_GetFloat(m_Id, 0); set => Native.Api.Cam_SetFloat(m_Id, 0, value); }
        public unsafe float nearClipPlane { get => Native.Api.Cam_GetFloat(m_Id, 1); set => Native.Api.Cam_SetFloat(m_Id, 1, value); }
        public unsafe float farClipPlane { get => Native.Api.Cam_GetFloat(m_Id, 2); set => Native.Api.Cam_SetFloat(m_Id, 2, value); }
        public unsafe float orthographicSize { get => Native.Api.Cam_GetFloat(m_Id, 3); set => Native.Api.Cam_SetFloat(m_Id, 3, value); }
        public unsafe float aspect => Native.Api.Cam_GetFloat(m_Id, 4);
        public unsafe bool orthographic => Native.Api.Cam_GetFloat(m_Id, 5) != 0f;

        /// <summary>화면 픽셀 (Input.mousePosition 과 같은 좌표 — 왼쪽 아래 0,0) → 월드 광선 (Unity 와 같다)</summary>
        public Ray ScreenPointToRay(Vector3 position) =>
            ViewportPointToRay(new Vector3(position.x / Mathf.Max(1, Screen.width), position.y / Mathf.Max(1, Screen.height), 0f));

        /// <summary>뷰포트 (0 ~ 1, 왼쪽 아래 0,0) → 월드 광선. 원근 = 카메라 자리에서, 직교 = 화면 평면에서 앞으로</summary>
        public Ray ViewportPointToRay(Vector3 position)
        {
            var t = transform;
            float x = position.x * 2f - 1f, y = position.y * 2f - 1f;
            if (orthographic)
                return new Ray(t.position + t.right * (x * orthographicSize * aspect) + t.up * (y * orthographicSize), t.forward);
            float h = Mathf.Tan(fieldOfView * 0.5f * Mathf.Deg2Rad);
            return new Ray(t.position, t.forward + t.right * (x * h * aspect) + t.up * (y * h));
        }
    }
    public sealed unsafe class Light : Behaviour
    {
        internal Light() { }
        /// <summary>이 빛이 비추는 레이어 (비트 = 레이어). 안 비추는 레이어는 이 빛의 그림자도 드리우지 않는다</summary>
        public int cullingMask { get => Native.Api.CL_GetMask(m_Id, 1); set => Native.Api.CL_SetMask(m_Id, 1, value); }
        public float intensity { get => Native.Api.Light_GetFloat(m_Id, 0); set => Native.Api.Light_SetFloat(m_Id, 0, value); }
        public float shadowStrength { get => Native.Api.Light_GetFloat(m_Id, 1); set => Native.Api.Light_SetFloat(m_Id, 1, value); }
        public float range => Native.Api.Light_GetFloat(m_Id, 2);
        public float spotAngle => Native.Api.Light_GetFloat(m_Id, 3);
        public Color color
        {
            get { Vector4 v; Native.Api.Light_GetColor(m_Id, &v); return new Color(v.x, v.y, v.z, 1f); }
            set { Vector4 v = new Vector4(value.r, value.g, value.b, 1f); Native.Api.Light_SetColor(m_Id, &v); }
        }
    }
    /// <summary>Unity 의 MeshRenderer (material · sharedMaterial · SetPropertyBlock · enabled · bounds = Renderer.cs)</summary>
    public sealed class MeshRenderer : Renderer { internal MeshRenderer() { } internal override int Kind => 0; }

    /// <summary>Unity 의 SkinnedMeshRenderer: BlendShape (모프 타깃) 가중치 0..100</summary>
    public sealed class SkinnedMeshRenderer : Renderer
    {
        internal SkinnedMeshRenderer() { }
        internal override int Kind => 1;
        public unsafe float GetBlendShapeWeight(int index) => Native.Api.SMR_GetWeight(m_Id, index);
        public unsafe void SetBlendShapeWeight(int index, float value) => Native.Api.SMR_SetWeight(m_Id, index, value);
        /// <summary>이 렌더러의 메시 (BlendShape 이름 · 수)</summary>
        public Mesh sharedMesh => new Mesh(m_Id);
    }

    /// <summary>Unity 의 SpriteRenderer: 그림 · 색 · 뒤집기 · Order in Layer</summary>
    public sealed unsafe class SpriteRenderer : Renderer
    {
        internal SpriteRenderer() { }
        internal override int Kind => 2;
        public Color color
        {
            get { float* c = stackalloc float[4]; return Native.Api.SR_GetColor(m_Id, c) != 0 ? new Color(c[0], c[1], c[2], c[3]) : Color.white; }
            set { float* c = stackalloc float[4] { value.r, value.g, value.b, value.a }; Native.Api.SR_SetColor(m_Id, c); }
        }
        public bool flipX { get => Native.Api.SR_GetInt(m_Id, 0) != 0; set => Native.Api.SR_SetInt(m_Id, 0, value ? 1 : 0); }
        public bool flipY { get => Native.Api.SR_GetInt(m_Id, 1) != 0; set => Native.Api.SR_SetInt(m_Id, 1, value ? 1 : 0); }
        // sortingOrder · sortingLayerName · sortingLayerID = Renderer (Renderer.cs)
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
