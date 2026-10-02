using System;
using System.Collections.Generic;
using NovaEngine.Interop;

namespace NovaEngine
{
    // Unity 의 RectTransform (UI 요소의 위치/크기). Transform 을 상속한다: (RectTransform)transform 이 동작
    public class RectTransform : Transform
    {
        internal RectTransform() { }
        internal RectTransform(ulong id) : base(id) { }
        unsafe Vector2 Get2(int p) { Vector4 v; Native.Api.UI_GetVec(m_Id, p, &v); return new Vector2(v.x, v.y); }
        unsafe void Set2(int p, Vector2 value) { Vector4 v = new Vector4(value.x, value.y, 0, 0); Native.Api.UI_SetVec(m_Id, p, &v); }

        public Vector2 anchoredPosition { get => Get2(0); set => Set2(0, value); }
        public Vector2 sizeDelta { get => Get2(1); set => Set2(1, value); }
        public Vector2 anchorMin { get => Get2(2); set => Set2(2, value); }
        public Vector2 anchorMax { get => Get2(3); set => Set2(3, value); }
        public Vector2 pivot { get => Get2(4); set => Set2(4, value); }
        public Vector2 offsetMin { get => Get2(6); set => Set2(6, value); }
        public Vector2 offsetMax { get => Get2(7); set => Set2(7, value); }
        public Vector3 anchoredPosition3D
        {
            get { var p = anchoredPosition; return new Vector3(p.x, p.y, localPosition.z); }
            set { anchoredPosition = new Vector2(value.x, value.y); var lp = localPosition; lp.z = value.z; localPosition = lp; }
        }
        public unsafe Rect rect { get { Vector4 v; Native.Api.UI_GetVec(m_Id, 5, &v); return new Rect(v.x, v.y, v.z, v.w); } }
        public void SetSizeWithCurrentAnchors(Axis axis, float size)
        {
            var sd = sizeDelta;
            var r = rect;
            if (axis == Axis.Horizontal) sd.x += size - r.width; else sd.y += size - r.height;
            sizeDelta = sd;
        }
        public enum Axis { Horizontal = 0, Vertical = 1 }
    }

    public struct Rect
    {
        public float x, y, width, height;
        public Rect(float x, float y, float width, float height) { this.x = x; this.y = y; this.width = width; this.height = height; }
        public Vector2 position => new Vector2(x, y);
        public Vector2 size => new Vector2(width, height);
        public Vector2 center => new Vector2(x + width * 0.5f, y + height * 0.5f);
        public float xMin => x; public float yMin => y; public float xMax => x + width; public float yMax => y + height;
        public Vector2 min => new Vector2(x, y); public Vector2 max => new Vector2(x + width, y + height);
        public bool Contains(Vector2 p) => p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
        public override string ToString() => $"(x:{x:F2}, y:{y:F2}, width:{width:F2}, height:{height:F2})";
    }

    public enum RenderMode { ScreenSpaceOverlay = 0, ScreenSpaceCamera = 1, WorldSpace = 2 }

    public sealed class Canvas : Behaviour
    {
        internal Canvas() { }
        public unsafe int sortingOrder
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 50, &v); return (int)v.x; }
            set { Vector4 v = new Vector4(value, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 50, &v); }
        }
        public unsafe float scaleFactor { get { Vector4 v; Native.Api.UI_GetVec(m_Id, 51, &v); return v.x; } }
        public unsafe RenderMode renderMode
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 52, &v); return (RenderMode)(int)v.x; }
            set { Vector4 v = new Vector4((int)value, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 52, &v); }
        }
        /// <summary>Screen Space - Camera 의 Render Camera / World Space 의 Event Camera</summary>
        public unsafe Camera worldCamera
        {
            // fileID 64 비트 = float 두 개의 비트 그대로
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 53, &v); ulong id = ((ulong)(uint)System.BitConverter.SingleToInt32Bits(v.y) << 32) | (uint)System.BitConverter.SingleToInt32Bits(v.x); return id == 0 ? null : new GameObject(id).GetComponent<Camera>(); }
            set { ulong id = value == null ? 0 : value.gameObject.GetInstanceID(); Vector4 v = new Vector4(System.BitConverter.Int32BitsToSingle((int)(uint)(id & 0xFFFFFFFF)), System.BitConverter.Int32BitsToSingle((int)(uint)(id >> 32)), 0, 0); Native.Api.UI_SetVec(m_Id, 53, &v); }
        }
        public unsafe float planeDistance
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 54, &v); return v.x; }
            set { Vector4 v = new Vector4(value, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 54, &v); }
        }
    }

    // Unity 의 Sprite: 이 엔진에서는 텍스처 경로 (Assets\..., 또는 "builtin:UISprite" 등)
    public sealed class Sprite : Object
    {
        internal string m_Path;
        internal Sprite(string path) { m_Path = path; }
        public static Sprite FromPath(string path) => string.IsNullOrEmpty(path) ? null : new Sprite(path);
        public override string name { get => m_Path == null ? "" : m_Path.StartsWith("builtin:") ? m_Path.Substring(8) : System.IO.Path.GetFileNameWithoutExtension(m_Path); set { } }
        internal override bool IsAlive() => !string.IsNullOrEmpty(m_Path);
        public override bool Equals(object o) => o is Sprite s && s.m_Path == m_Path;
        public override int GetHashCode() => (m_Path ?? "").GetHashCode();
    }

    public enum TextAnchor { UpperLeft = 0, UpperCenter, UpperRight, MiddleLeft, MiddleCenter, MiddleRight, LowerLeft, LowerCenter, LowerRight }
    public enum FontStyle { Normal = 0, Bold = 1, Italic = 2, BoldAndItalic = 3 }
}

namespace NovaEngine.Events
{
    public delegate void UnityAction();
    public delegate void UnityAction<T0>(T0 arg0);

