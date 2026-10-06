using System.Runtime.InteropServices;

namespace NovaEngine
{
    internal static unsafe class TilemapNative
    {
        const string Dll = "NovaTilemap";
        [DllImport(Dll)] internal static extern int TM_SetTile(ulong go, int x, int y, byte* tile, int flags);
        [DllImport(Dll)] internal static extern byte* TM_GetTile(ulong go, int x, int y);
        [DllImport(Dll)] internal static extern int TM_GetFlags(ulong go, int x, int y);
        [DllImport(Dll)] internal static extern int TM_BoxFill(ulong go, int x0, int y0, int x1, int y1, byte* tile);
        [DllImport(Dll)] internal static extern int TM_FloodFill(ulong go, int x, int y, byte* tile);
        [DllImport(Dll)] internal static extern void TM_Clear(ulong go);
        [DllImport(Dll)] internal static extern int TM_Count(ulong go);
        [DllImport(Dll)] internal static extern int TM_Bounds(ulong go, int* out4);
        [DllImport(Dll)] internal static extern void TM_CellToWorld(ulong go, int x, int y, int center, float* out3);
        [DllImport(Dll)] internal static extern void TM_WorldToCell(ulong go, float x, float y, float z, int* out3);
        [DllImport(Dll)] internal static extern float TM_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void TM_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern float GRID_Get(ulong go, int prop);
        [DllImport(Dll)] internal static extern void GRID_Set(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern int TMR_GetInt(ulong go, int prop);
        [DllImport(Dll)] internal static extern void TMR_SetInt(ulong go, int prop, int v);

        internal static byte[] Utf8(string s) => System.Text.Encoding.UTF8.GetBytes((s ?? "") + "\0");
        internal static string Read(byte* p) => p == null ? "" : Marshal.PtrToStringUTF8((System.IntPtr)p);
    }

    /// <summary>
    /// Unity 의 Grid: 자식 Tilemap 의 칸 크기 · 간격 (Rectangle 배치).
    /// </summary>
    [NativeComponent("Grid")]
    public sealed unsafe class Grid : Behaviour
    {
        internal Grid() { }
        public Vector3 cellSize
        {
            get => new Vector3(TilemapNative.GRID_Get(nativeId, 0), TilemapNative.GRID_Get(nativeId, 1), TilemapNative.GRID_Get(nativeId, 2));
            set { TilemapNative.GRID_Set(nativeId, 0, value.x); TilemapNative.GRID_Set(nativeId, 1, value.y); TilemapNative.GRID_Set(nativeId, 2, value.z); }
        }
        public Vector3 cellGap
        {
            get => new Vector3(TilemapNative.GRID_Get(nativeId, 3), TilemapNative.GRID_Get(nativeId, 4), TilemapNative.GRID_Get(nativeId, 5));
            set { TilemapNative.GRID_Set(nativeId, 3, value.x); TilemapNative.GRID_Set(nativeId, 4, value.y); TilemapNative.GRID_Set(nativeId, 5, value.z); }
        }
        /// <summary>월드 점 → 칸 (Grid 의 로컬 공간, 첫 자식 Tilemap 이 없으면 Grid 자세로 직접 계산)</summary>
        public Vector3Int WorldToCell(Vector3 world)
        {
            Vector3 l = transform.InverseTransformPoint(world);
            Vector3 c = cellSize, g = cellGap;
            return new Vector3Int(Mathf.FloorToInt(l.x / (c.x + g.x)), Mathf.FloorToInt(l.y / (c.y + g.y)), 0);
        }
        /// <summary>칸 왼쪽 아래 → 월드</summary>
        public Vector3 CellToWorld(Vector3Int cell)
        {
            Vector3 c = cellSize, g = cellGap;
            return transform.TransformPoint(new Vector3(cell.x * (c.x + g.x), cell.y * (c.y + g.y), 0));
        }
        public Vector3 GetCellCenterWorld(Vector3Int cell)
        {
            Vector3 c = cellSize, g = cellGap;
            return transform.TransformPoint(new Vector3(cell.x * (c.x + g.x) + c.x * 0.5f, cell.y * (c.y + g.y) + c.y * 0.5f, 0));
        }
    }
}

namespace NovaEngine.Tilemaps
{
    /// <summary>
    /// 타일 에셋 (.tile) 을 가리킨다. Unity 의 TileBase — Inspector 필드 대신 경로로: new Tile("Assets/Tiles/grass.tile")
    /// </summary>
    public class TileBase
    {
        public readonly string path;
        public TileBase(string path) { this.path = (path ?? "").Replace('/', '\\'); }
        public string name => System.IO.Path.GetFileNameWithoutExtension(path);
        public override bool Equals(object o) => o is TileBase t && string.Equals(t.path, path, System.StringComparison.OrdinalIgnoreCase);
        public override int GetHashCode() => path.ToLowerInvariant().GetHashCode();
        public override string ToString() => name;
    }

    public class Tile : TileBase
    {
        public Tile(string path) : base(path) { }
        public enum ColliderType { None, Sprite, Grid }
    }

    /// <summary>
    /// Unity 의 Tilemap: 칸 (Vector3Int — z 는 쓰지 않는다) 마다 타일. 부모 Grid 의 칸 크기를 따른다.
    /// </summary>
    [NativeComponent("Tilemap")]
    public sealed unsafe class Tilemap : Behaviour
    {
        internal Tilemap() { }

