using System.Runtime.InteropServices;

namespace NovaEngine
{
    // Unity 의 LineRenderer · TrailRenderer (Source/Effects/LineRenderer.cpp 가 이름으로 내보낸 NovaLine_* · NovaTrail_* 함수)
    public enum LineAlignment { View = 0, TransformZ = 1 }
    public enum LineTextureMode { Stretch = 0, Tile = 1, DistributePerSegment = 2, RepeatPerSegment = 3 }

    internal static unsafe class LineNative
    {
        const string Lib = "NovaCore";
        [DllImport(Lib)] internal static extern float NovaLine_GetFloat(ulong id, int kind, int prop);
        [DllImport(Lib)] internal static extern void NovaLine_SetFloat(ulong id, int kind, int prop, float value);
        [DllImport(Lib)] internal static extern void NovaLine_GetColor(ulong id, int kind, int which, float* rgba);
        [DllImport(Lib)] internal static extern void NovaLine_SetColor(ulong id, int kind, int which, float* rgba);
        [DllImport(Lib)] internal static extern int NovaLine_GetCount(ulong id, int kind);
        [DllImport(Lib)] internal static extern void NovaLine_SetCount(ulong id, int count);
        [DllImport(Lib)] internal static extern void NovaLine_GetPosition(ulong id, int kind, int index, float* xyz);
        [DllImport(Lib)] internal static extern void NovaLine_SetPosition(ulong id, int kind, int index, float* xyz);
        [DllImport(Lib)] internal static extern void NovaLine_SetPositions(ulong id, float* xyz, int count);
        [DllImport(Lib)] internal static extern void NovaTrail_Clear(ulong id);
        [DllImport(Lib)] internal static extern void NovaTrail_AddPosition(ulong id, float* xyz);
    }

    /// <summary>Line · Trail Renderer 가 같이 쓰는 값 (너비 · 색 · 꺾인 곳 · 끝 · 방향 · 텍스처)</summary>
    public abstract unsafe class LineRendererCommon : Component
    {
        internal abstract int Kind { get; }
        internal float GetF(int prop) => LineNative.NovaLine_GetFloat(m_Id, Kind, prop);
        internal void SetF(int prop, float v) => LineNative.NovaLine_SetFloat(m_Id, Kind, prop, v);
        Color GetC(int which) { float* c = stackalloc float[4]; LineNative.NovaLine_GetColor(m_Id, Kind, which, c); return new Color(c[0], c[1], c[2], c[3]); }
        void SetC(int which, Color v) { float* c = stackalloc float[4]; c[0] = v.r; c[1] = v.g; c[2] = v.b; c[3] = v.a; LineNative.NovaLine_SetColor(m_Id, Kind, which, c); }

        public bool enabled { get => GetF(11) != 0; set => SetF(11, value ? 1 : 0); }
        public float widthMultiplier { get => GetF(0); set => SetF(0, value); }
        public float startWidth { get => GetF(1); set => SetF(1, value); }
        public float endWidth { get => GetF(2); set => SetF(2, value); }
        public Color startColor { get => GetC(0); set => SetC(0, value); }
        public Color endColor { get => GetC(1); set => SetC(1, value); }
        public int numCornerVertices { get => (int)GetF(5); set => SetF(5, value); }
        public int numCapVertices { get => (int)GetF(6); set => SetF(6, value); }
        public LineAlignment alignment { get => (LineAlignment)(int)GetF(7); set => SetF(7, (int)value); }
        public LineTextureMode textureMode { get => (LineTextureMode)(int)GetF(8); set => SetF(8, (int)value); }
        public int positionCount { get => LineNative.NovaLine_GetCount(m_Id, Kind); set { if (Kind == 0) LineNative.NovaLine_SetCount(m_Id, value); } }
        public Vector3 GetPosition(int index) { float* p = stackalloc float[3]; LineNative.NovaLine_GetPosition(m_Id, Kind, index, p); return new Vector3(p[0], p[1], p[2]); }
        public void SetPosition(int index, Vector3 position) { float* p = stackalloc float[3]; p[0] = position.x; p[1] = position.y; p[2] = position.z; LineNative.NovaLine_SetPosition(m_Id, Kind, index, p); }
        /// <summary>positions 에 점을 채우고 개수를 돌려준다 (Unity 와 같음)</summary>
        public int GetPositions(Vector3[] positions)
        {
            int n = positionCount;
            if (positions == null) return 0;
            n = System.Math.Min(n, positions.Length);
            for (int i = 0; i < n; ++i) positions[i] = GetPosition(i);
            return n;
        }
    }

    [NativeComponent("LineRenderer")]
    public sealed unsafe class LineRenderer : LineRendererCommon
    {
        internal LineRenderer() { }
        internal override int Kind => 0;
        public bool loop { get => GetF(3) != 0; set => SetF(3, value ? 1 : 0); }
        public bool useWorldSpace { get => GetF(4) != 0; set => SetF(4, value ? 1 : 0); }
        /// <summary>점을 모두 바꾼다 (positionCount 도 그 수로)</summary>
        public void SetPositions(Vector3[] positions)
        {
            int n = positions?.Length ?? 0;
            float[] xyz = new float[n * 3];
            for (int i = 0; i < n; ++i) { xyz[i * 3] = positions[i].x; xyz[i * 3 + 1] = positions[i].y; xyz[i * 3 + 2] = positions[i].z; }
            fixed (float* p = xyz) LineNative.NovaLine_SetPositions(m_Id, p, n);
        }
    }

    [NativeComponent("TrailRenderer")]
    public sealed unsafe class TrailRenderer : LineRendererCommon
    {
        internal TrailRenderer() { }
        internal override int Kind => 1;
        /// <summary>점이 사라지기까지 (초)</summary>
        public float time { get => GetF(3); set => SetF(3, value); }
        public float minVertexDistance { get => GetF(4); set => SetF(4, value); }
        public bool emitting { get => GetF(9) != 0; set => SetF(9, value ? 1 : 0); }
        public bool autodestruct { get => GetF(10) != 0; set => SetF(10, value ? 1 : 0); }
        /// <summary>꼬리를 모두 지운다 (순간 이동 뒤 등)</summary>
        public void Clear() => LineNative.NovaTrail_Clear(m_Id);
        public void AddPosition(Vector3 position) { float* p = stackalloc float[3]; p[0] = position.x; p[1] = position.y; p[2] = position.z; LineNative.NovaTrail_AddPosition(m_Id, p); }
        public void AddPositions(Vector3[] positions) { if (positions != null) foreach (var v in positions) AddPosition(v); }
    }
}