    // Unity 의 UnityEvent (AddListener / RemoveListener / Invoke)
    public class UnityEvent
    {
        readonly List<UnityAction> m_Calls = new List<UnityAction>();
        public void AddListener(UnityAction call) { if (call != null) m_Calls.Add(call); }
        public void RemoveListener(UnityAction call) => m_Calls.Remove(call);
        public void RemoveAllListeners() => m_Calls.Clear();
        public int GetPersistentEventCount() => 0;
        public void Invoke()
        {
            foreach (var c in m_Calls.ToArray())
            {
                try { c(); }
                catch (Exception e) { Debug.LogException(e); }
            }
        }
    }

    public class UnityEvent<T0>
    {
        readonly List<UnityAction<T0>> m_Calls = new List<UnityAction<T0>>();
        public void AddListener(UnityAction<T0> call) { if (call != null) m_Calls.Add(call); }
        public void RemoveListener(UnityAction<T0> call) => m_Calls.Remove(call);
        public void RemoveAllListeners() => m_Calls.Clear();
        public void Invoke(T0 arg)
        {
            foreach (var c in m_Calls.ToArray())
            {
                try { c(arg); }
                catch (Exception e) { Debug.LogException(e); }
            }
        }
    }
}

namespace NovaEngine.UI
{
    using NovaEngine.Events;

    // C# 래퍼는 GetComponent 마다 새로 만들어지므로 리스너는 GameObject 별로 여기에 둔다
    internal static class UIEvents
    {
        static readonly Dictionary<ulong, Button.ButtonClickedEvent> s_Click = new Dictionary<ulong, Button.ButtonClickedEvent>();
        static readonly Dictionary<ulong, Slider.SliderEvent> s_Slider = new Dictionary<ulong, Slider.SliderEvent>();
        static readonly Dictionary<ulong, Toggle.ToggleEvent> s_Toggle = new Dictionary<ulong, Toggle.ToggleEvent>();
        static readonly Dictionary<ulong, InputField.OnChangeEvent> s_InputChanged = new Dictionary<ulong, InputField.OnChangeEvent>();
        static readonly Dictionary<ulong, InputField.EndEditEvent> s_InputEnd = new Dictionary<ulong, InputField.EndEditEvent>();
        static readonly Dictionary<ulong, Text.LinkClickedEvent> s_Link = new Dictionary<ulong, Text.LinkClickedEvent>();
        static readonly Dictionary<ulong, Scrollbar.ScrollEvent> s_Scrollbar = new Dictionary<ulong, Scrollbar.ScrollEvent>();
        static readonly Dictionary<ulong, Dropdown.DropdownEvent> s_Dropdown = new Dictionary<ulong, Dropdown.DropdownEvent>();

        static T Get<T>(Dictionary<ulong, T> d, ulong id) where T : new()
        {
            if (!d.TryGetValue(id, out var e)) d[id] = e = new T();
            return e;
        }
        internal static Button.ButtonClickedEvent Click(ulong id) => Get(s_Click, id);
        internal static Slider.SliderEvent SliderChanged(ulong id) => Get(s_Slider, id);
        internal static Toggle.ToggleEvent ToggleChanged(ulong id) => Get(s_Toggle, id);
        internal static InputField.OnChangeEvent InputChanged(ulong id) => Get(s_InputChanged, id);
        internal static InputField.EndEditEvent InputEnd(ulong id) => Get(s_InputEnd, id);
        internal static Text.LinkClickedEvent LinkClicked(ulong id) => Get(s_Link, id);
        internal static Scrollbar.ScrollEvent ScrollbarChanged(ulong id) => Get(s_Scrollbar, id);
        internal static Dropdown.DropdownEvent DropdownChanged(ulong id) => Get(s_Dropdown, id);

        internal static void Invoke(ulong id, int kind, float number, string text)
        {
            switch (kind)
            {
                case 0: if (s_Click.TryGetValue(id, out var c)) c.Invoke(); break;
                case 1: if (s_Slider.TryGetValue(id, out var s)) s.Invoke(number); break;
                case 2: if (s_Toggle.TryGetValue(id, out var t)) t.Invoke(number != 0); break;
                case 3: if (s_InputChanged.TryGetValue(id, out var ic)) ic.Invoke(text); break;
                case 4: if (s_InputEnd.TryGetValue(id, out var ie)) ie.Invoke(text); break;
                case 5: if (s_Link.TryGetValue(id, out var lk)) lk.Invoke(text); break;
                case 6: if (s_Scrollbar.TryGetValue(id, out var sb)) sb.Invoke(number); break;
                case 7: if (s_Dropdown.TryGetValue(id, out var dd)) dd.Invoke((int)number); break;
            }
        }
        internal static void Clear()
        {
            s_Click.Clear(); s_Slider.Clear(); s_Toggle.Clear(); s_InputChanged.Clear(); s_InputEnd.Clear(); s_Link.Clear(); s_Scrollbar.Clear(); s_Dropdown.Clear();
        }
    }

    public abstract class Graphic : Behaviour
    {
        internal Graphic() { }
        protected unsafe Vector4 GetV(int p) { Vector4 v; Native.Api.UI_GetVec(m_Id, p, &v); return v; }
        protected unsafe void SetV(int p, Vector4 v) => Native.Api.UI_SetVec(m_Id, p, &v);
        protected unsafe string GetS(int p) => Native.Str(Native.Api.UI_GetString(m_Id, p));
        protected unsafe void SetS(int p, string s) { fixed (byte* b = Native.Utf8(s)) Native.Api.UI_SetString(m_Id, p, b); }

        // 같은 GameObject 의 Image 와 Text 를 구분하는 번호 (0 Image, 1 Text)
        internal abstract int GraphicKind { get; }
        public Color color
        {
            get { var v = GetV(10 + GraphicKind * 100); return new Color(v.x, v.y, v.z, v.w); }
            set => SetV(10 + GraphicKind * 100, new Vector4(value.r, value.g, value.b, value.a));
        }
        public bool raycastTarget
        {
            get => GetV(11 + GraphicKind * 100).x != 0;
            set => SetV(11 + GraphicKind * 100, new Vector4(value ? 1 : 0, 0, 0, 0));
        }
        public override bool enabled
        {
            get => GetV(12 + GraphicKind * 100).x != 0;
            set => SetV(12 + GraphicKind * 100, new Vector4(value ? 1 : 0, 0, 0, 0));
        }
        public RectTransform rectTransform => new RectTransform(m_Id);
        public void CrossFadeColor(Color targetColor, float duration, bool ignoreTimeScale, bool useAlpha) => color = targetColor;
    }