        public void SetTile(Vector3Int position, TileBase tile)
        {
            fixed (byte* p = TilemapNative.Utf8(tile?.path)) TilemapNative.TM_SetTile(nativeId, position.x, position.y, p, 0);
        }
        public void SetTiles(Vector3Int[] positions, TileBase[] tiles)
        {
            for (int i = 0; i < positions.Length && i < tiles.Length; i++) SetTile(positions[i], tiles[i]);
        }
        public TileBase GetTile(Vector3Int position)
        {
            string s = TilemapNative.Read(TilemapNative.TM_GetTile(nativeId, position.x, position.y));
            return s.Length == 0 ? null : new Tile(s);
        }
        public T GetTile<T>(Vector3Int position) where T : TileBase => GetTile(position) as T;
        public bool HasTile(Vector3Int position)
        {
            byte* p = TilemapNative.TM_GetTile(nativeId, position.x, position.y);
            return p != null && *p != 0;
        }
        /// <summary>두 모서리 칸을 포함하는 사각형 채우기 (Unity: position 기준 start..end)</summary>
        public void BoxFill(Vector3Int position, TileBase tile, int startX, int startY, int endX, int endY)
        {
            fixed (byte* p = TilemapNative.Utf8(tile?.path)) TilemapNative.TM_BoxFill(nativeId, position.x + startX, position.y + startY, position.x + endX, position.y + endY, p);
        }
        public void FloodFill(Vector3Int position, TileBase tile)
        {
            fixed (byte* p = TilemapNative.Utf8(tile?.path)) TilemapNative.TM_FloodFill(nativeId, position.x, position.y, p);
        }
        public void ClearAllTiles() => TilemapNative.TM_Clear(nativeId);
        public void DeleteCells(Vector3Int position, int numRows, int numColumns, int numLayers) { }   // Unity 와 이름만 (칸 이동 없음)
        public void CompressBounds() { }   // cellBounds 가 늘 타일이 있는 칸에 맞춰져 있다
        public void RefreshAllTiles() { }
        public void RefreshTile(Vector3Int position) { }
        public int GetUsedTilesCount() => TilemapNative.TM_Count(nativeId);
        /// <summary>타일이 있는 칸의 상자 (max 는 포함하지 않는다 — Unity 와 같다)</summary>
        public BoundsInt cellBounds
        {
            get
            {
                int* b = stackalloc int[4];
                if (TilemapNative.TM_Bounds(nativeId, b) == 0) return new BoundsInt(0, 0, 0, 0, 0, 1);
                return new BoundsInt(b[0], b[1], 0, b[2] - b[0] + 1, b[3] - b[1] + 1, 1);
            }
        }
        public Vector3Int origin => cellBounds.position;
        public Vector3Int size => cellBounds.size;
        public Color color
        {
            get => new Color(TilemapNative.TM_GetFloat(nativeId, 0), TilemapNative.TM_GetFloat(nativeId, 1), TilemapNative.TM_GetFloat(nativeId, 2), TilemapNative.TM_GetFloat(nativeId, 3));
            set { TilemapNative.TM_SetFloat(nativeId, 0, value.r); TilemapNative.TM_SetFloat(nativeId, 1, value.g); TilemapNative.TM_SetFloat(nativeId, 2, value.b); TilemapNative.TM_SetFloat(nativeId, 3, value.a); }
        }
        public Vector3 tileAnchor
        {
            get => new Vector3(TilemapNative.TM_GetFloat(nativeId, 10), TilemapNative.TM_GetFloat(nativeId, 11), TilemapNative.TM_GetFloat(nativeId, 12));
            set { TilemapNative.TM_SetFloat(nativeId, 10, value.x); TilemapNative.TM_SetFloat(nativeId, 11, value.y); TilemapNative.TM_SetFloat(nativeId, 12, value.z); }
        }
        public Grid layoutGrid => GetComponentInParent<Grid>();
        public Vector3Int WorldToCell(Vector3 world)
        {
            int* c = stackalloc int[3];
            TilemapNative.TM_WorldToCell(nativeId, world.x, world.y, world.z, c);
            return new Vector3Int(c[0], c[1], 0);
        }
        public Vector3 CellToWorld(Vector3Int cell)
        {
            float* w = stackalloc float[3];
            TilemapNative.TM_CellToWorld(nativeId, cell.x, cell.y, 0, w);
            return new Vector3(w[0], w[1], w[2]);
        }
        /// <summary>칸 + Tile Anchor (기본 = 칸 가운데)</summary>
        public Vector3 GetCellCenterWorld(Vector3Int cell)
        {
            float* w = stackalloc float[3];
            TilemapNative.TM_CellToWorld(nativeId, cell.x, cell.y, 1, w);
            return new Vector3(w[0], w[1], w[2]);
        }
        public Vector3 GetCellCenterLocal(Vector3Int cell) => transform.InverseTransformPoint(GetCellCenterWorld(cell));
    }

    [NativeComponent("TilemapRenderer")]
    public sealed unsafe class TilemapRenderer : Behaviour
    {
        internal TilemapRenderer() { }
        public int sortingOrder { get => TilemapNative.TMR_GetInt(nativeId, 0); set => TilemapNative.TMR_SetInt(nativeId, 0, value); }
        public int sortingLayerID { get => TilemapNative.TMR_GetInt(nativeId, 1); set => TilemapNative.TMR_SetInt(nativeId, 1, value); }
    }

    /// <summary>콜라이더가 있는 타일 → 상자 (맞닿은 칸은 합친다). 값은 Inspector (Is Trigger · Offset · Material)</summary>
    [NativeComponent("TilemapCollider2D")]
    public sealed class TilemapCollider2D : Behaviour
    {
        internal TilemapCollider2D() { }
    }
}
