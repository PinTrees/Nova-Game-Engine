using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using NovaEngine.Interop;

namespace NovaEngine
{
    // Unity 의 Material · Shader.PropertyToID · MaterialPropertyBlock · Renderer.material / sharedMaterial
    //  (Source/Scene/MaterialScripting.cpp 가 이름으로 내보낸 NovaMat_* 함수)
    internal static unsafe class MaterialNative
    {
        const string Lib = "NovaCore";
        [DllImport(Lib)] internal static extern int NovaMat_Count(ulong id);
        [DllImport(Lib)] internal static extern ulong NovaMat_Get(ulong id, int index, int instance);
        [DllImport(Lib)] internal static extern int NovaMat_Set(ulong id, int index, ulong handle);
        [DllImport(Lib)] internal static extern ulong NovaMat_Clone(ulong source);
        [DllImport(Lib)] internal static extern ulong NovaMat_Load(byte* path);
        [DllImport(Lib)] internal static extern byte* NovaMat_Name(ulong handle);
        [DllImport(Lib)] internal static extern byte* NovaMat_Shader(ulong handle);
        [DllImport(Lib)] internal static extern int NovaMat_SetColor(ulong handle, byte* name, float* rgba);
        [DllImport(Lib)] internal static extern int NovaMat_GetColor(ulong handle, byte* name, float* rgba);
        [DllImport(Lib)] internal static extern int NovaMat_SetFloat(ulong handle, byte* name, float v);
        [DllImport(Lib)] internal static extern int NovaMat_GetFloat(ulong handle, byte* name, float* v);
        [DllImport(Lib)] internal static extern int NovaMat_HasProperty(ulong handle, byte* name);
        [DllImport(Lib)] internal static extern int NovaMat_SetBlock(ulong id, int count, byte* names, float* values, int* colors);
        [DllImport(Lib)] internal static extern int NovaMat_BlockCount(ulong id);
        [DllImport(Lib)] internal static extern byte* NovaMat_BlockEntry(ulong id, int index, float* value, int* color);
    }

    /// <summary>Unity 의 Shader.PropertyToID: 속성 이름 ↔ 번호 (같은 이름 = 같은 번호)</summary>
    public static class Shader
    {
        static readonly Dictionary<string, int> s_Ids = new Dictionary<string, int>();
        static readonly List<string> s_Names = new List<string>();
        public static int PropertyToID(string name)
        {
            name ??= string.Empty;
            if (s_Ids.TryGetValue(name, out int id)) return id;
            id = s_Names.Count;
            s_Names.Add(name);
            s_Ids[name] = id;
            return id;
        }
        internal static string NameOf(int id) => id >= 0 && id < s_Names.Count ? s_Names[id] : string.Empty;
    }

    /// <summary>
    /// Unity 의 Material (URP Lit · Unlit 값의 이름: _BaseColor · _Color · _EmissionColor · _Metallic · _Smoothness · _Cutoff …,
    /// 패키지 · Shader Graph 재질은 그 속성 이름). 색은 감마 (Inspector 에 보이는 값)
    /// </summary>
    public sealed unsafe class Material
    {
        internal ulong m_Handle;
        internal Material(ulong handle) { m_Handle = handle; }
        /// <summary>source 의 사본 (런타임 재질 — 저장되지 않는다)</summary>
        public Material(Material source) { m_Handle = MaterialNative.NovaMat_Clone(source != null ? source.m_Handle : 0); }

        internal static Material FromHandle(ulong h) => h == 0 ? null : new Material(h);
        /// <summary>프로젝트의 .mat 을 읽는다 ("Assets/Materials/Red.mat"). 없으면 null (Unity 의 Resources.Load&lt;Material&gt; 자리)</summary>
        public static Material Load(string path) { fixed (byte* p = Native.Utf8(path)) return FromHandle(MaterialNative.NovaMat_Load(p)); }

        public string name => Native.Str(MaterialNative.NovaMat_Name(m_Handle)) ?? string.Empty;
        public string shaderName => Native.Str(MaterialNative.NovaMat_Shader(m_Handle)) ?? string.Empty;