    public class Image : Graphic
    {
        internal Image() { }
        internal override int GraphicKind => 0;
        public enum Type { Simple = 0, Sliced = 1, Tiled = 2, Filled = 3 }
        public enum FillMethod { Horizontal = 0, Vertical = 1, Radial90 = 2, Radial180 = 3, Radial360 = 4 }
        public enum OriginHorizontal { Left = 0, Right = 1 }
        public enum OriginVertical { Bottom = 0, Top = 1 }
        public enum Origin360 { Bottom = 0, Right = 1, Top = 2, Left = 3 }

        public float fillAmount { get => GetV(20).x; set => SetV(20, new Vector4(Mathf.Clamp01(value), 0, 0, 0)); }
        public Type type { get => (Type)(int)GetV(21).x; set => SetV(21, new Vector4((int)value, 0, 0, 0)); }
        public FillMethod fillMethod { get => (FillMethod)(int)GetV(22).x; set => SetV(22, new Vector4((int)value, 0, 0, 0)); }
        public int fillOrigin { get => (int)GetV(23).x; set => SetV(23, new Vector4(value, 0, 0, 0)); }
        public bool preserveAspect { get => GetV(24).x != 0; set => SetV(24, new Vector4(value ? 1 : 0, 0, 0, 0)); }
        public Sprite sprite { get => Sprite.FromPath(GetS(2)); set => SetS(2, value?.m_Path ?? ""); }
        public void SetNativeSize() => SetV(25, new Vector4(1, 0, 0, 0));
    }

    public class Text : Graphic
    {
        internal Text() { }
        internal override int GraphicKind => 1;
        public virtual string text { get => GetS(0) ?? ""; set => SetS(0, value ?? ""); }
        public int fontSize { get => (int)GetV(30).x; set => SetV(30, new Vector4(value, 0, 0, 0)); }
        public TextAnchor alignment { get => (TextAnchor)(int)GetV(31).x; set => SetV(31, new Vector4((int)value, 0, 0, 0)); }
        public float lineSpacing { get => GetV(32).x; set => SetV(32, new Vector4(value, 0, 0, 0)); }
        public FontStyle fontStyle { get => (FontStyle)(int)GetV(33).x; set => SetV(33, new Vector4((int)value, 0, 0, 0)); }

        // ---- TextMeshPro 기능 (같은 컴포넌트) ----
        public bool supportRichText { get => GetV(137).x != 0; set => SetV(137, new Vector4(value ? 1 : 0, 0, 0, 0)); }
        public bool richText { get => supportRichText; set => supportRichText = value; }
        public bool resizeTextForBestFit { get => GetV(138).x != 0; set => SetV(138, new Vector4(value ? 1 : 0, 0, 0, 0)); }
        public bool enableAutoSizing { get => resizeTextForBestFit; set => resizeTextForBestFit = value; }
        public int resizeTextMinSize { get => (int)GetV(139).x; set { var v = GetV(139); SetV(139, new Vector4(value, v.y, 0, 0)); } }
        public int resizeTextMaxSize { get => (int)GetV(139).y; set { var v = GetV(139); SetV(139, new Vector4(v.x, value, 0, 0)); } }
        public float fontSizeMin { get => resizeTextMinSize; set => resizeTextMinSize = (int)value; }
        public float fontSizeMax { get => resizeTextMaxSize; set => resizeTextMaxSize = (int)value; }
        /// <summary>글자 간격 (글자 크기의 1/100 단위, TMP 와 같음)</summary>
        public float characterSpacing { get => GetV(130).x; set => SetV(130, new Vector4(value, 0, 0, 0)); }
        public float wordSpacing { get => GetV(131).x; set => SetV(131, new Vector4(value, 0, 0, 0)); }
        public float paragraphSpacing { get => GetV(132).x; set => SetV(132, new Vector4(value, 0, 0, 0)); }
        /// <summary>여백 (왼쪽, 위, 오른쪽, 아래)</summary>
        public Vector4 margin { get => GetV(133); set => SetV(133, value); }
        /// <summary>앞에서부터 이만큼만 보인다 (타자기 효과). 음수 = 전부</summary>
        public int maxVisibleCharacters { get => (int)GetV(134).x; set => SetV(134, new Vector4(value, 0, 0, 0)); }
        public bool enableWordWrapping { get => GetV(135).x != 0; set => SetV(135, new Vector4(value ? 1 : 0, 0, 0, 0)); }
        public HorizontalWrapMode horizontalOverflow { get => enableWordWrapping ? HorizontalWrapMode.Wrap : HorizontalWrapMode.Overflow; set => enableWordWrapping = value == HorizontalWrapMode.Wrap; }
        public TMPro.TextOverflowModes overflowMode
        {
            get { int v = (int)GetV(136).x; return v == 0 ? TMPro.TextOverflowModes.Truncate : (v == 2 ? TMPro.TextOverflowModes.Ellipsis : TMPro.TextOverflowModes.Overflow); }
            set => SetV(136, new Vector4(value == TMPro.TextOverflowModes.Truncate || value == TMPro.TextOverflowModes.Masking ? 0 : (value == TMPro.TextOverflowModes.Ellipsis ? 2 : 1), 0, 0, 0));
        }
        /// <summary>외곽선 두께 (0 ~ 1, 글꼴 여백 비율)</summary>
        public float outlineWidth { get => GetV(141).x; set => SetV(141, new Vector4(value, 0, 0, 0)); }
        public Color outlineColor { get { var v = GetV(142); return new Color(v.x, v.y, v.z, v.w); } set => SetV(142, new Vector4(value.r, value.g, value.b, value.a)); }
        /// <summary>굵기 (-1 ~ 1)</summary>
        public float faceDilate { get => GetV(143).x; set { var v = GetV(143); SetV(143, new Vector4(value, v.y, 0, 0)); } }
        public float faceSoftness { get => GetV(143).y; set { var v = GetV(143); SetV(143, new Vector4(v.x, value, 0, 0)); } }
        /// <summary>그림자 (Underlay)</summary>
        public bool enableUnderlay { get => GetV(145).x != 0; set { var v = GetV(145); SetV(145, new Vector4(value ? 1 : 0, v.y, v.z, v.w)); } }
        public Color underlayColor { get { var v = GetV(144); return new Color(v.x, v.y, v.z, v.w); } set => SetV(144, new Vector4(value.r, value.g, value.b, value.a)); }
        /// <summary>그림자 위치 (-1 ~ 1, + = 오른쪽 · 위)</summary>
        public Vector2 underlayOffset { get { var v = GetV(145); return new Vector2(v.y, v.z); } set { var v = GetV(145); SetV(145, new Vector4(v.x, value.x, value.y, v.w)); } }
        public float underlaySoftness { get => GetV(145).w; set { var v = GetV(145); SetV(145, new Vector4(v.x, v.y, v.z, value)); } }
        public float preferredWidth => GetV(146).x;
        public float preferredHeight => GetV(146).y;
        public Vector2 GetPreferredValues() { var v = GetV(146); return new Vector2(v.x, v.y); }
        public bool isTextOverflowing => GetV(147).x != 0;
        /// <summary>Auto Size 가 고른 크기</summary>
        public float fontSizeUsed => GetV(148).x;
        /// <summary>글자 · 줄 · 링크 정보 (지금 글 기준으로 다시 계산)</summary>
        public TMPro.TMP_TextInfo textInfo => TMPro.TMP_TextInfo.Read(m_Id);
        public unsafe void ForceMeshUpdate() => Native.Api.TX_Info(m_Id, 11, 0, null, 0);
        public class LinkClickedEvent : UnityEvent<string> { }
        /// <summary>NOVA: &lt;link="id"&gt; 를 누르면 id</summary>
        public LinkClickedEvent onLinkClicked => UIEvents.LinkClicked(m_Id);

