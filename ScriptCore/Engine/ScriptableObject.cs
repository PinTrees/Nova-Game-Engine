using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using NovaEngine.Interop;

namespace NovaEngine
{
    /// <summary>Unity 의 CreateAssetMenu: Project 창 Create 메뉴에 이 ScriptableObject 에셋 만들기를 더한다</summary>
    [AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
    public sealed class CreateAssetMenuAttribute : Attribute
    {
        /// <summary>Create 메뉴 경로 ("Game/Weapon Data"). 비우면 클래스 이름</summary>
        public string menuName { get; set; }
        /// <summary>새 에셋 파일 이름 (비우면 "New 클래스")</summary>
        public string fileName { get; set; }
        /// <summary>메뉴 안 순서 (작을수록 위)</summary>
        public int order { get; set; }
    }

    /// <summary>
    /// Unity 의 ScriptableObject — 씬과 따로 저장하는 데이터 에셋 (.asset). 무기 · 아이템 · 적 스탯 · 설정.
    /// 같은 에셋을 가리키는 필드는 모두 같은 인스턴스를 받는다. 메시지: Awake · OnEnable (읽을 때), OnDisable · OnDestroy, OnValidate (Inspector 편집)
    /// </summary>
    public class ScriptableObject : Object
    {
        internal string m_AssetPath;   // "Assets/Data/Sword.asset" (CreateInstance 로 만든 것은 null)
        internal string m_Name = string.Empty;
        internal bool m_Destroyed;

        public ScriptableObject() { }

        internal override bool IsAlive() => !m_Destroyed;
        public override string name { get => m_Name; set => m_Name = value ?? string.Empty; }
        public override bool Equals(object other) => ReferenceEquals(this, other);
        public override int GetHashCode() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);

        public static T CreateInstance<T>() where T : ScriptableObject => (T)CreateInstance(typeof(T));

        public static ScriptableObject CreateInstance(Type type)
        {
            if (type == null || !typeof(ScriptableObject).IsAssignableFrom(type) || type.IsAbstract)
            {
                Debug.LogError("Instance of " + (type?.Name ?? "null") + " couldn't be created. The script class needs to derive from ScriptableObject and can't be abstract.");
                return null;
            }
            var so = (ScriptableObject)Activator.CreateInstance(type, true);
            ScriptableAssets.Send(so, "Awake");
            ScriptableAssets.Send(so, "OnEnable");
            return so;
        }

        public static ScriptableObject CreateInstance(string className) => CreateInstance(Bridge.FindScriptableType(className));

        internal void DestroyInternal()
        {
            if (m_Destroyed) return;
            ScriptableAssets.Send(this, "OnDisable");
            ScriptableAssets.Send(this, "OnDestroy");
            m_Destroyed = true;
            ScriptableAssets.Forget(this);
        }
    }