        /// <summary>_BaseColor (Unity 의 Material.color)</summary>
        public Color color { get => GetColor("_BaseColor"); set => SetColor("_BaseColor", value); }

        public void SetColor(string name, Color value)
        {
            float* c = stackalloc float[4] { value.r, value.g, value.b, value.a };
            fixed (byte* n = Native.Utf8(name)) MaterialNative.NovaMat_SetColor(m_Handle, n, c);
        }
        public void SetColor(int nameID, Color value) => SetColor(Shader.NameOf(nameID), value);
        public Color GetColor(string name)
        {
            float* c = stackalloc float[4] { 0, 0, 0, 0 };
            fixed (byte* n = Native.Utf8(name)) MaterialNative.NovaMat_GetColor(m_Handle, n, c);
            return new Color(c[0], c[1], c[2], c[3]);
        }
        public Color GetColor(int nameID) => GetColor(Shader.NameOf(nameID));
        public void SetVector(string name, Vector4 v) => SetColor(name, new Color(v.x, v.y, v.z, v.w));
        public void SetVector(int nameID, Vector4 v) => SetVector(Shader.NameOf(nameID), v);
        public Vector4 GetVector(string name) { Color c = GetColor(name); return new Vector4(c.r, c.g, c.b, c.a); }
        public Vector4 GetVector(int nameID) => GetVector(Shader.NameOf(nameID));
        public void SetFloat(string name, float value) { fixed (byte* n = Native.Utf8(name)) MaterialNative.NovaMat_SetFloat(m_Handle, n, value); }
        public void SetFloat(int nameID, float value) => SetFloat(Shader.NameOf(nameID), value);
        public float GetFloat(string name) { float v = 0; fixed (byte* n = Native.Utf8(name)) MaterialNative.NovaMat_GetFloat(m_Handle, n, &v); return v; }
        public float GetFloat(int nameID) => GetFloat(Shader.NameOf(nameID));
        public void SetInt(string name, int value) => SetFloat(name, value);
        public int GetInt(string name) => (int)GetFloat(name);
        public bool HasProperty(string name) { fixed (byte* n = Native.Utf8(name)) return MaterialNative.NovaMat_HasProperty(m_Handle, n) != 0; }
        public bool HasProperty(int nameID) => HasProperty(Shader.NameOf(nameID));

        public override bool Equals(object obj) => obj is Material m && m.m_Handle == m_Handle;
        public override int GetHashCode() => m_Handle.GetHashCode();
        public static bool operator ==(Material a, Material b) => ReferenceEquals(a, b) || (a is not null && b is not null && a.m_Handle == b.m_Handle);
        public static bool operator !=(Material a, Material b) => !(a == b);
        public override string ToString() => name;
    }

    /// <summary>
    /// Unity 의 MaterialPropertyBlock: 재질은 공유한 채 렌더러마다 값만 바꾼다 (Renderer.SetPropertyBlock).
    /// NOVA 는 같은 재질 · 같은 블록 값의 렌더러를 한 인스턴싱 묶음으로 그린다 (값이 렌더러마다 다르면 그만큼 묶음이 나뉜다)
    /// </summary>
    public sealed class MaterialPropertyBlock
    {
        internal readonly Dictionary<string, (Vector4 Value, bool Color)> m_Values = new Dictionary<string, (Vector4, bool)>();
        public bool isEmpty => m_Values.Count == 0;
        public void Clear() => m_Values.Clear();
        public void SetColor(string name, Color value) => m_Values[name] = (new Vector4(value.r, value.g, value.b, value.a), true);
        public void SetColor(int nameID, Color value) => SetColor(Shader.NameOf(nameID), value);
        public void SetVector(string name, Vector4 value) => m_Values[name] = (value, true);
        public void SetVector(int nameID, Vector4 value) => SetVector(Shader.NameOf(nameID), value);
        public void SetFloat(string name, float value) => m_Values[name] = (new Vector4(value, 0, 0, 0), false);
        public void SetFloat(int nameID, float value) => SetFloat(Shader.NameOf(nameID), value);
        public void SetInt(string name, int value) => SetFloat(name, value);
        public Color GetColor(string name) => m_Values.TryGetValue(name, out var v) ? new Color(v.Value.x, v.Value.y, v.Value.z, v.Value.w) : new Color(0, 0, 0, 0);
        public Color GetColor(int nameID) => GetColor(Shader.NameOf(nameID));
        public Vector4 GetVector(string name) => m_Values.TryGetValue(name, out var v) ? v.Value : Vector4.zero;
        public float GetFloat(string name) => m_Values.TryGetValue(name, out var v) ? v.Value.x : 0f;
        public float GetFloat(int nameID) => GetFloat(Shader.NameOf(nameID));
        public bool HasProperty(string name) => m_Values.ContainsKey(name);
    }