        // ---- 글자 애니메이션 (NOVA): 글자 순번 = textInfo.characterInfo 순번, 글이 바뀌면 지워진다 ----
        public unsafe void SetCharacterOffset(int index, Vector2 offset) { float* v = stackalloc float[2]; v[0] = offset.x; v[1] = offset.y; Native.Api.TX_Info(m_Id, 7, index, v, 2); }
        public unsafe void SetCharacterScale(int index, float scale) { float* v = stackalloc float[1]; v[0] = scale; Native.Api.TX_Info(m_Id, 8, index, v, 1); }
        public unsafe void SetCharacterColor(int index, Color color) { float* v = stackalloc float[4]; v[0] = color.r; v[1] = color.g; v[2] = color.b; v[3] = color.a; Native.Api.TX_Info(m_Id, 9, index, v, 4); }
        public unsafe void ClearCharacterModifiers() => Native.Api.TX_Info(m_Id, 10, 0, null, 0);
    }

    public enum HorizontalWrapMode { Wrap = 0, Overflow = 1 }

    // ------------------------------------------------------------------ 자동 레이아웃 (Unity UnityEngine.UI 와 같은 이름)
    public abstract class UIComponent : Behaviour
    {
        internal UIComponent() { }
        protected unsafe Vector4 GetV(int p) { Vector4 v; Native.Api.UI_GetVec(m_Id, p, &v); return v; }
        protected unsafe void SetV(int p, Vector4 v) => Native.Api.UI_SetVec(m_Id, p, &v);
    }

    /// <summary>Unity 의 RectOffset (Layout Group 의 padding) — 값을 바꾸면 컴포넌트에 바로 반영</summary>
    public class RectOffset
    {
        readonly LayoutGroup m_Owner;
        int m_Left, m_Right, m_Top, m_Bottom;
        public RectOffset() { }
        public RectOffset(int left, int right, int top, int bottom) { m_Left = left; m_Right = right; m_Top = top; m_Bottom = bottom; }
        internal RectOffset(LayoutGroup owner, Vector4 v) { m_Owner = owner; m_Left = (int)v.x; m_Right = (int)v.y; m_Top = (int)v.z; m_Bottom = (int)v.w; }
        void Push() => m_Owner?.SetPadding(this);
        public int left { get => m_Left; set { m_Left = value; Push(); } }
        public int right { get => m_Right; set { m_Right = value; Push(); } }
        public int top { get => m_Top; set { m_Top = value; Push(); } }
        public int bottom { get => m_Bottom; set { m_Bottom = value; Push(); } }
        public int horizontal => m_Left + m_Right;
        public int vertical => m_Top + m_Bottom;
    }

    public abstract class LayoutGroup : UIComponent
    {
        internal LayoutGroup() { }
        public RectOffset padding { get => new RectOffset(this, GetV(230)); set { if (value != null) SetPadding(value); } }
        internal void SetPadding(RectOffset p) => SetV(230, new Vector4(p.left, p.right, p.top, p.bottom));
        public TextAnchor childAlignment { get => (TextAnchor)(int)GetV(231).x; set => SetV(231, new Vector4((int)value, 0, 0, 0)); }
        public float minWidth => GetV(237).x;
        public float minHeight => GetV(237).y;
        public float preferredWidth => GetV(237).z;
        public float preferredHeight => GetV(237).w;
    }

