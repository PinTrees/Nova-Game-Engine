using System.Text;
using NovaEngine.Interop;

namespace NovaEngine.Rendering
{
    /// <summary>Unity 의 ShadowCastingMode (Renderer.shadowCastingMode)</summary>
    public enum ShadowCastingMode { Off = 0, On = 1, TwoSided = 2, ShadowsOnly = 3 }
}

namespace NovaEngine
{
    /// <summary>Unity 의 Bounds: 축 정렬 상자 (중심 + 반 크기)</summary>
    public struct Bounds
    {
        public Vector3 center;
        public Vector3 extents;
        public Bounds(Vector3 center, Vector3 size) { this.center = center; extents = size * 0.5f; }
        public Vector3 size { get => extents * 2f; set => extents = value * 0.5f; }
        public Vector3 min { get => center - extents; set => SetMinMax(value, max); }
        public Vector3 max { get => center + extents; set => SetMinMax(min, value); }
        public void SetMinMax(Vector3 min, Vector3 max) { extents = (max - min) * 0.5f; center = min + extents; }
        public bool Contains(Vector3 p)
        {
            Vector3 a = min, b = max;
            return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y && p.z >= a.z && p.z <= b.z;
        }
        public bool Intersects(Bounds o)
        {
            Vector3 a = min, b = max, c = o.min, d = o.max;
            return a.x <= d.x && b.x >= c.x && a.y <= d.y && b.y >= c.y && a.z <= d.z && b.z >= c.z;
        }
        public void Encapsulate(Vector3 p) => SetMinMax(Vector3.Min(min, p), Vector3.Max(max, p));
        public void Encapsulate(Bounds b) { Encapsulate(b.min); Encapsulate(b.max); }
        public void Expand(float amount) => extents += new Vector3(amount, amount, amount) * 0.5f;
        public override string ToString() => $"Center: {center}, Extents: {extents}";
    }

    /// <summary>
    /// Unity 의 Renderer: MeshRenderer · SkinnedMeshRenderer · SpriteRenderer · LineRenderer · TrailRenderer 의 공통 부분
    /// (GetComponent&lt;Renderer&gt;() 은 이 순서로 찾는다). 재질 · MaterialPropertyBlock 은 Mesh · Skinned 만 — Sprite · Line · Trail 은
    /// 재질이 없어 material 이 null, SetPropertyBlock 은 무시된다 (색은 각자의 color · startColor). 함수는 Source/Scene/MaterialScripting.cpp
    /// </summary>
    public abstract unsafe class Renderer : Component
    {
        internal Renderer() { }
        internal abstract int Kind { get; }   // 0 Mesh, 1 Skinned, 2 Sprite, 3 Line, 4 Trail (네이티브 함수의 kind)

        /// <summary>꺼지면 그리지 않는다 (Inspector 의 체크 상자, 씬에 저장된다)</summary>
        public bool enabled
        {
            get => MaterialNative.NovaRenderer_GetEnabled(m_Id, Kind) != 0;
            set => MaterialNative.NovaRenderer_SetEnabled(m_Id, Kind, value ? 1 : 0);
        }
        public bool isVisible => enabled && gameObject.activeInHierarchy;

        /// <summary>
        /// 월드 상자. Mesh · Skinned 는 컬링이 마지막 프레임에 잰 상자 (Skinned 는 애니메이션 여유를 더해 Unity 보다 조금 크다 —
        /// 같은 프레임에 만든 렌더러는 다음 프레임부터), Sprite 는 그림 사각형
        /// </summary>
        public Bounds bounds
        {
            get
            {
                float* a = stackalloc float[3];
                float* b = stackalloc float[3];
                MaterialNative.NovaRenderer_Bounds(m_Id, Kind, a, b);
                var r = new Bounds();
                r.SetMinMax(new Vector3(a[0], a[1], a[2]), new Vector3(b[0], b[1], b[2]));
                return r;
            }
        }

        /// <summary>그림자를 드리우는지 (Sprite · Line · Trail 은 늘 Off)</summary>
        public Rendering.ShadowCastingMode shadowCastingMode
        {
            get => (Rendering.ShadowCastingMode)MaterialNative.NovaRenderer_GetShadows(m_Id, Kind);
            set => MaterialNative.NovaRenderer_SetShadows(m_Id, Kind, (int)value);
        }

