using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using NovaEngine.Interop;

namespace NovaEngine
{
    // ------------------------------------------------------------------ 속성 (Inspector)
    [AttributeUsage(AttributeTargets.Field)] public sealed class SerializeField : Attribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class HideInInspector : Attribute { }
    [AttributeUsage(AttributeTargets.Field)] public sealed class RangeAttribute : Attribute { public readonly float min, max; public RangeAttribute(float min, float max) { this.min = min; this.max = max; } }
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = true)] public sealed class HeaderAttribute : Attribute { public readonly string header; public HeaderAttribute(string header) { this.header = header; } }
    [AttributeUsage(AttributeTargets.Field)] public sealed class TooltipAttribute : Attribute { public readonly string tooltip; public TooltipAttribute(string tooltip) { this.tooltip = tooltip; } }
    [AttributeUsage(AttributeTargets.Field)] public sealed class SpaceAttribute : Attribute { public readonly float height; public SpaceAttribute(float height = 8) { this.height = height; } }
    [AttributeUsage(AttributeTargets.Class)] public sealed class RequireComponent : Attribute { public readonly Type type; public RequireComponent(Type type) { this.type = type; } }
    [AttributeUsage(AttributeTargets.Class)] public sealed class DisallowMultipleComponent : Attribute { }

    public enum Space { World, Self }
    public enum PrimitiveType { Sphere, Capsule, Cylinder, Cube, Plane, Quad }

    // ------------------------------------------------------------------ Object
    // 네이티브 GameObject 를 fileID(64비트)로 가리킨다. 파괴된 오브젝트는 Unity 처럼 == null 이 참이 된다.
    public class Object
    {
        internal ulong m_Id;

        internal virtual bool IsAlive() => m_Id != 0 && Native_IsValid(m_Id);
        internal static unsafe bool Native_IsValid(ulong id) => id != 0 && Native.Api.GO_IsValid(id) != 0;

        public virtual string name
        {
            get => GameObject.GetName(m_Id);
            set => GameObject.SetName(m_Id, value);
        }

        public ulong GetInstanceID() => m_Id;

        public static bool operator ==(Object a, Object b)
        {
            bool an = a is null || !a.IsAlive(), bn = b is null || !b.IsAlive();
            if (an || bn) return an && bn;
            return a.Equals(b);
        }
        public static bool operator !=(Object a, Object b) => !(a == b);
        public static implicit operator bool(Object o) => !(o is null) && o.IsAlive();
        public override bool Equals(object other)
        {
            if (ReferenceEquals(this, other)) return true;
            if (this is MonoBehaviour || other is MonoBehaviour) return false;   // 스크립트는 인스턴스 자체로 비교
            return other is Object o && o.GetType() == GetType() && o.m_Id == m_Id;   // 래퍼(GameObject, Transform ...)는 같은 오브젝트면 같음
        }
        public override int GetHashCode() => m_Id.GetHashCode();
        public override string ToString() => IsAlive() ? $"{name} ({GetType().Name})" : "null";

        public static void Destroy(Object obj, float t = 0f)
        {
            if (obj is null) return;
            if (obj is MonoBehaviour mb) { mb.RequestDestroyComponent(); return; }
            if (obj is Component c && !(obj is Transform)) { c.RemoveNative(); return; }
            GameObject.DestroyNative(obj.m_Id, t);
        }
        public static void DestroyImmediate(Object obj) => Destroy(obj, 0f);
        public static void DontDestroyOnLoad(Object target) { }

        public static T Instantiate<T>(T original) where T : Object => (T)InstantiateImpl(original, false, Vector3.zero, Quaternion.identity, null);
        public static T Instantiate<T>(T original, Transform parent) where T : Object => (T)InstantiateImpl(original, false, Vector3.zero, Quaternion.identity, parent);
        public static T Instantiate<T>(T original, Vector3 position, Quaternion rotation) where T : Object => (T)InstantiateImpl(original, true, position, rotation, null);
        public static T Instantiate<T>(T original, Vector3 position, Quaternion rotation, Transform parent) where T : Object => (T)InstantiateImpl(original, true, position, rotation, parent);

        static unsafe Object InstantiateImpl(Object original, bool hasPose, Vector3 pos, Quaternion rot, Transform parent)
        {
            if (original == null) throw new ArgumentException("The Object you want to instantiate is null.");
            ulong id = Native.Api.GO_Instantiate(original.m_Id, hasPose ? 1 : 0, &pos, &rot, parent is null ? 0 : parent.m_Id);
            if (id == 0) return null;
            var go = new GameObject(id);
            // 복제본에서 같은 종류의 컴포넌트를 돌려준다 (Unity: Instantiate(component) → 복제본의 그 컴포넌트)
            switch (original)
            {
                case GameObject _: return go;
                case Transform _: return go.transform;
                case MonoBehaviour mb:
                {
                    var list = ScriptRegistry.Get(id);
                    foreach (var s in list) if (s.GetType() == mb.GetType()) return s;
                    return null;
                }
                case Component c: return go.GetComponent(c.GetType());
            }
            return go;
        }

        public static T FindObjectOfType<T>() where T : Object => FindFirstObjectByType<T>();
        public static T FindFirstObjectByType<T>() where T : Object
        {
            if (typeof(MonoBehaviour).IsAssignableFrom(typeof(T)))
                foreach (var s in ScriptRegistry.All()) if (s is T t && t.IsAlive()) return t;
            return null;
        }
        public static T[] FindObjectsOfType<T>() where T : Object => FindObjectsByType<T>();
        public static T[] FindObjectsByType<T>() where T : Object
        {
            var list = new List<T>();
            if (typeof(MonoBehaviour).IsAssignableFrom(typeof(T)))
                foreach (var s in ScriptRegistry.All()) if (s is T t && t.IsAlive()) list.Add(t);
            return list.ToArray();
        }
    }

    // ------------------------------------------------------------------ GameObject
    public sealed class GameObject : Object
    {
        internal GameObject(ulong id) { m_Id = id; }
        public unsafe GameObject() : this("New Game Object") { }
        public unsafe GameObject(string name)
        {
            fixed (byte* p = Native.Utf8(name)) m_Id = Native.Api.GO_Create(p);
        }
        public GameObject(string name, params Type[] components) : this(name)
        {
            foreach (var t in components) AddComponent(t);
        }

        internal static unsafe string GetName(ulong id) => Native.Str(Native.Api.GO_GetName(id)) ?? string.Empty;
        internal static unsafe void SetName(ulong id, string v) { fixed (byte* p = Native.Utf8(v)) Native.Api.GO_SetName(id, p); }
        internal static unsafe void DestroyNative(ulong id, float delay) => Native.Api.GO_Destroy(id, delay);

        public GameObject gameObject => this;
        public Transform transform => new Transform(m_Id);
        public unsafe bool activeSelf => Native.Api.GO_GetActive(m_Id, 0) != 0;
        public unsafe bool activeInHierarchy => Native.Api.GO_GetActive(m_Id, 1) != 0;
        public unsafe void SetActive(bool value) => Native.Api.GO_SetActive(m_Id, value ? 1 : 0);
        public unsafe string tag
        {
            get => Native.Str(Native.Api.GO_GetTag(m_Id)) ?? "Untagged";
            set { fixed (byte* p = Native.Utf8(value)) Native.Api.GO_SetTag(m_Id, p); }
        }
        public bool CompareTag(string t) => tag == t;

        public static unsafe GameObject Find(string name)
        {
            fixed (byte* p = Native.Utf8(name)) { ulong id = Native.Api.GO_Find(p, 0); return id == 0 ? null : new GameObject(id); }
        }
        public static unsafe GameObject FindWithTag(string tag)
        {
            fixed (byte* p = Native.Utf8(tag)) { ulong id = Native.Api.GO_Find(p, 1); return id == 0 ? null : new GameObject(id); }
        }
        public static GameObject FindGameObjectWithTag(string tag) => FindWithTag(tag);
        public static unsafe GameObject CreatePrimitive(PrimitiveType type)
        {
            ulong id = Native.Api.GO_CreatePrimitive((int)type);
            return id == 0 ? null : new GameObject(id);
        }

        public T GetComponent<T>() where T : class => GetComponent(typeof(T)) as T;
        public unsafe Component GetComponent(Type type)
        {
            if (type == typeof(Transform) || type == typeof(Component)) return transform;
            if (typeof(MonoBehaviour).IsAssignableFrom(type) || type == typeof(Behaviour))
            {
                foreach (var s in ScriptRegistry.Get(m_Id)) if (type.IsInstanceOfType(s)) return s;
                return null;
            }
            string native = Component.NativeTypeName(type);
            if (native == null) return null;
            fixed (byte* p = Native.Utf8(native))
                if (Native.Api.GO_HasComponent(m_Id, p) == 0) return null;
            var c = (Component)Activator.CreateInstance(type, true);
            c.m_Id = m_Id;
            return c;
        }
        public bool TryGetComponent<T>(out T component) where T : class { component = GetComponent<T>(); return component != null; }
        public T[] GetComponents<T>() where T : class
        {
            var list = new List<T>();
            if (typeof(MonoBehaviour).IsAssignableFrom(typeof(T)) || typeof(T) == typeof(Component) || typeof(T) == typeof(Behaviour))
            {
                foreach (var s in ScriptRegistry.Get(m_Id))
                    if (s is T t) list.Add(t);
            }
            else if (GetComponent<T>() is T one)
                list.Add(one);
            return list.ToArray();
        }
        public T GetComponentInChildren<T>() where T : class
        {
            var c = GetComponent<T>();
            if (c != null) return c;
            var tr = transform;
            for (int i = 0; i < tr.childCount; i++)
            {
                var r = tr.GetChild(i).gameObject.GetComponentInChildren<T>();
                if (r != null) return r;
            }
            return null;
        }
        public T GetComponentInParent<T>() where T : class
        {
            for (Transform t = transform; t != null; t = t.parent)
            {
                var c = t.gameObject.GetComponent<T>();
                if (c != null) return c;
            }
            return null;
        }

        public T AddComponent<T>() where T : Component => AddComponent(typeof(T)) as T;
        public unsafe Component AddComponent(Type type)
        {
            string key = typeof(MonoBehaviour).IsAssignableFrom(type) ? "script:" + type.FullName : Component.NativeTypeName(type);
            if (key == null) throw new ArgumentException($"AddComponent: {type.Name} is not supported");
            IntPtr r;
            fixed (byte* p = Native.Utf8(key)) r = Native.Api.GO_AddComponent(m_Id, p);
            if (r == IntPtr.Zero) return null;
            if (typeof(MonoBehaviour).IsAssignableFrom(type))
                return ScriptRegistry.FromHandle(r);
            return GetComponent(type);
        }

        public void SendMessage(string methodName, object value = null)
        {
            foreach (var s in ScriptRegistry.Get(m_Id)) s.InvokeByName(methodName, value);
        }
        public void BroadcastMessage(string methodName, object value = null)
        {
            SendMessage(methodName, value);
            var tr = transform;
            for (int i = 0; i < tr.childCount; i++) tr.GetChild(i).gameObject.BroadcastMessage(methodName, value);
        }
    }

    // ------------------------------------------------------------------ Component
    public class Component : Object
    {
        public GameObject gameObject => new GameObject(m_Id);
        public Transform transform => new Transform(m_Id);
        public string tag { get => gameObject.tag; set => gameObject.tag = value; }
        public bool CompareTag(string t) => gameObject.CompareTag(t);
        public T GetComponent<T>() where T : class => gameObject.GetComponent<T>();
        public Component GetComponent(Type type) => gameObject.GetComponent(type);
        public bool TryGetComponent<T>(out T component) where T : class => gameObject.TryGetComponent(out component);
        public T[] GetComponents<T>() where T : class => gameObject.GetComponents<T>();
        public T GetComponentInChildren<T>() where T : class => gameObject.GetComponentInChildren<T>();
        public T GetComponentInParent<T>() where T : class => gameObject.GetComponentInParent<T>();
        public void SendMessage(string methodName, object value = null) => gameObject.SendMessage(methodName, value);

        // C# 타입 → 네이티브 컴포넌트 타입 이름
        internal static string NativeTypeName(Type t)
        {
            if (t == typeof(Rigidbody)) return "RigidBody";
            if (t == typeof(AudioSource)) return "AudioSource";
            if (t == typeof(AudioListener)) return "AudioListener";
            if (t == typeof(Animator)) return "Animator";
            if (t == typeof(Collider)) return "Collider";
            if (t == typeof(BoxCollider)) return "BoxCollider";
            if (t == typeof(SphereCollider)) return "SphereCollider";
            if (t == typeof(CapsuleCollider)) return "CapsuleCollider";
            if (t == typeof(Camera)) return "Camera";
            if (t == typeof(Light)) return "Light";
            if (t == typeof(MeshRenderer)) return "MeshRenderer";
            return null;
        }

        internal unsafe void RemoveNative()
        {
            string n = NativeTypeName(GetType());
            if (n == null) return;
            fixed (byte* p = Native.Utf8(n)) Native.Api.GO_RemoveComponent(m_Id, p);
        }
    }

    public class Behaviour : Component
    {
        public virtual bool enabled { get; set; } = true;
        public bool isActiveAndEnabled => enabled && gameObject.activeInHierarchy;
    }

    // ------------------------------------------------------------------ Transform
    public class Transform : Component, IEnumerable
    {
        internal Transform() { }
        internal Transform(ulong id) { m_Id = id; }

        unsafe Vector3 GetV(int p) { Vector3 v; Native.Api.TR_GetVector(m_Id, p, &v); return v; }
        unsafe void SetV(int p, Vector3 v) => Native.Api.TR_SetVector(m_Id, p, &v);
        unsafe Quaternion GetQ(int p) { Quaternion q; Native.Api.TR_GetQuat(m_Id, p, &q); return q; }
        unsafe void SetQ(int p, Quaternion q) => Native.Api.TR_SetQuat(m_Id, p, &q);

        public Vector3 position { get => GetV(0); set => SetV(0, value); }
        public Vector3 localPosition { get => GetV(1); set => SetV(1, value); }
        public Vector3 localScale { get => GetV(2); set => SetV(2, value); }
        public Vector3 eulerAngles { get => GetV(3); set => SetV(3, value); }
        public Vector3 localEulerAngles { get => GetV(4); set => SetV(4, value); }
        public Vector3 lossyScale => GetV(5);
        public Vector3 forward { get => GetV(6); set => rotation = Quaternion.LookRotation(value); }
        public Vector3 right { get => GetV(7); set => rotation = Quaternion.FromToRotation(Vector3.right, value); }
        public Vector3 up { get => GetV(8); set => rotation = Quaternion.FromToRotation(Vector3.up, value); }
        public Quaternion rotation { get => GetQ(0); set => SetQ(0, value); }
        public Quaternion localRotation { get => GetQ(1); set => SetQ(1, value); }

        public unsafe Transform parent
        {
            get { ulong p = Native.Api.TR_GetParent(m_Id); return p == 0 ? null : new Transform(p); }
            set => SetParent(value, true);
        }
        public unsafe void SetParent(Transform p, bool worldPositionStays = true) => Native.Api.TR_SetParent(m_Id, p is null ? 0 : p.m_Id, worldPositionStays ? 1 : 0);
        public Transform root { get { Transform t = this; while (t.parent != null) t = t.parent; return t; } }
        public unsafe int childCount => Native.Api.TR_GetChildCount(m_Id);
        public unsafe Transform GetChild(int index)
        {
            ulong c = Native.Api.TR_GetChild(m_Id, index);
            if (c == 0) throw new IndexOutOfRangeException("Transform child out of bounds");
            return new Transform(c);
        }
        public Transform Find(string n)
        {
            for (int i = 0; i < childCount; i++) { var c = GetChild(i); if (c.name == n) return c; }
            return null;
        }
        public bool IsChildOf(Transform p) { for (Transform t = this; t != null; t = t.parent) if (t.m_Id == p.m_Id) return true; return false; }
        public void SetPositionAndRotation(Vector3 p, Quaternion r) { position = p; rotation = r; }

        public void Translate(Vector3 translation, Space relativeTo = Space.Self)
        {
            if (relativeTo == Space.World) position += translation;
            else position += rotation * translation;
        }
        public void Translate(float x, float y, float z, Space relativeTo = Space.Self) => Translate(new Vector3(x, y, z), relativeTo);
        public void Translate(Vector3 translation, Transform relativeTo) => position += relativeTo != null ? relativeTo.rotation * translation : translation;
        public void Rotate(Vector3 eulers, Space relativeTo = Space.Self)
        {
            Quaternion q = Quaternion.Euler(eulers);
            rotation = relativeTo == Space.Self ? rotation * q : q * rotation;
        }
        public void Rotate(float x, float y, float z, Space relativeTo = Space.Self) => Rotate(new Vector3(x, y, z), relativeTo);
        public void Rotate(Vector3 axis, float angle, Space relativeTo = Space.Self)
        {
            Quaternion q = Quaternion.AngleAxis(angle, axis);
            rotation = relativeTo == Space.Self ? rotation * q : q * rotation;
        }
        public void RotateAround(Vector3 point, Vector3 axis, float angle)
        {
            Quaternion q = Quaternion.AngleAxis(angle, axis);
            position = point + q * (position - point);
            rotation = q * rotation;
        }
        public void LookAt(Transform target) { if (target != null) LookAt(target.position, Vector3.up); }
        public void LookAt(Vector3 worldPosition) => LookAt(worldPosition, Vector3.up);
        public void LookAt(Vector3 worldPosition, Vector3 worldUp)
        {
            Vector3 d = worldPosition - position;
            if (d.sqrMagnitude > 1e-10f) rotation = Quaternion.LookRotation(d, worldUp);
        }
        public Vector3 TransformPoint(Vector3 p) => position + rotation * Vector3.Scale(p, lossyScale);
        public Vector3 InverseTransformPoint(Vector3 p)
        {
            Vector3 s = lossyScale, l = Quaternion.Inverse(rotation) * (p - position);
            return new Vector3(s.x != 0 ? l.x / s.x : 0, s.y != 0 ? l.y / s.y : 0, s.z != 0 ? l.z / s.z : 0);
        }
        public Vector3 TransformDirection(Vector3 d) => rotation * d;
        public Vector3 InverseTransformDirection(Vector3 d) => Quaternion.Inverse(rotation) * d;
        public Vector3 TransformVector(Vector3 v) => rotation * Vector3.Scale(v, lossyScale);

        public IEnumerator GetEnumerator() { for (int i = 0; i < childCount; i++) yield return GetChild(i); }
    }

    // ------------------------------------------------------------------ 코루틴
    public class YieldInstruction { }
    public class Coroutine : YieldInstruction
    {
        internal readonly Stack<IEnumerator> Stack = new Stack<IEnumerator>();
        internal float WaitUntilTime = -1f;
        internal bool Realtime;
        internal Func<bool> WaitCondition;
        internal Coroutine WaitFor;
        internal bool Done;
    }
    public sealed class WaitForSeconds : YieldInstruction { internal readonly float seconds; public WaitForSeconds(float seconds) { this.seconds = seconds; } }
    public sealed class WaitForSecondsRealtime : YieldInstruction { internal readonly float seconds; public WaitForSecondsRealtime(float seconds) { this.seconds = seconds; } }
    public sealed class WaitForEndOfFrame : YieldInstruction { }
    public sealed class WaitForFixedUpdate : YieldInstruction { }
    public sealed class WaitUntil : YieldInstruction { internal readonly Func<bool> predicate; public WaitUntil(Func<bool> predicate) { this.predicate = predicate; } }
    public sealed class WaitWhile : YieldInstruction { internal readonly Func<bool> predicate; public WaitWhile(Func<bool> predicate) { this.predicate = predicate; } }

    // ------------------------------------------------------------------ MonoBehaviour
    public class MonoBehaviour : Behaviour
    {
        internal IntPtr m_NativeComponent;   // 네이티브 CSharpScript 컴포넌트
        internal bool m_Destroyed;
        internal Action[] m_Messages;        // Awake, OnEnable, Start, Update, LateUpdate, FixedUpdate, OnDisable, OnDestroy
        readonly List<Coroutine> m_Coroutines = new List<Coroutine>();
        readonly List<(string method, float time, float repeat)> m_Invokes = new List<(string, float, float)>();
        bool m_Enabled = true;

        internal override bool IsAlive() => !m_Destroyed && base.IsAlive();

        public override bool enabled
        {
            get => m_Enabled;
            set
            {
                if (m_Enabled == value) return;
                m_Enabled = value;
                Bridge.SetNativeEnabled(this, value);
            }
        }
        internal void SetEnabledFromNative(bool v) => m_Enabled = v;
        public bool useGUILayout { get; set; } = true;

        public static void print(object message) => Debug.Log(message);

        internal unsafe void RequestDestroyComponent()
        {
            // 컴포넌트만 지우기: 네이티브에서 떼어내면 OnDestroy 가 불린다
            fixed (byte* p = Native.Utf8("script:" + GetType().FullName))
                Native.Api.GO_RemoveComponent(m_Id, p);
        }

        // ---- 코루틴 (Update 뒤에 진행)
        public Coroutine StartCoroutine(IEnumerator routine)
        {
            if (routine == null) return null;
            var c = new Coroutine();
            c.Stack.Push(routine);
            m_Coroutines.Add(c);
            StepCoroutine(c);   // Unity: 첫 yield 까지는 바로 실행
            return c;
        }
        public Coroutine StartCoroutine(string methodName)
        {
            var mi = GetType().GetMethod(methodName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            return mi != null && mi.Invoke(this, null) is IEnumerator e ? StartCoroutine(e) : null;
        }
        public void StopCoroutine(Coroutine c) { if (c != null) { c.Done = true; m_Coroutines.Remove(c); } }
        public void StopCoroutine(IEnumerator routine) { m_Coroutines.RemoveAll(c => c.Stack.Count > 0 && ReferenceEquals(c.Stack.Peek(), routine)); }
        public void StopAllCoroutines() { foreach (var c in m_Coroutines) c.Done = true; m_Coroutines.Clear(); }

        internal void TickCoroutines()
        {
            if (m_Coroutines.Count == 0) return;
            foreach (var c in m_Coroutines.ToArray())
            {
                if (c.Done) continue;
                if (c.WaitUntilTime >= 0f && (c.Realtime ? Time.realtimeSinceStartup : Time.time) < c.WaitUntilTime) continue;
                if (c.WaitCondition != null && !c.WaitCondition()) continue;
                if (c.WaitFor != null && !c.WaitFor.Done) continue;
                StepCoroutine(c);
            }
            m_Coroutines.RemoveAll(c => c.Done);
        }

        void StepCoroutine(Coroutine c)
        {
            c.WaitUntilTime = -1f; c.WaitCondition = null; c.WaitFor = null;
            while (c.Stack.Count > 0)
            {
                IEnumerator top = c.Stack.Peek();
                bool more;
                try { more = top.MoveNext(); }
                catch (Exception e) { Bridge.LogException(e); c.Done = true; return; }
                if (!more) { c.Stack.Pop(); continue; }
                switch (top.Current)
                {
                    case null: return;
                    case WaitForSeconds w: c.WaitUntilTime = Time.time + w.seconds; c.Realtime = false; return;
                    case WaitForSecondsRealtime w: c.WaitUntilTime = Time.realtimeSinceStartup + w.seconds; c.Realtime = true; return;
                    case WaitUntil w: c.WaitCondition = w.predicate; return;
                    case WaitWhile w: { var p = w.predicate; c.WaitCondition = () => !p(); return; }
                    case Coroutine other: c.WaitFor = other; return;
                    case IEnumerator nested: c.Stack.Push(nested); continue;
                    default: return;   // WaitForEndOfFrame, WaitForFixedUpdate 등 = 다음 프레임
                }
            }
            c.Done = true;
        }

        // ---- Invoke
        public void Invoke(string methodName, float time) => m_Invokes.Add((methodName, Time.time + time, -1f));
        public void InvokeRepeating(string methodName, float time, float repeatRate) => m_Invokes.Add((methodName, Time.time + time, repeatRate));
        public void CancelInvoke() => m_Invokes.Clear();
        public void CancelInvoke(string methodName) => m_Invokes.RemoveAll(i => i.method == methodName);
        public bool IsInvoking(string methodName) => m_Invokes.Exists(i => i.method == methodName);
        public bool IsInvoking() => m_Invokes.Count > 0;

        internal void TickInvokes()
        {
            if (m_Invokes.Count == 0) return;
            float now = Time.time;
            for (int i = 0; i < m_Invokes.Count; i++)
            {
                var inv = m_Invokes[i];
                if (now < inv.time) continue;
                InvokeByName(inv.method, null);
                if (inv.repeat > 0f) m_Invokes[i] = (inv.method, inv.time + inv.repeat, inv.repeat);
                else { m_Invokes.RemoveAt(i); i--; }
            }
        }

        internal void InvokeByName(string methodName, object arg)
        {
            var t = GetType();
            const BindingFlags f = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (var mi in t.GetMethods(f))
            {
                if (mi.Name != methodName) continue;
                var ps = mi.GetParameters();
                try
                {
                    if (ps.Length == 0) { mi.Invoke(this, null); return; }
                    if (ps.Length == 1 && (arg == null || ps[0].ParameterType.IsInstanceOfType(arg))) { mi.Invoke(this, new[] { arg }); return; }
                }
                catch (TargetInvocationException e) { Bridge.LogException(e.InnerException ?? e); return; }
            }
        }
    }
}