    public abstract class HorizontalOrVerticalLayoutGroup : LayoutGroup
    {
        internal HorizontalOrVerticalLayoutGroup() { }
        public float spacing { get => GetV(232).x; set => SetV(232, new Vector4(value, 0, 0, 0)); }
        public bool childControlWidth { get => GetV(233).x != 0; set { var v = GetV(233); SetV(233, new Vector4(value ? 1 : 0, v.y, 0, 0)); } }
        public bool childControlHeight { get => GetV(233).y != 0; set { var v = GetV(233); SetV(233, new Vector4(v.x, value ? 1 : 0, 0, 0)); } }
        public bool childForceExpandWidth { get => GetV(234).x != 0; set { var v = GetV(234); SetV(234, new Vector4(value ? 1 : 0, v.y, 0, 0)); } }
        public bool childForceExpandHeight { get => GetV(234).y != 0; set { var v = GetV(234); SetV(234, new Vector4(v.x, value ? 1 : 0, 0, 0)); } }
        public bool childScaleWidth { get => GetV(235).x != 0; set { var v = GetV(235); SetV(235, new Vector4(value ? 1 : 0, v.y, 0, 0)); } }
        public bool childScaleHeight { get => GetV(235).y != 0; set { var v = GetV(235); SetV(235, new Vector4(v.x, value ? 1 : 0, 0, 0)); } }
        public bool reverseArrangement { get => GetV(236).x != 0; set => SetV(236, new Vector4(value ? 1 : 0, 0, 0, 0)); }
    }

    public sealed class HorizontalLayoutGroup : HorizontalOrVerticalLayoutGroup { internal HorizontalLayoutGroup() { } }
    public sealed class VerticalLayoutGroup : HorizontalOrVerticalLayoutGroup { internal VerticalLayoutGroup() { } }

    public sealed class GridLayoutGroup : LayoutGroup
    {
        internal GridLayoutGroup() { }
        public enum Corner { UpperLeft = 0, UpperRight = 1, LowerLeft = 2, LowerRight = 3 }
        public enum Axis { Horizontal = 0, Vertical = 1 }
        public enum Constraint { Flexible = 0, FixedColumnCount = 1, FixedRowCount = 2 }
        public Vector2 cellSize { get { var v = GetV(240); return new Vector2(v.x, v.y); } set => SetV(240, new Vector4(value.x, value.y, 0, 0)); }
        public Vector2 spacing { get { var v = GetV(232); return new Vector2(v.x, v.y); } set => SetV(232, new Vector4(value.x, value.y, 0, 0)); }
        public Constraint constraint { get => (Constraint)(int)GetV(241).x; set { var v = GetV(241); SetV(241, new Vector4((int)value, v.y, 0, 0)); } }
        public int constraintCount { get => (int)GetV(241).y; set { var v = GetV(241); SetV(241, new Vector4(v.x, value, 0, 0)); } }
        public Corner startCorner { get => (Corner)(int)GetV(242).x; set { var v = GetV(242); SetV(242, new Vector4((int)value, v.y, 0, 0)); } }
        public Axis startAxis { get => (Axis)(int)GetV(242).y; set { var v = GetV(242); SetV(242, new Vector4(v.x, (int)value, 0, 0)); } }
    }

    public sealed class ContentSizeFitter : UIComponent
    {
        internal ContentSizeFitter() { }
        public enum FitMode { Unconstrained = 0, MinSize = 1, PreferredSize = 2 }
        public FitMode horizontalFit { get => (FitMode)(int)GetV(250).x; set { var v = GetV(250); SetV(250, new Vector4((int)value, v.y, 0, 0)); } }
        public FitMode verticalFit { get => (FitMode)(int)GetV(250).y; set { var v = GetV(250); SetV(250, new Vector4(v.x, (int)value, 0, 0)); } }
    }

    public sealed class AspectRatioFitter : UIComponent
    {
        internal AspectRatioFitter() { }
        public enum AspectMode { None = 0, WidthControlsHeight = 1, HeightControlsWidth = 2, FitInParent = 3, EnvelopeParent = 4 }
        public AspectMode aspectMode { get => (AspectMode)(int)GetV(270).x; set { var v = GetV(270); SetV(270, new Vector4((int)value, v.y, 0, 0)); } }
        public float aspectRatio { get => GetV(270).y; set { var v = GetV(270); SetV(270, new Vector4(v.x, value, 0, 0)); } }
    }

    public sealed class LayoutElement : UIComponent
    {
        internal LayoutElement() { }
        public bool ignoreLayout { get => GetV(263).x != 0; set => SetV(263, new Vector4(value ? 1 : 0, 0, 0, 0)); }
        public float minWidth { get => GetV(260).x; set { var v = GetV(260); SetV(260, new Vector4(value, v.y, 0, 0)); } }
        public float minHeight { get => GetV(260).y; set { var v = GetV(260); SetV(260, new Vector4(v.x, value, 0, 0)); } }
        public float preferredWidth { get => GetV(261).x; set { var v = GetV(261); SetV(261, new Vector4(value, v.y, 0, 0)); } }
        public float preferredHeight { get => GetV(261).y; set { var v = GetV(261); SetV(261, new Vector4(v.x, value, 0, 0)); } }
        public float flexibleWidth { get => GetV(262).x; set { var v = GetV(262); SetV(262, new Vector4(value, v.y, 0, 0)); } }
        public float flexibleHeight { get => GetV(262).y; set { var v = GetV(262); SetV(262, new Vector4(v.x, value, 0, 0)); } }
        public int layoutPriority { get => (int)GetV(264).x; set => SetV(264, new Vector4(value, 0, 0, 0)); }
    }

    /// <summary>NOVA: 화면 점의 UI (EventSystem 의 레이캐스트와 같은 규칙 — Overlay 먼저, World Space 캔버스는 카메라 광선)</summary>
    public static class UIRaycast
    {
        /// <summary>screenPosition = Input.mousePosition 과 같은 좌표 (게임 화면 픽셀, 왼쪽 아래 0,0). 없으면 null</summary>
        public static unsafe GameObject Pick(Vector2 screenPosition)
        {
            ulong id = Native.Api.UI_RaycastScreen(screenPosition.x, screenPosition.y);
            return id == 0 ? null : new GameObject(id);
        }
    }

    /// <summary>Unity 의 LayoutRebuilder</summary>
    public static class LayoutRebuilder
    {
        public static unsafe void ForceRebuildLayoutImmediate(RectTransform layoutRoot)
        {
            if (layoutRoot == null) return;
            Vector4 v = default; Native.Api.UI_SetVec(layoutRoot.m_Id, 280, &v);
        }
        public static void MarkLayoutForRebuild(RectTransform rect) { }   // NOVA 는 매 프레임 계산한다
    }