    /// <summary>Unity 의 TextAsset (.txt · .json · .csv · .bytes …) — Resources.Load 로 읽는다</summary>
    public sealed class TextAsset : Object
    {
        string m_Path;
        string m_Text;
        byte[] m_Bytes;
        public TextAsset() : this(string.Empty) { }
        public TextAsset(string text) { m_Text = text ?? string.Empty; }
        internal static TextAsset FromFile(string path) => new TextAsset { m_Text = null, m_Path = path };
        internal override bool IsAlive() => true;
        public override string name { get => Path.GetFileNameWithoutExtension(m_Path ?? string.Empty); set { } }
        public byte[] bytes => m_Bytes ??= m_Path != null ? (ScriptableAssets.ReadBytes(m_Path) ?? Array.Empty<byte>()) : System.Text.Encoding.UTF8.GetBytes(m_Text ?? string.Empty);
        public string text => m_Text ??= System.Text.Encoding.UTF8.GetString(bytes).TrimStart('﻿');
        public long dataSize => bytes.LongLength;
        public override string ToString() => text;
        public override bool Equals(object other) => ReferenceEquals(this, other);
        public override int GetHashCode() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);
    }

    /// <summary>
    /// Unity 의 Resources: 프로젝트의 아무 "Resources" 폴더 안 에셋을 경로 (확장자 없이, 그 폴더 기준) 로 읽는다.
    /// ScriptableObject (.asset) · TextAsset · Material · AudioClip. 빌드는 Resources 폴더를 통째로 넣는다
    /// </summary>
    public static class Resources
    {
        public static T Load<T>(string path) where T : class => Load(path, typeof(T)) as T;
        public static Object Load(string path) => Load(path, typeof(Object)) as Object;

        public static object Load(string path, Type type)
        {
            foreach (string file in ScriptableAssets.FindResources(path, false))
            {
                object o = ScriptableAssets.LoadAny(file, type);
                if (o != null) return o;
            }
            return null;
        }

        public static T[] LoadAll<T>(string path) where T : class
        {
            var list = new List<T>();
            foreach (string file in ScriptableAssets.FindResources(path, true))
                if (ScriptableAssets.LoadAny(file, typeof(T)) is T t) list.Add(t);
            return list.ToArray();
        }

        public static Object[] LoadAll(string path) => LoadAll<Object>(path);

        /// <summary>Unity 와 같은 이름 — 읽은 데이터 에셋을 놓는다 (다시 Load 하면 파일에서 새로)</summary>
        public static void UnloadAsset(Object assetToUnload)
        {
            if (assetToUnload is ScriptableObject so) ScriptableAssets.Forget(so);
        }
    }
}

namespace NovaEngine.Interop
{
    internal static unsafe class AssetNative
    {
        const string Lib = "NovaCore";
        [DllImport(Lib)] internal static extern byte* NovaAsset_Resolve(byte* path);              // 절대 경로 ("" = 없음)
        [DllImport(Lib)] internal static extern byte* NovaAsset_FindResources(byte* path, int all); // "\n" 로 이은 상대 경로
    }

    // 데이터 에셋 (.asset) 읽기 · 캐시: 경로 하나에 인스턴스 하나 (Unity 처럼 같은 에셋 = 같은 오브젝트)
    internal static unsafe class ScriptableAssets
    {
        static readonly Dictionary<string, ScriptableObject> s_Cache = new Dictionary<string, ScriptableObject>(StringComparer.OrdinalIgnoreCase);
        static readonly HashSet<string> s_Warned = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        internal static string Normalize(string p) => (p ?? string.Empty).Replace('\\', '/').TrimStart('/');

        internal static string Resolve(string path)
        {
            fixed (byte* p = Native.Utf8(Normalize(path))) { string r = Native.Str(AssetNative.NovaAsset_Resolve(p)); return string.IsNullOrEmpty(r) ? null : r; }
        }

        internal static byte[] ReadBytes(string path)
        {
            string full = Resolve(path);
            try { return full != null ? File.ReadAllBytes(full) : null; } catch { return null; }
        }

        internal static IEnumerable<string> FindResources(string path, bool all)
        {
            string list;
            fixed (byte* p = Native.Utf8(Normalize(path ?? string.Empty))) list = Native.Str(AssetNative.NovaAsset_FindResources(p, all ? 1 : 0)) ?? string.Empty;
            return list.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        }