    public sealed unsafe partial class MeshRenderer
    {
        /// <summary>첫 재질 칸. 처음 읽으면 이 렌더러만의 사본으로 바꾼다 (다른 렌더러는 그대로 — Unity 와 같음, 사본은 씬에 저장되지 않는다)</summary>
        public Material material
        {
            get => Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, 0, 1));
            set { if (value != null) MaterialNative.NovaMat_Set(m_Id, 0, value.m_Handle); }
        }
        /// <summary>첫 재질 칸의 공유 재질 (바꾸면 이 재질을 쓰는 모든 렌더러가 바뀐다)</summary>
        public Material sharedMaterial
        {
            get => Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, 0, 0));
            set { if (value != null) MaterialNative.NovaMat_Set(m_Id, 0, value.m_Handle); }
        }
        public Material[] materials
        {
            get { int n = MaterialNative.NovaMat_Count(m_Id); var a = new Material[n]; for (int i = 0; i < n; ++i) a[i] = Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, i, 1)); return a; }
            set { if (value != null) for (int i = 0; i < value.Length; ++i) if (value[i] != null) MaterialNative.NovaMat_Set(m_Id, i, value[i].m_Handle); }
        }
        public Material[] sharedMaterials
        {
            get { int n = MaterialNative.NovaMat_Count(m_Id); var a = new Material[n]; for (int i = 0; i < n; ++i) a[i] = Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, i, 0)); return a; }
            set => materials = value;
        }

        /// <summary>렌더러의 값 덮어쓰기 (null · 빈 블록 = 없앰). 블록은 복사된다 (나중에 블록을 바꾸면 다시 불러야 한다 — Unity 와 같음)</summary>
        public void SetPropertyBlock(MaterialPropertyBlock properties)
        {
            int n = properties?.m_Values.Count ?? 0;
            if (n == 0) { MaterialNative.NovaMat_SetBlock(m_Id, 0, null, null, null); return; }
            var names = new StringBuilder();
            float* values = stackalloc float[n * 4];
            int* colors = stackalloc int[n];
            int i = 0;
            foreach (var kv in properties.m_Values)
            {
                if (i > 0) names.Append('\n');
                names.Append(kv.Key);
                values[i * 4] = kv.Value.Value.x; values[i * 4 + 1] = kv.Value.Value.y; values[i * 4 + 2] = kv.Value.Value.z; values[i * 4 + 3] = kv.Value.Value.w;
                colors[i] = kv.Value.Color ? 1 : 0;
                ++i;
            }
            fixed (byte* p = Native.Utf8(names.ToString())) MaterialNative.NovaMat_SetBlock(m_Id, n, p, values, colors);
        }
        /// <summary>렌더러에 넣은 블록 값을 properties 에 (먼저 비운다)</summary>
        public void GetPropertyBlock(MaterialPropertyBlock properties)
        {
            if (properties == null) return;
            properties.Clear();
            int n = MaterialNative.NovaMat_BlockCount(m_Id);
            float* v = stackalloc float[4];
            for (int i = 0; i < n; ++i)
            {
                int color = 0;
                string name = Native.Str(MaterialNative.NovaMat_BlockEntry(m_Id, i, v, &color)) ?? string.Empty;
                properties.m_Values[name] = (new Vector4(v[0], v[1], v[2], v[3]), color != 0);
            }
        }
        public bool HasPropertyBlock() => MaterialNative.NovaMat_BlockCount(m_Id) > 0;
    }
}