    /// <summary>Unity 의 LayoutUtility</summary>
    public static class LayoutUtility
    {
        static unsafe Vector4 Sizes(RectTransform rect) { Vector4 v = default; if (rect != null) Native.Api.UI_GetVec(rect.m_Id, 271, &v); return v; }
        public static float GetMinWidth(RectTransform rect) => Sizes(rect).x;
        public static float GetMinHeight(RectTransform rect) => Sizes(rect).y;
        public static float GetPreferredWidth(RectTransform rect) => Sizes(rect).z;
        public static float GetPreferredHeight(RectTransform rect) => Sizes(rect).w;
        public static float GetMinSize(RectTransform rect, int axis) => axis == 0 ? GetMinWidth(rect) : GetMinHeight(rect);
        public static float GetPreferredSize(RectTransform rect, int axis) => axis == 0 ? GetPreferredWidth(rect) : GetPreferredHeight(rect);
    }
    public enum VerticalWrapMode { Truncate = 0, Overflow = 1 }

    public class Selectable : Behaviour
    {
        internal Selectable() { }
        public unsafe bool interactable
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 40, &v); return v.x != 0; }
            set { Vector4 v = new Vector4(value ? 1 : 0, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 40, &v); }
        }
        public unsafe override bool enabled
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 41, &v); return v.x != 0; }
            set { Vector4 v = new Vector4(value ? 1 : 0, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 41, &v); }
        }
        public Graphic targetGraphic => GetComponent<Image>();
    }

    public class Button : Selectable
    {
        internal Button() { }
        public class ButtonClickedEvent : UnityEvent { }
        // Inspector 의 On Click () 목록과 별개로, 코드에서 추가한 리스너 (Unity 와 같음)
        public ButtonClickedEvent onClick => UIEvents.Click(m_Id);
    }

    public class Toggle : Selectable
    {
        internal Toggle() { }
        public class ToggleEvent : UnityEvent<bool> { }
        public unsafe bool isOn
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 70, &v); return v.x != 0; }
            set { Vector4 v = new Vector4(value ? 1 : 0, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 70, &v); }
        }
        public unsafe void SetIsOnWithoutNotify(bool value) { Vector4 v = new Vector4(value ? 1 : 0, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 71, &v); }
        public ToggleEvent onValueChanged => UIEvents.ToggleChanged(m_Id);
    }

    public class Slider : Selectable
    {
        internal Slider() { }
        public class SliderEvent : UnityEvent<float> { }
        public enum Direction { LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3 }
        unsafe float Get(int p) { Vector4 v; Native.Api.UI_GetVec(m_Id, p, &v); return v.x; }
        unsafe void Set(int p, float x) { Vector4 v = new Vector4(x, 0, 0, 0); Native.Api.UI_SetVec(m_Id, p, &v); }
        public float value { get => Get(60); set => Set(60, value); }
        public float minValue { get => Get(61); set => Set(61, value); }
        public float maxValue { get => Get(62); set => Set(62, value); }
        public bool wholeNumbers { get => Get(63) != 0; set => Set(63, value ? 1 : 0); }
        public float normalizedValue { get => Get(64); set => Set(64, value); }
        public void SetValueWithoutNotify(float input) => Set(65, input);
        public SliderEvent onValueChanged => UIEvents.SliderChanged(m_Id);
    }

    public class Scrollbar : Selectable
    {
        internal Scrollbar() { }
        public class ScrollEvent : UnityEvent<float> { }
        public enum Direction { LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3 }
        unsafe float Get(int p) { Vector4 v; Native.Api.UI_GetVec(m_Id, p, &v); return v.x; }
        unsafe void Set(int p, float x, float y = 0) { Vector4 v = new Vector4(x, y, 0, 0); Native.Api.UI_SetVec(m_Id, p, &v); }
        public float value { get => Get(90); set => Set(90, value); }
        public float size { get => Get(91); set => Set(91, value); }
        public int numberOfSteps { get => (int)Get(92); set => Set(92, value); }
        public Direction direction { get => (Direction)(int)Get(93); set => Set(93, (int)value); }
        public void SetValueWithoutNotify(float input) => Set(90, input, 1);
        public ScrollEvent onValueChanged => UIEvents.ScrollbarChanged(m_Id);
    }

    public class Dropdown : Selectable
    {
        internal Dropdown() { }
        public class DropdownEvent : UnityEvent<int> { }
        public class OptionData
        {
            public string text;
            public Sprite image;
            public OptionData() { }
            public OptionData(string text) { this.text = text; }
            public OptionData(string text, Sprite image) { this.text = text; this.image = image; }
            public OptionData(Sprite image) { this.image = image; }
        }
        /// <summary>옵션 목록 — Add / Clear / 인덱서로 바꾸면 컴포넌트에 바로 반영 (Unity 처럼 dropdown.options.Add(...))</summary>
        public class OptionDataList : System.Collections.Generic.IEnumerable<OptionData>
        {
            readonly Dropdown m_Owner; readonly List<OptionData> m_List;
            internal OptionDataList(Dropdown owner, List<OptionData> list) { m_Owner = owner; m_List = list; }
            void Push() => m_Owner.WriteOptions(m_List);
            public int Count => m_List.Count;
            public OptionData this[int i] { get => m_List[i]; set { m_List[i] = value; Push(); } }
            public void Add(OptionData o) { m_List.Add(o); Push(); }
            public void AddRange(IEnumerable<OptionData> o) { m_List.AddRange(o); Push(); }
            public void Insert(int i, OptionData o) { m_List.Insert(i, o); Push(); }
            public void RemoveAt(int i) { m_List.RemoveAt(i); Push(); }
            public bool Remove(OptionData o) { bool r = m_List.Remove(o); Push(); return r; }
            public void Clear() { m_List.Clear(); Push(); }
            public System.Collections.Generic.IEnumerator<OptionData> GetEnumerator() => m_List.GetEnumerator();
            System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => m_List.GetEnumerator();
            public static implicit operator List<OptionData>(OptionDataList l) => new List<OptionData>(l.m_List);
        }
        unsafe float Get(int p) { Vector4 v; Native.Api.UI_GetVec(m_Id, p, &v); return v.x; }
        unsafe void Set(int p, float x, float y = 0) { Vector4 v = new Vector4(x, y, 0, 0); Native.Api.UI_SetVec(m_Id, p, &v); }
        unsafe List<OptionData> ReadOptions()
        {
            string s = Native.Str(Native.Api.UI_GetString(m_Id, 10)) ?? "";
            var list = new List<OptionData>();
            if (Get(97) > 0) foreach (var t in s.Split('\u001F')) list.Add(new OptionData(t));
            return list;
        }
        internal unsafe void WriteOptions(List<OptionData> list)
        {
            var sb = new System.Text.StringBuilder();
            for (int i = 0; i < list.Count; i++) { if (i > 0) sb.Append('\u001F'); sb.Append(list[i]?.text ?? ""); }
            fixed (byte* p = Native.Utf8(sb.ToString())) Native.Api.UI_SetString(m_Id, 10, p);
        }
        public OptionDataList options
        {
            get => new OptionDataList(this, ReadOptions());
            set { var l = new List<OptionData>(); if (value != null) foreach (var o in value) l.Add(o); WriteOptions(l); }
        }
        public int value { get => (int)Get(95); set => Set(95, value); }
        public void SetValueWithoutNotify(int input) => Set(95, input, 1);
        public void AddOptions(List<string> labels) { var l = ReadOptions(); foreach (var s in labels) l.Add(new OptionData(s)); WriteOptions(l); }
        public void AddOptions(List<OptionData> data) { var l = ReadOptions(); l.AddRange(data); WriteOptions(l); }
        public void ClearOptions() => WriteOptions(new List<OptionData>());
        public void RefreshShownValue() => Set(97, 1);
        public void Show() => Set(96, 1);
        public void Hide() => Set(96, 0);
        public bool IsExpanded => Get(96) != 0;
        public DropdownEvent onValueChanged => UIEvents.DropdownChanged(m_Id);
    }

    public class InputField : Selectable
    {
        internal InputField() { }
        public class OnChangeEvent : UnityEvent<string> { }
        public class EndEditEvent : UnityEvent<string> { }
        public unsafe string text
        {
            get => Native.Str(Native.Api.UI_GetString(m_Id, 3)) ?? "";
            set { fixed (byte* b = Native.Utf8(value ?? "")) Native.Api.UI_SetString(m_Id, 3, b); }
        }
        public unsafe void SetTextWithoutNotify(string value) { fixed (byte* b = Native.Utf8(value ?? "")) Native.Api.UI_SetString(m_Id, 4, b); }
        public unsafe int characterLimit
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 75, &v); return (int)v.x; }
            set { Vector4 v = new Vector4(value, 0, 0, 0); Native.Api.UI_SetVec(m_Id, 75, &v); }
        }
        public unsafe bool isFocused { get { Vector4 v; Native.Api.UI_GetVec(m_Id, 76, &v); return v.x != 0; } }
        public OnChangeEvent onValueChanged => UIEvents.InputChanged(m_Id);
        public EndEditEvent onEndEdit => UIEvents.InputEnd(m_Id);
    }

    public class ScrollRect : Behaviour
    {
        internal ScrollRect() { }
        public unsafe Vector2 normalizedPosition
        {
            get { Vector4 v; Native.Api.UI_GetVec(m_Id, 80, &v); return new Vector2(v.x, v.y); }
            set { Vector4 v = new Vector4(value.x, value.y, 0, 0); Native.Api.UI_SetVec(m_Id, 80, &v); }
        }
        public float horizontalNormalizedPosition { get => normalizedPosition.x; set => normalizedPosition = new Vector2(value, normalizedPosition.y); }
        public float verticalNormalizedPosition { get => normalizedPosition.y; set => normalizedPosition = new Vector2(normalizedPosition.x, value); }
    }

    public class Mask : Behaviour { internal Mask() { } }
    public class RectMask2D : Behaviour { internal RectMask2D() { } }
}

