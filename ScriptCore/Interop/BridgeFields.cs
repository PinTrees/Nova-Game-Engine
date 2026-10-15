using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace NovaEngine.Interop
{
    // 직렬화 필드 하나의 형식 (Unity 의 SerializedProperty 종류와 같은 범위).
    //  Kind: int, float, bool, string, Vector2, Vector3, Vector4, Color, enum, GameObject, Transform, AudioClip,
    //        ScriptableObject (값 = 에셋 경로), array (T[] · List<T>, Element), struct ([Serializable] 클래스 · 구조체, Children), unsupported
    internal sealed class FieldMeta
    {
        public FieldInfo Field;            // 배열 원소는 null
        public Type Type;
        public string Kind;
        public FieldMeta Element;          // array
        public List<FieldMeta> Children;   // struct
        public bool IsList;                // array: List<T> (아니면 T[])
    }

    public static unsafe partial class Bridge
    {
        const int kMaxDepth = 7;   // Unity 도 깊이 제한이 있다 (자기 자신을 품는 클래스)

        // MonoBehaviour · ScriptableObject 의 Inspector 필드 (부모 클래스 필드부터, Unity 와 같은 순서)
        internal static List<FieldMeta> FieldsOf(Type t)
        {
            if (s_Fields.TryGetValue(t, out var list)) return list;
            list = PlainFields(t, 0);
            s_Fields[t] = list;
            return list;
        }

        static bool IsEngineBase(Type c) => c == null || c == typeof(object) || c == typeof(Object) || c == typeof(MonoBehaviour) || c == typeof(ScriptableObject)
            || c == typeof(Behaviour) || c == typeof(Component);

        static List<FieldMeta> PlainFields(Type t, int depth)
        {
            var list = new List<FieldMeta>();
            var chain = new List<Type>();
            for (Type c = t; !IsEngineBase(c); c = c.BaseType) chain.Insert(0, c);
            foreach (Type c in chain)
                foreach (FieldInfo f in c.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                {
                    if (f.IsInitOnly || f.IsLiteral || f.GetCustomAttribute<NonSerializedAttribute>() != null) continue;
                    if (!f.IsPublic && f.GetCustomAttribute<SerializeField>() == null) continue;
                    if (f.GetCustomAttribute<HideInInspector>() != null) continue;
                    if (f.GetCustomAttribute<CompilerGeneratedAttribute>() != null) continue;
                    FieldMeta m = MetaOf(f.FieldType, depth);
                    m.Field = f;
                    list.Add(m);
                }
            return list;
        }

        static FieldMeta MetaOf(Type t, int depth)
        {
            var m = new FieldMeta { Type = t, Kind = KindOf(t) };
            if (m.Kind != "unsupported" || depth >= kMaxDepth) return m;
            Type elem = t.IsArray && t.GetArrayRank() == 1 ? t.GetElementType()
                : (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(List<>) ? t.GetGenericArguments()[0] : null);
            if (elem != null)
            {
                FieldMeta e = MetaOf(elem, depth + 1);
                if (e.Kind != "unsupported" && e.Kind != "array")   // Unity 도 배열의 배열은 직렬화하지 않는다
                {
                    m.Kind = "array";
                    m.Element = e;
                    m.IsList = !t.IsArray;
                }
            }
            else if (IsSerializableType(t))
            {
                m.Kind = "struct";
                m.Children = PlainFields(t, depth + 1);
            }
            return m;
        }

        // [Serializable] 사용자 클래스 · 구조체 (Unity 와 같은 조건 — 엔진 오브젝트 · 추상 · 제네릭 정의는 빼고)
        static bool IsSerializableType(Type t)
        {
            if (t.IsPrimitive || t.IsEnum || t == typeof(string) || t.IsAbstract || t.IsInterface || t.IsGenericTypeDefinition) return false;
            if (typeof(Object).IsAssignableFrom(t) || typeof(Delegate).IsAssignableFrom(t)) return false;
            if (t.Namespace != null && (t.Namespace == "System" || t.Namespace.StartsWith("System."))) return false;
#pragma warning disable SYSLIB0050   // [Serializable] 은 의사 속성 — 형식 플래그로만 읽힌다 (Unity 와 같은 표시)
            return (t.Attributes & TypeAttributes.Serializable) != 0;
#pragma warning restore SYSLIB0050
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
            if (typeof(ScriptableObject).IsAssignableFrom(t)) return "ScriptableObject";
            return "unsupported";
        }

        static object NewElement(FieldMeta m)
        {
            if (m.Kind == "string") return "";
            if (m.Kind == "struct") return Activator.CreateInstance(m.Type, true);
            return m.Type.IsValueType ? Activator.CreateInstance(m.Type) : null;
        }

        // ---------------------------------------------------------------- 형식 설명 (C++ Inspector 가 그린다)
        static void WriteMeta(Utf8JsonWriter w, FieldMeta m, object defaultValue)
        {
            w.WriteString("type", m.Kind);
            if (m.Kind == "unsupported" || m.Kind == "ScriptableObject" || m.Kind == "struct") w.WriteString("typeName", m.Type.Name);
            if (m.Kind == "ScriptableObject") w.WriteString("typeFullName", m.Type.FullName);
            w.WritePropertyName("value");
            WriteValue(w, m, defaultValue);
            if (m.Kind == "enum")
            {
                w.WriteStartArray("options");
                foreach (string n in Enum.GetNames(m.Type)) w.WriteStringValue(Nicify(n));
                w.WriteEndArray();
                w.WriteStartArray("optionValues");
                foreach (object v in Enum.GetValues(m.Type)) w.WriteNumberValue(Convert.ToInt64(v));
                w.WriteEndArray();
            }
            if (m.Kind == "array")
            {
                w.WriteStartObject("element");
                WriteMeta(w, m.Element, NewElement(m.Element));
                w.WriteEndObject();
            }
            if (m.Kind == "struct")
            {
                object inst = defaultValue ?? NewElement(m);
                WriteFieldList(w, "fields", m.Children, inst);
            }
        }

        static void WriteFieldList(Utf8JsonWriter w, string name, List<FieldMeta> fields, object defaults)
        {
            w.WriteStartArray(name);
            foreach (FieldMeta f in fields)
            {
                w.WriteStartObject();
                w.WriteString("name", f.Field.Name);
                w.WriteString("label", Nicify(f.Field.Name));
                object dv = null;
                try { dv = defaults != null ? f.Field.GetValue(defaults) : null; } catch { }
                WriteMeta(w, f, dv);
                if (f.Field.GetCustomAttribute<HeaderAttribute>() is HeaderAttribute h) w.WriteString("header", h.header);
                if (f.Field.GetCustomAttribute<TooltipAttribute>() is TooltipAttribute tip) w.WriteString("tooltip", tip.tooltip);
                if (f.Field.GetCustomAttribute<SpaceAttribute>() is SpaceAttribute sp) w.WriteNumber("space", sp.height);
                if (f.Field.GetCustomAttribute<RangeAttribute>() is RangeAttribute r)
                {
                    w.WriteStartArray("range"); w.WriteNumberValue(r.min); w.WriteNumberValue(r.max); w.WriteEndArray();
                }
                w.WriteEndObject();
            }
            w.WriteEndArray();
        }

        // ---------------------------------------------------------------- 값 → JSON
        internal static void WriteValue(Utf8JsonWriter w, FieldMeta f, object v)
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
                case "ScriptableObject": w.WriteStringValue(v is ScriptableObject so && !so.m_Destroyed ? so.m_AssetPath ?? "" : ""); break;
                case "array":
                    w.WriteStartArray();
                    if (v is IList list)
                        foreach (object e in list) WriteValue(w, f.Element, e);
                    w.WriteEndArray();
                    break;
                case "struct":
                    w.WriteStartObject();
                    object inst = v ?? NewElement(f);
                    foreach (FieldMeta ch in f.Children)
                    {
                        w.WritePropertyName(ch.Field.Name);
                        object cv = null;
                        try { cv = inst != null ? ch.Field.GetValue(inst) : null; } catch { }
                        WriteValue(w, ch, cv);
                    }
                    w.WriteEndObject();
                    break;
                default: w.WriteNullValue(); break;
            }
        }

        // 오브젝트의 직렬화 필드 전부 (ScriptableObject 복제 · CLI)
        internal static string FieldsToJson(object target)
        {
            using var ms = new System.IO.MemoryStream();
            using (var w = new Utf8JsonWriter(ms))
            {
                w.WriteStartObject();
                foreach (FieldMeta f in FieldsOf(target.GetType()))
                {
                    if (f.Kind == "unsupported") continue;
                    w.WritePropertyName(f.Field.Name);
                    WriteValue(w, f, f.Field.GetValue(target));
                }
                w.WriteEndObject();
            }
            return System.Text.Encoding.UTF8.GetString(ms.ToArray());
        }

        // ---------------------------------------------------------------- JSON → 값
        internal static void ApplyFields(object target, string json)
        {
            using var doc = JsonDocument.Parse(json);
            ApplyFields(target, doc.RootElement);
        }

        internal static void ApplyFields(object target, JsonElement root)
        {
            if (root.ValueKind != JsonValueKind.Object) return;
            foreach (FieldMeta f in FieldsOf(target.GetType()))
            {
                if (!root.TryGetProperty(f.Field.Name, out JsonElement e)) continue;
                try
                {
                    object v = ReadValue(f, e, f.Field.GetValue(target));
                    if (v != null || !f.Type.IsValueType) f.Field.SetValue(target, v);
                }
                catch { /* 형식이 바뀐 필드는 기본값 유지 (Unity 와 같음) */ }
            }
        }

        static float F(JsonElement a, int i) => a.ValueKind == JsonValueKind.Array && a.GetArrayLength() > i ? a[i].GetSingle() : 0f;

        static object ReadValue(FieldMeta f, JsonElement e, object current)
        {
            Type t = f.Type;
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
                case "ScriptableObject": return e.ValueKind == JsonValueKind.String ? ScriptableAssets.Load(e.GetString(), t) : null;
                case "array":
                {
                    if (e.ValueKind != JsonValueKind.Array) return current;
                    int n = e.GetArrayLength();
                    IList list;
                    if (f.IsList) { list = (IList)Activator.CreateInstance(t); }
                    else list = Array.CreateInstance(f.Element.Type, n);
                    int i = 0;
                    foreach (JsonElement x in e.EnumerateArray())
                    {
                        object v;
                        try { v = ReadValue(f.Element, x, null); }
                        catch { v = NewElement(f.Element); }
                        if (v == null && f.Element.Type.IsValueType) v = NewElement(f.Element);
                        if (f.IsList) list.Add(v); else list[i] = v;
                        i++;
                    }
                    return list;
                }
                case "struct":
                {
                    if (e.ValueKind != JsonValueKind.Object) return current ?? NewElement(f);
                    object inst = t.IsValueType || current == null ? (current ?? NewElement(f)) : current;
                    foreach (FieldMeta c in f.Children)
                    {
                        if (!e.TryGetProperty(c.Field.Name, out JsonElement ce)) continue;
                        try
                        {
                            object cv = ReadValue(c, ce, c.Field.GetValue(inst));
                            if (cv != null || !c.Type.IsValueType) c.Field.SetValue(inst, cv);
                        }
                        catch { }
                    }
                    return inst;
                }
            }
            return null;
        }
    }
}