        static readonly HashSet<string> kText = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".txt", ".json", ".csv", ".bytes", ".xml", ".html", ".htm", ".yaml", ".yml", ".md", ".fnt" };
        static readonly HashSet<string> kAudio = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".wav", ".ogg", ".mp3", ".flac" };

        // Resources.Load: 파일 종류와 바라는 형식이 맞으면 그 오브젝트
        internal static object LoadAny(string file, Type type)
        {
            string ext = Path.GetExtension(file);
            type ??= typeof(object);
            if (ext.Equals(".asset", StringComparison.OrdinalIgnoreCase) && (type.IsAssignableFrom(typeof(ScriptableObject)) || typeof(ScriptableObject).IsAssignableFrom(type)))
            {
                ScriptableObject so = Load(file, null, quiet: true);
                return so != null && type.IsInstanceOfType(so) ? so : null;
            }
            if (kText.Contains(ext) && type.IsAssignableFrom(typeof(TextAsset))) return TextAsset.FromFile(file);
            if (kAudio.Contains(ext) && type.IsAssignableFrom(typeof(AudioClip))) return new AudioClip(file);
            if (ext.Equals(".mat", StringComparison.OrdinalIgnoreCase) && type.IsAssignableFrom(typeof(Material))) return Material.Load(file);
            return null;
        }

        internal static ScriptableObject Load(string path, Type expected, bool quiet = false)
        {
            if (string.IsNullOrEmpty(path)) return null;
            string key = Normalize(path);
            if (s_Cache.TryGetValue(key, out ScriptableObject so) && !so.m_Destroyed)
                return expected == null || expected.IsInstanceOfType(so) ? so : null;
            byte[] data = ReadBytes(key);
            if (data == null)
            {
                if (!quiet && s_Warned.Add(key)) Debug.LogWarning("ScriptableObject asset not found: " + key);
                return null;
            }
            using var doc = JsonDocument.Parse(data);
            JsonElement root = doc.RootElement;
            string cls = root.TryGetProperty("class", out JsonElement c) && c.ValueKind == JsonValueKind.String ? c.GetString() : null;
            Type t = Bridge.FindScriptableType(cls);
            if (t == null || t.IsAbstract)
            {
                if (!quiet && s_Warned.Add(key)) Debug.LogWarning("The ScriptableObject class '" + cls + "' of " + key + " was not found (renamed or a compile error?)");
                return null;
            }
            if (expected != null && !expected.IsAssignableFrom(t)) return null;
            so = (ScriptableObject)Activator.CreateInstance(t, true);
            so.m_AssetPath = key;
            so.m_Name = Path.GetFileNameWithoutExtension(key);
            s_Cache[key] = so;   // 필드를 채우기 전에 — 서로 가리키는 에셋 (A ↔ B) 도 같은 인스턴스로
            if (root.TryGetProperty("fields", out JsonElement fields)) Bridge.ApplyFields(so, fields);
            Send(so, "Awake");
            Send(so, "OnEnable");
            return so;
        }

        // Inspector 에서 고친 에셋: 이미 읽은 인스턴스에 새 값 (Play 중 조정)
        internal static void Reload(string path)
        {
            string key = Normalize(path);
            if (!s_Cache.TryGetValue(key, out ScriptableObject so) || so.m_Destroyed) return;
            byte[] data = ReadBytes(key);
            if (data == null) return;
            using var doc = JsonDocument.Parse(data);
            if (doc.RootElement.TryGetProperty("fields", out JsonElement fields)) Bridge.ApplyFields(so, fields);
            Send(so, "OnValidate");
        }

        internal static void Forget(ScriptableObject so)
        {
            if (so?.m_AssetPath != null && s_Cache.TryGetValue(so.m_AssetPath, out var cur) && ReferenceEquals(cur, so)) s_Cache.Remove(so.m_AssetPath);
        }

        internal static void Clear()
        {
            foreach (ScriptableObject so in new List<ScriptableObject>(s_Cache.Values))
                if (!so.m_Destroyed) Send(so, "OnDisable");
            s_Cache.Clear();
            s_Warned.Clear();
        }

        // Instantiate(에셋): 같은 값의 새 인스턴스 (저장되지 않는다)
        internal static ScriptableObject Clone(ScriptableObject original)
        {
            var so = (ScriptableObject)Activator.CreateInstance(original.GetType(), true);
            Bridge.ApplyFields(so, Bridge.FieldsToJson(original));
            so.m_Name = original.name + "(Clone)";
            Send(so, "Awake");
            Send(so, "OnEnable");
            return so;
        }

        internal static void Send(ScriptableObject so, string message)
        {
            for (Type t = so.GetType(); t != null && t != typeof(ScriptableObject); t = t.BaseType)
            {
                MethodInfo mi = t.GetMethod(message, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, null, Type.EmptyTypes, null);
                if (mi == null) continue;
                try { mi.Invoke(so, null); }
                catch (TargetInvocationException e) { Debug.LogException(e.InnerException ?? e); }
                return;
            }
        }
    }
}