namespace TMPro
{
    using NovaEngine;
    using NovaEngine.Interop;

    // TextMeshPro 코드도 그대로 쓸 수 있게 (같은 Text 컴포넌트 — TMP 의 기능은 Text 에 들어 있다)
    public class TMP_Text : NovaEngine.UI.Text
    {
        internal TMP_Text() { }
        /// <summary>TMP 의 FontStyles (굵게 · 기울임 · 밑줄 · 취소선 · 소문자 · 대문자)</summary>
        public new FontStyles fontStyle
        {
            get
            {
                int b = (int)GetV(140).x; var s = FontStyles.Normal;
                if ((b & 1) != 0) s |= FontStyles.Bold; if ((b & 2) != 0) s |= FontStyles.Italic; if ((b & 4) != 0) s |= FontStyles.Underline;
                if ((b & 8) != 0) s |= FontStyles.Strikethrough; if ((b & 16) != 0) s |= FontStyles.LowerCase; if ((b & 32) != 0) s |= FontStyles.UpperCase;
                return s;
            }
            set
            {
                int b = 0;
                if ((value & FontStyles.Bold) != 0) b |= 1; if ((value & FontStyles.Italic) != 0) b |= 2; if ((value & FontStyles.Underline) != 0) b |= 4;
                if ((value & FontStyles.Strikethrough) != 0) b |= 8; if ((value & FontStyles.LowerCase) != 0) b |= 16; if ((value & FontStyles.UpperCase) != 0) b |= 32;
                SetV(140, new Vector4(b, 0, 0, 0));
            }
        }
        /// <summary>TMP 의 줄 간격 (글자 크기의 1/100 을 더함) — Text 의 배율로 바꿔 둔다</summary>
        public new float lineSpacing { get => (base.lineSpacing - 1f) * 100f; set => base.lineSpacing = 1f + value / 100f; }
        /// <summary>TMP 의 TextAlignmentOptions (가로 · 세로 비트)</summary>
        public new TextAlignmentOptions alignment
        {
            get { int a = (int)base.alignment; int h = a % 3, v = a / 3; return (TextAlignmentOptions)((h == 0 ? 1 : h == 1 ? 2 : 4) | (v == 0 ? 256 : v == 1 ? 512 : 1024)); }
            set
            {
                int x = (int)value;
                int h = (x & 4) != 0 ? 2 : ((x & (2 | 32)) != 0 ? 1 : 0);
                int v = (x & 1024) != 0 ? 2 : ((x & (512 | 4096 | 2048)) != 0 ? 1 : 0);
                base.alignment = (NovaEngine.TextAnchor)(v * 3 + h);
            }
        }
        public new float fontSize { get => base.fontSize; set => base.fontSize = (int)Mathf.Round(value); }
        public new string text { get => base.text; set => base.text = value; }
        public void SetText(string sourceText) => text = sourceText;
        public string GetParsedText()
        {
            var info = textInfo; var sb = new System.Text.StringBuilder();
            for (int i = 0; i < info.characterCount; i++) sb.Append(info.characterInfo[i].character);
            return sb.ToString();
        }
    }
    public class TextMeshProUGUI : TMP_Text { internal TextMeshProUGUI() { } }
    public class TMP_InputField : NovaEngine.UI.InputField { internal TMP_InputField() { } }
    public class TMP_Dropdown : NovaEngine.UI.Dropdown { internal TMP_Dropdown() { } }

