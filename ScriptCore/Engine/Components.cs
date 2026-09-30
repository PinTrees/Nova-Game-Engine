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

    // ------------------------------------------------------------------ Animator
    public sealed class Animator : Behaviour
    {
        internal Animator() { }
        unsafe void Set(string n, int kind, float v) { fixed (byte* p = Native.Utf8(n)) Native.Api.AN_SetParam(m_Id, p, kind, v); }
        unsafe float Get(string n, int kind) { fixed (byte* p = Native.Utf8(n)) return Native.Api.AN_GetParam(m_Id, p, kind); }
        public void SetFloat(string name, float value) => Set(name, 0, value);
        public void SetInteger(string name, int value) => Set(name, 1, value);
        public void SetBool(string name, bool value) => Set(name, 2, value ? 1 : 0);
        public void SetTrigger(string name) => Set(name, 3, 1);
        public void ResetTrigger(string name) => Set(name, 3, 0);
        public float GetFloat(string name) => Get(name, 0);
        public int GetInteger(string name) => (int)Get(name, 1);
        public bool GetBool(string name) => Get(name, 2) != 0;
    }

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
        public static Vector3 gravity { get; set; } = new Vector3(0, -9.81f, 0);
        public static unsafe bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance = float.PositiveInfinity)
        {
            RaycastData d;
            if (float.IsInfinity(maxDistance)) maxDistance = 100000f;
            int r = Native.Api.PH_Raycast(&origin, &direction, maxDistance, &d);
            hitInfo = new RaycastHit { point = d.point, normal = d.normal, distance = d.distance, m_GameObject = d.gameObject };
            return r != 0;
        }
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = float.PositiveInfinity) => Raycast(origin, direction, out _, maxDistance);
        public static bool Raycast(Ray ray, out RaycastHit hitInfo, float maxDistance = float.PositiveInfinity) => Raycast(ray.origin, ray.direction, out hitInfo, maxDistance);
    }

    // ------------------------------------------------------------------ Camera / Light / Renderer
    public sealed class Camera : Behaviour
    {
        internal Camera() { }
        public static unsafe Camera main
        {
            get { ulong id = Native.Api.Camera_Main(); return id == 0 ? null : new Camera { m_Id = id }; }
        }
    }
    public sealed class Light : Behaviour { internal Light() { } }
    public sealed class MeshRenderer : Component { internal MeshRenderer() { } }
}
