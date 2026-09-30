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
        public RenderMode renderMode => RenderMode.ScreenSpaceOverlay;
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
        internal static Button.ButtonClickedEvent Click(ulong id)
        {
            if (!s_Click.TryGetValue(id, out var e)) s_Click[id] = e = new Button.ButtonClickedEvent();
            return e;
        }
        internal static void Invoke(ulong id, int kind)
        {
            if (kind == 0 && s_Click.TryGetValue(id, out var e)) e.Invoke();
        }
        internal static void Clear() => s_Click.Clear();
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
    }

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
}

namespace TMPro
{
    // TextMeshPro 코드도 그대로 쓸 수 있게 (같은 Text 컴포넌트)
    public class TMP_Text : NovaEngine.UI.Text { internal TMP_Text() { } }
    public class TextMeshProUGUI : TMP_Text { internal TextMeshProUGUI() { } }
}