    [System.Flags]
    public enum FontStyles { Normal = 0, Bold = 1, Italic = 2, Underline = 4, LowerCase = 8, UpperCase = 16, SmallCaps = 32, Strikethrough = 64, Superscript = 128, Subscript = 256, Highlight = 512 }
    public enum TextOverflowModes { Overflow = 0, Ellipsis = 1, Masking = 2, Truncate = 3, ScrollRect = 4, Page = 5, Linked = 6 }
    public enum TextAlignmentOptions
    {
        TopLeft = 257, Top = 258, TopRight = 260, Left = 513, Center = 514, Right = 516, BottomLeft = 1025, Bottom = 1026, BottomRight = 1028,
        TopJustified = 264, Justified = 520, BottomJustified = 1032, Midline = 4098, Baseline = 2050, Capline = 8194
    }

    public struct TMP_CharacterInfo
    {
        public char character;
        public int index;          // 원문 위치 (태그 포함)
        public bool isVisible;
        public int lineNumber;
        public Vector3 bottomLeft, topRight;   // Text 로컬 좌표
        public Vector3 bottomRight => new Vector3(topRight.x, bottomLeft.y, 0);
        public Vector3 topLeft => new Vector3(bottomLeft.x, topRight.y, 0);
        public float pointSize;
        internal int link;
    }

    public struct TMP_LineInfo { public int firstCharacterIndex, characterCount, lastCharacterIndex; public float lineHeight; }

    public struct TMP_LinkInfo
    {
        internal ulong m_Owner; internal int m_Index;
        public int linkTextfirstCharacterIndex, linkTextLength;
        public unsafe string GetLinkID() => Native.Str(Native.Api.TX_Link(m_Owner, m_Index, 0));
        public unsafe string GetLinkText() => Native.Str(Native.Api.TX_Link(m_Owner, m_Index, 1));
    }

    public class TMP_TextInfo
    {
        public int characterCount, lineCount, linkCount;
        public TMP_CharacterInfo[] characterInfo;
        public TMP_LineInfo[] lineInfo;
        public TMP_LinkInfo[] linkInfo;

        internal static unsafe TMP_TextInfo Read(ulong id)
        {
            var info = new TMP_TextInfo();
            float* v = stackalloc float[10];
            info.characterCount = Native.Api.TX_Info(id, 0, 0, null, 0);
            info.characterInfo = new TMP_CharacterInfo[info.characterCount];
            for (int i = 0; i < info.characterCount; i++)
            {
                Native.Api.TX_Info(id, 1, i, v, 10);
                info.characterInfo[i] = new TMP_CharacterInfo
                {
                    character = (char)(int)v[0], index = (int)v[1], isVisible = v[2] != 0, lineNumber = (int)v[3],
                    bottomLeft = new Vector3(v[4], v[5], 0), topRight = new Vector3(v[6], v[7], 0), link = (int)v[8], pointSize = v[9]
                };
            }
            info.lineCount = Native.Api.TX_Info(id, 2, 0, null, 0);
            info.lineInfo = new TMP_LineInfo[info.lineCount];
            for (int i = 0; i < info.lineCount; i++)
            {
                Native.Api.TX_Info(id, 3, i, v, 4);
                info.lineInfo[i] = new TMP_LineInfo { firstCharacterIndex = (int)v[0], characterCount = (int)v[1], lastCharacterIndex = (int)v[0] + (int)v[1] - 1, lineHeight = v[3] };
            }
            info.linkCount = Native.Api.TX_Info(id, 4, 0, null, 0);
            info.linkInfo = new TMP_LinkInfo[info.linkCount];
            for (int i = 0; i < info.linkCount; i++)
            {
                Native.Api.TX_Info(id, 5, i, v, 2);
                info.linkInfo[i] = new TMP_LinkInfo { m_Owner = id, m_Index = i, linkTextfirstCharacterIndex = (int)v[0], linkTextLength = (int)v[1] };
            }
            return info;
        }
    }

    public static class TMP_TextUtilities
    {
        /// <summary>화면 점(Input.mousePosition)에 있는 &lt;link&gt; 번호 (없으면 -1). Screen Space - Overlay 캔버스 기준 (camera 는 무시)</summary>
        public static unsafe int FindIntersectingLink(NovaEngine.UI.Text text, Vector3 position, Camera camera)
        {
            if (text == null) return -1;
            float* v = stackalloc float[2]; v[0] = position.x; v[1] = position.y;
            return Native.Api.TX_Info(text.m_Id, 6, 0, v, 2);
        }
    }
}