        /// <summary>첫 재질 칸. 처음 읽으면 이 렌더러만의 사본으로 바꾼다 (다른 렌더러는 그대로 — Unity 와 같음, 사본은 씬에 저장되지 않는다)</summary>
        public Material material
        {
            get => Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, Kind, 0, 1));
            set { if (value != null) MaterialNative.NovaMat_Set(m_Id, Kind, 0, value.m_Handle); }
        }
        /// <summary>첫 재질 칸의 공유 재질 (바꾸면 이 재질을 쓰는 모든 렌더러가 바뀐다)</summary>
        public Material sharedMaterial
        {
            get => Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, Kind, 0, 0));
            set { if (value != null) MaterialNative.NovaMat_Set(m_Id, Kind, 0, value.m_Handle); }
        }
        public Material[] materials
        {
            get { int n = MaterialNative.NovaMat_Count(m_Id, Kind); var a = new Material[n]; for (int i = 0; i < n; ++i) a[i] = Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, Kind, i, 1)); return a; }
            set { if (value != null) for (int i = 0; i < value.Length; ++i) if (value[i] != null) MaterialNative.NovaMat_Set(m_Id, Kind, i, value[i].m_Handle); }
        }
        public Material[] sharedMaterials
        {
            get { int n = MaterialNative.NovaMat_Count(m_Id, Kind); var a = new Material[n]; for (int i = 0; i < n; ++i) a[i] = Material.FromHandle(MaterialNative.NovaMat_Get(m_Id, Kind, i, 0)); return a; }
            set => materials = value;
        }

        /// <summary>렌더러의 값 덮어쓰기 (null · 빈 블록 = 없앰). 블록은 복사된다 (나중에 블록을 바꾸면 다시 불러야 한다 — Unity 와 같음)</summary>
        public void SetPropertyBlock(MaterialPropertyBlock properties) => SetPropertyBlock(properties, -1);
        /// <summary>재질 칸 하나만 덮어쓰기: 그 칸은 렌더러 블록 위에 덮인다 (같은 이름이면 칸 쪽). null · 빈 블록 = 그 칸 블록을 없앰.
        /// 칸 블록이 있는 렌더러는 GPU 인스턴싱 속성 대신 파생 재질로 그린다</summary>
        public void SetPropertyBlock(MaterialPropertyBlock properties, int materialIndex)
        {
            if (materialIndex < -1) return;
            int n = properties?.m_Values.Count ?? 0;
            if (n == 0) { MaterialNative.NovaMat_SetBlock(m_Id, Kind, materialIndex, 0, null, null, null); return; }
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
            fixed (byte* p = Native.Utf8(names.ToString())) MaterialNative.NovaMat_SetBlock(m_Id, Kind, materialIndex, n, p, values, colors);
        }
        /// <summary>렌더러에 넣은 블록 값을 properties 에 (먼저 비운다)</summary>
        public void GetPropertyBlock(MaterialPropertyBlock properties) => GetPropertyBlock(properties, -1);
        /// <summary>재질 칸 블록의 값 (렌더러 블록과 합치지 않은, 그 칸에 넣은 값만)</summary>
        public void GetPropertyBlock(MaterialPropertyBlock properties, int materialIndex)
        {
            if (properties == null) return;
            properties.Clear();
            int n = MaterialNative.NovaMat_BlockCount(m_Id, Kind, materialIndex);
            float* v = stackalloc float[4];
            for (int i = 0; i < n; ++i)
            {
                int color = 0;
                string name = Native.Str(MaterialNative.NovaMat_BlockEntry(m_Id, Kind, materialIndex, i, v, &color)) ?? string.Empty;
                properties.m_Values[name] = (new Vector4(v[0], v[1], v[2], v[3]), color != 0);
            }
        }
        /// <summary>렌더러 블록이나 재질 칸 블록이 하나라도 있는지</summary>
        public bool HasPropertyBlock() => MaterialNative.NovaMat_HasBlock(m_Id, Kind) != 0;

        /// <summary>투명 순서: Sorting Layer → Order in Layer → 거리 (Sprite · 투명 재질 Mesh · Line · Trail — 불투명에는 영향 없음, Unity 와 같음)</summary>
        public int sortingOrder
        {
            get => MaterialNative.NovaRenderer_GetSorting(m_Id, Kind, 1);
            set => MaterialNative.NovaRenderer_SetSorting(m_Id, Kind, 1, value);
        }
        /// <summary>Sorting Layer id (Project Settings > Tags and Layers, Default = 0)</summary>
        public int sortingLayerID
        {
            get => MaterialNative.NovaRenderer_GetSorting(m_Id, Kind, 0);
            set => MaterialNative.NovaRenderer_SetSorting(m_Id, Kind, 0, value);
        }
        /// <summary>Sorting Layer 이름. 없는 이름은 무시</summary>
        public string sortingLayerName
        {
            get => Native.Str(MaterialNative.NovaRenderer_SortingLayerName(sortingLayerID)) ?? "Default";
            set
            {
                int id;
                fixed (byte* p = Native.Utf8(value)) id = MaterialNative.NovaRenderer_SortingLayerId(p);
                if (id >= 0) sortingLayerID = id;
            }
        }
    }
}
