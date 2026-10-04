using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace NovaEngine.Interop
{
    // 게임 스크립트의 인스턴스 목록 (GameObject 별)
    internal static class ScriptRegistry
    {
        static readonly Dictionary<ulong, List<MonoBehaviour>> s_ByObject = new Dictionary<ulong, List<MonoBehaviour>>();
        static readonly List<MonoBehaviour> s_Empty = new List<MonoBehaviour>();

        internal static List<MonoBehaviour> Get(ulong id) => s_ByObject.TryGetValue(id, out var l) ? l : s_Empty;
        internal static IEnumerable<MonoBehaviour> All() => s_ByObject.Values.SelectMany(l => l).ToArray();
        internal static void Add(MonoBehaviour mb)
        {
            if (!s_ByObject.TryGetValue(mb.m_Id, out var l)) s_ByObject[mb.m_Id] = l = new List<MonoBehaviour>();
            l.Add(mb);
        }
        internal static void Remove(MonoBehaviour mb)
        {
            if (s_ByObject.TryGetValue(mb.m_Id, out var l)) { l.Remove(mb); if (l.Count == 0) s_ByObject.Remove(mb.m_Id); }
        }
        internal static void Clear() => s_ByObject.Clear();
        internal static MonoBehaviour FromHandle(IntPtr h) => h == IntPtr.Zero ? null : GCHandle.FromIntPtr(h).Target as MonoBehaviour;
    }

    // 게임 어셈블리(Assembly-CSharp)를 담는 수집 가능한 컨텍스트 → 다시 컴파일하면 통째로 내리고 새로 읽는다 (핫 리로드)
    internal sealed class GameLoadContext : AssemblyLoadContext
    {
        public GameLoadContext() : base("NovaGameScripts", isCollectible: true) { }
        protected override Assembly Load(AssemblyName name)
        {
            // 엔진 API 는 이미 올라온 것을 공유해야 타입이 같다
            if (name.Name == typeof(Bridge).Assembly.GetName().Name) return typeof(Bridge).Assembly;
            return null;
        }
    }

    internal sealed class FieldMeta
    {
        public FieldInfo Field;
        public string Kind;   // int, float, bool, string, Vector2, Vector3, Vector4, Color, enum, GameObject, Transform, AudioClip, unsupported
    }

    public static unsafe class Bridge
    {
        internal static int FrameIndex;
        static GameLoadContext s_Context;
        static Assembly s_Game;
        static readonly Dictionary<string, Type> s_Types = new Dictionary<string, Type>();
        static readonly Dictionary<Type, List<FieldMeta>> s_Fields = new Dictionary<Type, List<FieldMeta>>();
        static readonly string[] kMessages = { "Awake", "OnEnable", "Start", "Update", "LateUpdate", "FixedUpdate", "OnDisable", "OnDestroy" };

        // ================================================================== 네이티브 진입점
        [UnmanagedCallersOnly]
        public static int Initialize(IntPtr apiTable)
        {
            var api = (NativeApiTable*)apiTable;
            if (api->Size != sizeof(NativeApiTable)) return -sizeof(NativeApiTable);   // C++ 와 표 크기가 다르면 거부
            Native.Api = *api;
            return 1;
        }

        [UnmanagedCallersOnly]
        public static void BeginFrame() => FrameIndex++;

        [UnmanagedCallersOnly]
        public static void FreeString(IntPtr p) { if (p != IntPtr.Zero) Marshal.FreeCoTaskMem(p); }

        [UnmanagedCallersOnly]
        public static int LoadGameAssembly(byte* pathUtf8)
        {
            try
            {
                UnloadInternal();
                string path = Native.Str(pathUtf8);
                byte[] dll = File.ReadAllBytes(path);   // 바이트로 읽어 파일을 잠그지 않는다 (다시 빌드 가능)
                string pdbPath = Path.ChangeExtension(path, ".pdb");
                byte[] pdb = File.Exists(pdbPath) ? File.ReadAllBytes(pdbPath) : null;
                s_Context = new GameLoadContext();
                s_Game = pdb != null ? s_Context.LoadFromStream(new MemoryStream(dll), new MemoryStream(pdb)) : s_Context.LoadFromStream(new MemoryStream(dll));
                foreach (Type t in SafeTypes(s_Game))
                {
                    if (!t.IsClass || t.IsAbstract || t.IsGenericTypeDefinition || !typeof(MonoBehaviour).IsAssignableFrom(t)) continue;
                    s_Types[t.FullName] = t;
                    if (!s_Types.ContainsKey(t.Name)) s_Types[t.Name] = t;
                }
                return s_Types.Values.Distinct().Count() + 1;   // 0 = 실패와 구분
            }
            catch (Exception e)
            {
                LogException(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void UnloadGameAssembly() => UnloadInternal();

        // NOVA CLI (nova exec): 따로 빌드한 작은 어셈블리를 수집 가능한 컨텍스트로 읽어 NovaExec.Run() 을 부르고 결과를 글자로.
        // 엔진 API 와 게임 스크립트(Assembly-CSharp)는 이미 올라온 것을 함께 써서 타입이 같다. 끝나면 컨텍스트를 내린다
        sealed class ExecLoadContext : AssemblyLoadContext
        {
            public ExecLoadContext() : base("NovaExec", isCollectible: true) { }
            protected override Assembly Load(AssemblyName name)
            {
                if (name.Name == typeof(Bridge).Assembly.GetName().Name) return typeof(Bridge).Assembly;
                if (s_Game != null && name.Name == s_Game.GetName().Name) return s_Game;
                return null;
            }
        }

        [UnmanagedCallersOnly]
        public static IntPtr ExecAssembly(byte* pathUtf8)
        {
            string result;
            var ctx = new ExecLoadContext();
            try
            {
                byte[] dll = File.ReadAllBytes(Native.Str(pathUtf8));
                Assembly asm = ctx.LoadFromStream(new MemoryStream(dll));
                MethodInfo run = asm.GetType("NovaExec")?.GetMethod("Run", BindingFlags.Public | BindingFlags.Static);
                if (run == null) return Marshal.StringToCoTaskMemUTF8("error:NovaExec.Run not found");
                object value = run.Invoke(null, null);
                result = "ok:" + Describe(value);
            }
            catch (TargetInvocationException e) when (e.InnerException != null)
            {
                result = "error:" + e.InnerException.GetType().Name + ": " + e.InnerException.Message;
            }
            catch (Exception e)
            {
                result = "error:" + e.GetType().Name + ": " + e.Message;
            }
            finally
            {
                ctx.Unload();
            }
            return Marshal.StringToCoTaskMemUTF8(result);
        }

        // 결과 글자: null, 글자 그대로, 모음은 [a, b, …], 나머지는 ToString()
        static string Describe(object value)
        {
            if (value == null) return "null";
            if (value is string s) return s;
            if (value is System.Collections.IEnumerable list)
            {
                var parts = new List<string>();
                foreach (object o in list) { parts.Add(o == null ? "null" : o.ToString()); if (parts.Count >= 200) { parts.Add("…"); break; } }
                return "[" + string.Join(", ", parts) + "]";
            }
            return value.ToString();
        }

        static void UnloadInternal()
        {
            ScriptRegistry.Clear();
            NovaEngine.UI.UIEvents.Clear();   // 리스너가 게임 어셈블리를 붙잡지 않게
            s_Types.Clear();
            s_Fields.Clear();
            s_Game = null;
            if (s_Context != null)
            {
                s_Context.Unload();
                s_Context = null;
                for (int i = 0; i < 3; i++) { GC.Collect(); GC.WaitForPendingFinalizers(); }
            }
        }

        static IEnumerable<Type> SafeTypes(Assembly a)
        {
            try { return a.GetTypes(); }
            catch (ReflectionTypeLoadException e) { return e.Types.Where(t => t != null); }
        }

        // 스크립트 클래스와 Inspector 필드 (JSON)
        [UnmanagedCallersOnly]
        public static IntPtr GetClassesJson()
        {
            try
            {
                using var ms = new MemoryStream();
                using (var w = new Utf8JsonWriter(ms))
                {
                    w.WriteStartObject();
                    w.WriteStartArray("classes");
                    foreach (Type t in s_Types.Values.Distinct().OrderBy(t => t.FullName))
                    {
                        w.WriteStartObject();
                        w.WriteString("name", t.Name);
                        w.WriteString("fullName", t.FullName);
                        object defaults = null;
                        try { defaults = Activator.CreateInstance(t, true); } catch { }
                        w.WriteStartArray("fields");
                        foreach (FieldMeta f in FieldsOf(t))
                        {
                            w.WriteStartObject();
                            w.WriteString("name", f.Field.Name);
                            w.WriteString("label", Nicify(f.Field.Name));
                            w.WriteString("type", f.Kind);
                            if (f.Kind == "unsupported") w.WriteString("typeName", f.Field.FieldType.Name);
                            w.WritePropertyName("value");
                            WriteValue(w, f, defaults != null ? f.Field.GetValue(defaults) : null);
                            if (f.Field.GetCustomAttribute<HeaderAttribute>() is HeaderAttribute h) w.WriteString("header", h.header);
                            if (f.Field.GetCustomAttribute<TooltipAttribute>() is TooltipAttribute tip) w.WriteString("tooltip", tip.tooltip);
                            if (f.Field.GetCustomAttribute<SpaceAttribute>() is SpaceAttribute sp) w.WriteNumber("space", sp.height);
                            if (f.Field.GetCustomAttribute<RangeAttribute>() is RangeAttribute r)
                            {
                                w.WriteStartArray("range"); w.WriteNumberValue(r.min); w.WriteNumberValue(r.max); w.WriteEndArray();
                            }
                            if (f.Kind == "enum")
                            {
                                w.WriteStartArray("options");
                                foreach (string n in Enum.GetNames(f.Field.FieldType)) w.WriteStringValue(Nicify(n));
                                w.WriteEndArray();
                                w.WriteStartArray("optionValues");
                                foreach (object v in Enum.GetValues(f.Field.FieldType)) w.WriteNumberValue(Convert.ToInt64(v));
                                w.WriteEndArray();
                            }
                            w.WriteEndObject();
                        }
                        w.WriteEndArray();
                        // Button On Click () 에서 고를 수 있는 메서드
                        w.WriteStartArray("methods");
                        foreach (MethodInfo mi in UIMethodsOf(t))
                        {
                            w.WriteStartObject();
                            w.WriteString("name", mi.Name);
                            var ps = mi.GetParameters();
                            w.WriteString("param", ps.Length == 0 ? "" : ParamKind(ps[0].ParameterType));
                            w.WriteEndObject();
                        }
                        w.WriteEndArray();
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                    w.WriteEndObject();
                }
                return Marshal.StringToCoTaskMemUTF8(Encoding.UTF8.GetString(ms.ToArray()));
            }
            catch (Exception e)
            {
                LogException(e);
                return IntPtr.Zero;
            }
        }

        // Unity UnityEvent 가 Inspector 에서 고를 수 있는 것과 같은 조건: public, void, 인자 0개 또는 int/float/string/bool 하나
        static IEnumerable<MethodInfo> UIMethodsOf(Type t)
        {
            var seen = new HashSet<string>();
            for (Type c = t; c != null && c != typeof(MonoBehaviour); c = c.BaseType)
                foreach (MethodInfo mi in c.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly))
                {
                    if (mi.IsSpecialName || mi.IsGenericMethodDefinition || mi.ReturnType != typeof(void)) continue;
                    var ps = mi.GetParameters();
                    if (ps.Length > 1 || (ps.Length == 1 && ParamKind(ps[0].ParameterType) == null)) continue;
                    if (seen.Add(mi.Name + "/" + ps.Length)) yield return mi;
                }
        }

        static string ParamKind(Type t)
        {
            if (t == typeof(int)) return "int";
            if (t == typeof(float)) return "float";
            if (t == typeof(string)) return "string";
            if (t == typeof(bool)) return "bool";
            return null;
        }

        // Button On Click (): GameObject 의 스크립트 className 에서 method(argument) 호출. 1 = 찾아서 호출함
        [UnmanagedCallersOnly]
        public static int InvokeMethod(ulong gameObjectId, byte* classNameUtf8, byte* methodUtf8, byte* argumentUtf8)
        {
            string className = Native.Str(classNameUtf8), method = Native.Str(methodUtf8), arg = Native.Str(argumentUtf8) ?? "";
            foreach (MonoBehaviour mb in ScriptRegistry.Get(gameObjectId).ToArray())
            {
                Type t = mb.GetType();
                if (t.Name != className && t.FullName != className) continue;
                foreach (MethodInfo mi in UIMethodsOf(t))
                {
                    if (mi.Name != method) continue;
                    var ps = mi.GetParameters();
                    object[] args = null;
                    if (ps.Length == 1)
                    {
                        string kind = ParamKind(ps[0].ParameterType);
                        var inv = System.Globalization.CultureInfo.InvariantCulture;
                        args = new object[] { kind switch {
                            "int" => int.TryParse(arg, System.Globalization.NumberStyles.Integer, inv, out int i) ? i : 0,
                            "float" => float.TryParse(arg, System.Globalization.NumberStyles.Float, inv, out float f) ? f : 0f,
                            "bool" => arg == "true" || arg == "1",
                            _ => (object)arg } };
                    }
                    try { mi.Invoke(mb, args); }
                    catch (TargetInvocationException e) { LogException(e.InnerException ?? e); }
                    catch (Exception e) { LogException(e); }
                    return 1;
                }
            }
            return 0;
        }

        // C# 에서 AddListener 로 등록한 UI 이벤트
        // kind 0 Button.onClick, 1 Slider.onValueChanged(number), 2 Toggle.onValueChanged(number != 0),
        //      3 InputField.onValueChanged(text), 4 InputField.onEndEdit(text), -1 = 모두 비우기
        [UnmanagedCallersOnly]
        public static void InvokeUIEvent(ulong gameObjectId, int kind, float number, byte* textUtf8)
        {
            try
            {
                if (kind < 0) NovaEngine.UI.UIEvents.Clear();   // Play 시작/끝: 이전 리스너 정리
                else NovaEngine.UI.UIEvents.Invoke(gameObjectId, kind, number, Native.Str(textUtf8) ?? "");
            }
            catch (Exception e) { LogException(e); }
        }

        // 엔진 C# API 목록 (NOVA Code 자동 완성): NovaEngine 네임스페이스의 공개 타입과 멤버(이름, 타입)
        [UnmanagedCallersOnly]
        public static IntPtr GetApiJson()
        {
            try
            {
                const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.FlattenHierarchy;
                using var ms = new MemoryStream();
                using (var w = new Utf8JsonWriter(ms))
                {
                    w.WriteStartObject();
                    w.WriteStartArray("types");
                    foreach (Type t in typeof(Bridge).Assembly.GetExportedTypes().OrderBy(t => t.Name))
                    {
                        if (t.Namespace != "NovaEngine" || t.IsNested || t.Name.EndsWith("Attribute")) continue;
                        w.WriteStartObject();
                        w.WriteString("name", TypeName(t));
                        w.WriteStartArray("members");
                        var seen = new HashSet<string>();
                        foreach (MemberInfo mi in t.GetMembers(flags))
                        {
                            string type;
                            switch (mi)
                            {
                                case FieldInfo f: type = TypeName(f.FieldType); break;
                                case PropertyInfo p: type = TypeName(p.PropertyType); break;
                                case MethodInfo m when !m.IsSpecialName: type = TypeName(m.ReturnType); break;
                                case EventInfo e: type = TypeName(e.EventHandlerType); break;
                                default: continue;
                            }
                            if (mi.DeclaringType == typeof(object) || !seen.Add(mi.Name)) continue;
                            w.WriteStartObject();
                            w.WriteString("n", mi.Name);
                            w.WriteString("t", type);
                            w.WriteEndObject();
                        }
                        w.WriteEndArray();
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                    w.WriteEndObject();
                }
                return Marshal.StringToCoTaskMemUTF8(Encoding.UTF8.GetString(ms.ToArray()));
            }
            catch (Exception e)
            {
                LogException(e);
                return IntPtr.Zero;
            }
        }

        // List`1 → List, Vector3[] → Vector3 (자동 완성에서 '.' 뒤 멤버를 찾을 이름)
        static string TypeName(Type t)
        {
            if (t == null) return "";
            if (t.IsArray || t.IsByRef) return TypeName(t.GetElementType());
            string n = t.Name;
            int tick = n.IndexOf('`');
            return tick >= 0 ? n.Substring(0, tick) : n;
        }

        [UnmanagedCallersOnly]
        public static IntPtr CreateInstance(byte* classNameUtf8, ulong gameObjectId, IntPtr nativeComponent, byte* fieldsJsonUtf8, int enabled)
        {
            string className = Native.Str(classNameUtf8);
            try
            {
                if (!s_Types.TryGetValue(className, out Type t)) return IntPtr.Zero;
                var mb = (MonoBehaviour)Activator.CreateInstance(t, true);
                mb.m_Id = gameObjectId;
                mb.m_NativeComponent = nativeComponent;
                mb.SetEnabledFromNative(enabled != 0);
                string json = Native.Str(fieldsJsonUtf8);
                if (!string.IsNullOrEmpty(json)) ApplyFields(mb, json);
                mb.m_Messages = BuildMessages(mb);
                ScriptRegistry.Add(mb);
                return GCHandle.ToIntPtr(GCHandle.Alloc(mb));
            }
            catch (Exception e)
            {
                LogException(e);
                return IntPtr.Zero;
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyInstance(IntPtr handle)
        {
            if (handle == IntPtr.Zero) return;
            var gh = GCHandle.FromIntPtr(handle);
            if (gh.Target is MonoBehaviour mb)
            {
                mb.m_Destroyed = true;
                mb.StopAllCoroutines();
                ScriptRegistry.Remove(mb);
            }
            gh.Free();
        }

        [UnmanagedCallersOnly]
        public static void Invoke(IntPtr handle, int message)
        {
            var mb = ScriptRegistry.FromHandle(handle);
            if (mb == null || mb.m_Destroyed) return;
            Action a = mb.m_Messages != null && message >= 0 && message < mb.m_Messages.Length ? mb.m_Messages[message] : null;
            bool fixedStep = message == 5;
            if (fixedStep) Time.s_InFixedUpdate = true;
            try { a?.Invoke(); }
            catch (Exception e) { LogException(e); }
            finally { if (fixedStep) Time.s_InFixedUpdate = false; }
            if (message == 3 && !mb.m_Destroyed)   // Update 뒤: Invoke 와 코루틴 진행 (Unity 순서)
            {
                try { mb.TickInvokes(); } catch (Exception e) { LogException(e); }
                mb.TickCoroutines();
            }
        }

        [UnmanagedCallersOnly]
        public static void SetEnabled(IntPtr handle, int enabled) => ScriptRegistry.FromHandle(handle)?.SetEnabledFromNative(enabled != 0);

        // kind 0 = 충돌(OnCollisionXxx(Collision)), 1 = 트리거(OnTriggerXxx(Collider)) / phase 0 Enter, 1 Stay, 2 Exit
        [UnmanagedCallersOnly]
        public static void InvokeCollision(IntPtr handle, int kind, int phase, ulong other)
        {
            var mb = ScriptRegistry.FromHandle(handle);
            if (mb == null || mb.m_Destroyed) return;
            if (kind == 6)
            {
                MethodInfo jointMethod = FindMethod(mb.GetType(), "OnJointBreak2D", typeof(Joint2D)) ?? FindMethod(mb.GetType(), "OnJointBreak2D", null);
                if (jointMethod == null) return;
                try { jointMethod.Invoke(mb, jointMethod.GetParameters().Length == 0 ? null : new object[] { Joint2D.FromNative(mb.m_Id, phase, (int)other) }); }
                catch (TargetInvocationException e) { LogException(e.InnerException ?? e); }
                catch (Exception e) { LogException(e); }
                return;
            }
            if (kind == 3)
            {
                // OnJointBreak(float breakForce): 힘 = other 의 float 비트
                MethodInfo jm = FindMethod(mb.GetType(), "OnJointBreak", typeof(float)) ?? FindMethod(mb.GetType(), "OnJointBreak", null);
                if (jm == null) return;
                float force = BitConverter.Int32BitsToSingle((int)(uint)other);
                try { jm.Invoke(mb, jm.GetParameters().Length == 0 ? null : new object[] { force }); }
                catch (TargetInvocationException e) { LogException(e.InnerException ?? e); }
                catch (Exception e) { LogException(e); }
                return;
            }
            if (kind == 2)
            {
                // OnControllerColliderHit: phase = CharacterController 의 이번 Move 충돌 번호
                MethodInfo hm = FindMethod(mb.GetType(), "OnControllerColliderHit", typeof(ControllerColliderHit));
                if (hm == null) return;
                try { hm.Invoke(mb, new object[] { new ControllerColliderHit(mb.m_Id, phase) }); }
                catch (TargetInvocationException e) { LogException(e.InnerException ?? e); }
                catch (Exception e) { LogException(e); }
                return;
            }
            if (kind == 4 || kind == 5)
            {
                // 2D: OnCollisionEnter2D(Collision2D) · OnTriggerEnter2D(Collider2D) …
                string name2 = (kind == 4 ? "OnCollision" : "OnTrigger") + (phase == 0 ? "Enter" : phase == 1 ? "Stay" : "Exit") + "2D";
                MethodInfo m2 = FindMethod(mb.GetType(), name2, kind == 4 ? typeof(Collision2D) : typeof(Collider2D)) ?? FindMethod(mb.GetType(), name2, null);
                if (m2 == null) return;
                try
                {
                    if (m2.GetParameters().Length == 0) m2.Invoke(mb, null);
                    else m2.Invoke(mb, new object[] { kind == 4 ? new Collision2D(mb.m_Id, other) : (object)Collider2D.Of(other) });
                }
                catch (TargetInvocationException e) { LogException(e.InnerException ?? e); }
                catch (Exception e) { LogException(e); }
                return;
            }
            string name = (kind == 0 ? "OnCollision" : "OnTrigger") + (phase == 0 ? "Enter" : phase == 1 ? "Stay" : "Exit");
            MethodInfo mi = FindMethod(mb.GetType(), name, kind == 0 ? typeof(Collision) : typeof(Collider)) ?? FindMethod(mb.GetType(), name, null);
            if (mi == null) return;
            try
            {
                if (mi.GetParameters().Length == 0) mi.Invoke(mb, null);
                else mi.Invoke(mb, new object[] { kind == 0 ? new Collision(other) : new Collider(other) });
            }
            catch (TargetInvocationException e) { LogException(e.InnerException ?? e); }
            catch (Exception e) { LogException(e); }
        }

        // Play 중 Inspector: 현재 값 읽기 / 쓰기
        [UnmanagedCallersOnly]
        public static IntPtr GetFieldsJson(IntPtr handle)
        {
            var mb = ScriptRegistry.FromHandle(handle);
            if (mb == null) return IntPtr.Zero;
            try
            {
                using var ms = new MemoryStream();
                using (var w = new Utf8JsonWriter(ms))
                {
                    w.WriteStartObject();
                    foreach (FieldMeta f in FieldsOf(mb.GetType()))
                    {
                        if (f.Kind == "unsupported") continue;
                        w.WritePropertyName(f.Field.Name);
                        WriteValue(w, f, f.Field.GetValue(mb));
                    }
                    w.WriteEndObject();
                }
                return Marshal.StringToCoTaskMemUTF8(Encoding.UTF8.GetString(ms.ToArray()));
            }
            catch (Exception e) { LogException(e); return IntPtr.Zero; }
        }

        [UnmanagedCallersOnly]
        public static void SetFieldsJson(IntPtr handle, byte* jsonUtf8)
        {
            var mb = ScriptRegistry.FromHandle(handle);
            if (mb == null) return;
            try { ApplyFields(mb, Native.Str(jsonUtf8)); }
            catch (Exception e) { LogException(e); }
        }

        // ================================================================== 필드
        internal static List<FieldMeta> FieldsOf(Type t)
        {
            if (s_Fields.TryGetValue(t, out var list)) return list;
            list = new List<FieldMeta>();
            // 부모 클래스 필드부터 (Unity 와 같은 순서)
            var chain = new List<Type>();
            for (Type c = t; c != null && c != typeof(MonoBehaviour); c = c.BaseType) chain.Insert(0, c);
            foreach (Type c in chain)
                foreach (FieldInfo f in c.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                {
                    if (f.IsInitOnly || f.IsLiteral || f.GetCustomAttribute<NonSerializedAttribute>() != null) continue;
                    if (!f.IsPublic && f.GetCustomAttribute<SerializeField>() == null) continue;
                    if (f.GetCustomAttribute<HideInInspector>() != null) continue;
                    if (f.GetCustomAttribute<CompilerGeneratedAttribute>() != null) continue;
                    list.Add(new FieldMeta { Field = f, Kind = KindOf(f.FieldType) });
                }
            s_Fields[t] = list;
            return list;
        }

        static string KindOf(Type t)
        {
            if (t == typeof(int) || t == typeof(long) || t == typeof(short) || t == typeof(byte) || t == typeof(uint)) return "int";
            if (t == typeof(float) || t == typeof(double)) return "float";
            if (t == typeof(bool)) return "bool";
            if (t == typeof(string)) return "string";
            if (t == typeof(Vector2)) return "Vector2";
            if (t == typeof(Vector3)) return "Vector3";
            if (t == typeof(Vector4)) return "Vector4";
            if (t == typeof(Color)) return "Color";
            if (t.IsEnum) return "enum";
            if (t == typeof(GameObject)) return "GameObject";
            if (t == typeof(Transform)) return "Transform";
            if (t == typeof(AudioClip)) return "AudioClip";
            return "unsupported";
        }

        static void WriteValue(Utf8JsonWriter w, FieldMeta f, object v)
        {
            switch (f.Kind)
            {
                case "int": w.WriteNumberValue(v == null ? 0 : Convert.ToInt64(v)); break;
                case "float": w.WriteNumberValue(v == null ? 0.0 : Convert.ToDouble(v)); break;
                case "bool": w.WriteBooleanValue(v is bool b && b); break;
                case "string": w.WriteStringValue(v as string ?? string.Empty); break;
                case "Vector2": { var x = v is Vector2 q ? q : default; w.WriteStartArray(); w.WriteNumberValue(x.x); w.WriteNumberValue(x.y); w.WriteEndArray(); break; }
                case "Vector3": { var x = v is Vector3 q ? q : default; w.WriteStartArray(); w.WriteNumberValue(x.x); w.WriteNumberValue(x.y); w.WriteNumberValue(x.z); w.WriteEndArray(); break; }
                case "Vector4": { var x = v is Vector4 q ? q : default; w.WriteStartArray(); w.WriteNumberValue(x.x); w.WriteNumberValue(x.y); w.WriteNumberValue(x.z); w.WriteNumberValue(x.w); w.WriteEndArray(); break; }
                case "Color": { var x = v is Color q ? q : new Color(1, 1, 1, 1); w.WriteStartArray(); w.WriteNumberValue(x.r); w.WriteNumberValue(x.g); w.WriteNumberValue(x.b); w.WriteNumberValue(x.a); w.WriteEndArray(); break; }
                case "enum": w.WriteNumberValue(v == null ? 0 : Convert.ToInt64(v)); break;
                case "GameObject":
                case "Transform": w.WriteNumberValue(v is Object o && !(o is null) ? o.m_Id : 0UL); break;
                case "AudioClip": w.WriteStringValue(v is AudioClip c ? c.m_Path ?? "" : ""); break;
                default: w.WriteNullValue(); break;
            }
        }

        static void ApplyFields(MonoBehaviour mb, string json)
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.ValueKind != JsonValueKind.Object) return;
            foreach (FieldMeta f in FieldsOf(mb.GetType()))
            {
                if (!doc.RootElement.TryGetProperty(f.Field.Name, out JsonElement e)) continue;
                try
                {
                    object v = ReadValue(f, e);
                    if (v != null || !f.Field.FieldType.IsValueType) f.Field.SetValue(mb, v);
                }
                catch { /* 형식이 바뀐 필드는 기본값 유지 (Unity 와 같음) */ }
            }
        }

        static float F(JsonElement a, int i) => a.GetArrayLength() > i ? a[i].GetSingle() : 0f;

        static object ReadValue(FieldMeta f, JsonElement e)
        {
            Type t = f.Field.FieldType;
            switch (f.Kind)
            {
                case "int": return Convert.ChangeType(e.GetInt64(), t);
                case "float": return Convert.ChangeType(e.GetDouble(), t);
                case "bool": return e.ValueKind == JsonValueKind.True;
                case "string": return e.ValueKind == JsonValueKind.String ? e.GetString() : "";
                case "Vector2": return new Vector2(F(e, 0), F(e, 1));
                case "Vector3": return new Vector3(F(e, 0), F(e, 1), F(e, 2));
                case "Vector4": return new Vector4(F(e, 0), F(e, 1), F(e, 2), F(e, 3));
                case "Color": return new Color(F(e, 0), F(e, 1), F(e, 2), e.GetArrayLength() > 3 ? F(e, 3) : 1f);
                case "enum": return Enum.ToObject(t, e.GetInt64());
                case "GameObject": { ulong id = e.GetUInt64(); return id == 0 ? null : new GameObject(id); }
                case "Transform": { ulong id = e.GetUInt64(); return id == 0 ? null : new Transform(id); }
                case "AudioClip": { string p = e.GetString(); return string.IsNullOrEmpty(p) ? null : new AudioClip(p); }
            }
            return null;
        }

        // "moveSpeed" → "Move Speed", "m_Health" → "Health" (Unity 의 ObjectNames.NicifyVariableName)
        internal static string Nicify(string n)
        {
            if (n.StartsWith("m_") && n.Length > 2) n = n.Substring(2);
            else if (n.StartsWith("_") && n.Length > 1) n = n.Substring(1);
            else if (n.Length > 1 && n[0] == 'k' && char.IsUpper(n[1])) n = n.Substring(1);
            var sb = new StringBuilder();
            for (int i = 0; i < n.Length; i++)
            {
                char c = n[i];
                if (i == 0) { sb.Append(char.ToUpperInvariant(c)); continue; }
                bool upper = char.IsUpper(c), prevLower = char.IsLower(n[i - 1]) || char.IsDigit(n[i - 1]) && !char.IsDigit(c);
                bool nextLower = i + 1 < n.Length && char.IsLower(n[i + 1]);
                if (upper && (prevLower || (char.IsUpper(n[i - 1]) && nextLower))) sb.Append(' ');
                else if (char.IsDigit(c) && !char.IsDigit(n[i - 1])) sb.Append(' ');
                sb.Append(c == '_' ? ' ' : c);
            }
            return sb.ToString();
        }

        // ================================================================== 메시지
        static MethodInfo FindMethod(Type t, string name, Type param)
        {
            for (Type c = t; c != null && c != typeof(MonoBehaviour); c = c.BaseType)
            {
                MethodInfo mi = param == null
                    ? c.GetMethod(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, null, Type.EmptyTypes, null)
                    : c.GetMethod(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, null, new[] { param }, null);
                if (mi != null) return mi;
            }
            return null;
        }

        static Action[] BuildMessages(MonoBehaviour mb)
        {
            var arr = new Action[kMessages.Length];
            for (int i = 0; i < kMessages.Length; i++)
            {
                MethodInfo mi = FindMethod(mb.GetType(), kMessages[i], null);
                if (mi == null) continue;
                if (mi.ReturnType == typeof(void)) arr[i] = (Action)Delegate.CreateDelegate(typeof(Action), mb, mi);
                else if (mi.ReturnType == typeof(System.Collections.IEnumerator))   // IEnumerator Start() → 코루틴 (Unity 와 같음)
                {
                    var target = mb; var m = mi;
                    arr[i] = () => target.StartCoroutine((System.Collections.IEnumerator)m.Invoke(target, null));
                }
            }
            return arr;
        }

        internal static void SetNativeEnabled(MonoBehaviour mb, bool enabled)
        {
            if (mb.m_NativeComponent != IntPtr.Zero) Native.Api.Script_SetEnabled(mb.m_NativeComponent, enabled ? 1 : 0);
        }

        // ================================================================== 로그
        internal static void Log(int level, string message, string stack, string file, int line)
        {
            fixed (byte* m = Native.Utf8(message))
            fixed (byte* s = Native.Utf8(stack))
            fixed (byte* f = Native.Utf8(file))
                Native.Api.Log(level, m, s, f, line);
        }

        internal static void LogException(Exception e)
        {
            if (e is TargetInvocationException tie && tie.InnerException != null) e = tie.InnerException;
            string stack = FormatStack(new StackTrace(e, true), out string file, out int line);
            Log(2, $"{e.GetType().Name}: {e.Message}", stack, file, line);
        }

        // Unity 형식: "Player.Update () (at Assets/Scripts/Player.cs:12)", 엔진 API 프레임은 뺀다
        internal static string FormatStack(StackTrace st, out string file, out int line)
        {
            file = null; line = 0;
            var sb = new StringBuilder();
            foreach (StackFrame fr in st.GetFrames() ?? Array.Empty<StackFrame>())
            {
                MethodBase m = fr.GetMethod();
                if (m == null || m.DeclaringType == null) continue;
                if (m.DeclaringType.Assembly == typeof(Bridge).Assembly) continue;
                if (m.DeclaringType.Namespace != null && m.DeclaringType.Namespace.StartsWith("System")) continue;
                string f = fr.GetFileName();
                int l = fr.GetFileLineNumber();
                sb.Append(m.DeclaringType.FullName).Append(':').Append(m.Name).Append(" ()");
                if (!string.IsNullOrEmpty(f))
                {
                    string rel = RelativeToAssets(f);
                    sb.Append(" (at ").Append(rel).Append(':').Append(l).Append(')');
                    if (file == null) { file = f; line = l; }
                }
                sb.Append('\n');
            }
            return sb.ToString();
        }

        static string RelativeToAssets(string path)
        {
            string p = path.Replace('\\', '/');
            int i = p.IndexOf("/Assets/", StringComparison.OrdinalIgnoreCase);
            return i >= 0 ? p.Substring(i + 1) : p;
        }
    }
}
