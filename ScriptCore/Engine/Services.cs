using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using NovaEngine.Interop;

namespace NovaEngine
{
    // ------------------------------------------------------------------ Time
    public static class Time
    {
        static TimeData s_Data;
        static int s_FetchedFrame = -1;
        internal static bool s_InFixedUpdate;

        static unsafe ref TimeData Data
        {
            get
            {
                // 한 프레임에 한 번만 네이티브에서 가져온다 (Bridge 가 프레임 시작마다 무효화)
                if (s_FetchedFrame != Bridge.FrameIndex)
                {
                    fixed (TimeData* p = &s_Data) Native.Api.Time_Get(p);
                    s_FetchedFrame = Bridge.FrameIndex;
                }
                return ref s_Data;
            }
        }

        public static float deltaTime => s_InFixedUpdate ? Data.fixedDeltaTime : Data.deltaTime;
        public static float unscaledDeltaTime => Data.unscaledDeltaTime;
        public static float smoothDeltaTime => deltaTime;
        public static float time => Data.time;
        public static float timeSinceLevelLoad => Data.time;
        public static float unscaledTime => Data.unscaledTime;
        public static float fixedDeltaTime { get => Data.fixedDeltaTime; set { } }
        public static float fixedTime => Data.time;
        public static float realtimeSinceStartup => Data.realtimeSinceStartup;
        public static int frameCount => Data.frameCount;
        public static float timeScale { get => Data.timeScale; set { } }
    }

    // ------------------------------------------------------------------ Debug
    public static class Debug
    {
        public static void Log(object message) => Write(0, message);
        public static void Log(object message, Object context) => Write(0, message);
        public static void LogWarning(object message) => Write(1, message);
        public static void LogWarning(object message, Object context) => Write(1, message);
        public static void LogError(object message) => Write(2, message);
        public static void LogError(object message, Object context) => Write(2, message);
        public static void LogFormat(string format, params object[] args) => Write(0, string.Format(format, args));
        public static void LogWarningFormat(string format, params object[] args) => Write(1, string.Format(format, args));
        public static void LogErrorFormat(string format, params object[] args) => Write(2, string.Format(format, args));
        public static void LogException(Exception e) => Bridge.LogException(e);
        public static void Assert(bool condition, object message = null) { if (!condition) Write(2, "Assertion failed" + (message != null ? ": " + message : "")); }
        public static void DrawLine(Vector3 start, Vector3 end, Color color = default, float duration = 0f) { }
        public static void DrawRay(Vector3 start, Vector3 dir, Color color = default, float duration = 0f) { }

        static void Write(int level, object message)
        {
            string text = message == null ? "Null" : message.ToString();
            // 호출한 사용자 코드의 위치 (Console 더블클릭 → 파일:줄)
            var st = new StackTrace(2, true);
            Bridge.Log(level, text, Bridge.FormatStack(st, out string file, out int line), file, line);
        }
    }

    // ------------------------------------------------------------------ Application / Screen
    public static class Application
    {
        public static bool isPlaying => true;
        // 빌드된 게임(플레이어)인지는 네이티브가 알려 준다
        public static unsafe bool isEditor => Native.Api.App_Info(0) == 0;
        public static bool isFocused => true;
        public static int targetFrameRate { get; set; } = -1;
        public static unsafe string productName => Native.Str(Native.Api.App_ProductName()) ?? "NOVA Game";
        // 저장 폴더 (Unity 와 같은 자리 — Windows: %USERPROFILE%\AppData\LocalLow\<회사>\<제품>, 안드로이드: 앱 파일 폴더)
        public static unsafe string persistentDataPath => Native.Str(Native.Api.App_Path(0)) ?? "";
        public static unsafe string dataPath => Native.Str(Native.Api.App_Path(1)) ?? "";
        public static unsafe string companyName => Native.Str(Native.Api.App_Path(2)) ?? "";
        public static unsafe string version => Native.Str(Native.Api.App_Path(3)) ?? "";
        public static unsafe string temporaryCachePath => Native.Str(Native.Api.App_Path(4)) ?? "";
        public static RuntimePlatform platform => (RuntimePlatform)Platform.NovaApp_Platform();
        public static bool isMobilePlatform => platform == RuntimePlatform.Android;
        // Unity 와 같이 에디터에서는 무시, 빌드된 게임은 종료
        public static unsafe void Quit()
        {
            if (isEditor) Debug.Log("Application.Quit() is ignored in the editor.");
            else Native.Api.App_Quit();
        }
    }
    // Unity 의 PlayerPrefs: 게임을 껐다 켜도 남는 작은 값 (설정 · 최고 점수 …). 종류가 다른 키는 없는 것으로 (GetInt 로 실수 키를 읽으면 기본값)
    //  저장 자리: 에디터 = 프로젝트의 Library/PlayerPrefs.json, Windows 게임 = persistentDataPath/PlayerPrefs.json, 안드로이드 = 앱 파일 폴더, 웹 = 브라우저 localStorage
    public static class PlayerPrefs
    {
        public static unsafe void SetInt(string key, int value) { fixed (byte* k = Native.Utf8(key)) Native.Api.Prefs_SetInt(k, value); }
        public static int GetInt(string key) => GetInt(key, 0);
        public static unsafe int GetInt(string key, int defaultValue) { fixed (byte* k = Native.Utf8(key)) return Native.Api.Prefs_GetInt(k, defaultValue); }
        public static unsafe void SetFloat(string key, float value) { fixed (byte* k = Native.Utf8(key)) Native.Api.Prefs_SetFloat(k, value); }
        public static float GetFloat(string key) => GetFloat(key, 0f);
        public static unsafe float GetFloat(string key, float defaultValue) { fixed (byte* k = Native.Utf8(key)) return Native.Api.Prefs_GetFloat(k, defaultValue); }
        public static unsafe void SetString(string key, string value)
        {
            fixed (byte* k = Native.Utf8(key)) fixed (byte* v = Native.Utf8(value ?? "")) Native.Api.Prefs_SetString(k, v);
        }
        public static string GetString(string key) => GetString(key, "");
        public static unsafe string GetString(string key, string defaultValue)
        {
            fixed (byte* k = Native.Utf8(key)) { byte* v = Native.Api.Prefs_GetString(k); return v == null ? defaultValue : Native.Str(v); }
        }
        public static unsafe bool HasKey(string key) { fixed (byte* k = Native.Utf8(key)) return Native.Api.Prefs_Has(k) != 0; }
        public static unsafe void DeleteKey(string key) { if (key == null) return; fixed (byte* k = Native.Utf8(key)) Native.Api.Prefs_Delete(k); }
        public static unsafe void DeleteAll() => Native.Api.Prefs_Delete(null);
        public static unsafe void Save() => Native.Api.Prefs_Save();
    }

    // Unity 와 같은 값
    public enum RuntimePlatform { WindowsPlayer = 2, WindowsEditor = 7, Android = 11, WebGLPlayer = 17 }
    public enum ScreenOrientation { Portrait = 1, PortraitUpsideDown = 2, LandscapeLeft = 3, LandscapeRight = 4, AutoRotation = 5 }

    // 엔진이 이름으로 내보낸 플랫폼 함수 (Source/Scripting/PlatformBindings.cpp — Windows 는 NovaCore.dll, 안드로이드는 libnova.so)
    internal static unsafe class Platform
    {
        const string Lib = "NovaCore";
        [DllImport(Lib)] internal static extern int NovaApp_Platform();
        [DllImport(Lib)] internal static extern int NovaInput_TouchCount();
        [DllImport(Lib)] internal static extern int NovaInput_GetTouch(int index, float* out7);
        [DllImport(Lib)] internal static extern void NovaScreen_SafeArea(float* out4);
        [DllImport(Lib)] internal static extern int NovaScreen_GetOrientation();
        [DllImport(Lib)] internal static extern void NovaScreen_SetOrientation(int orientation);
    }

    public static class Screen
    {
        public static unsafe int width { get { int w, h; Native.Api.Screen_Get(&w, &h); return w; } }
        public static unsafe int height { get { int w, h; Native.Api.Screen_Get(&w, &h); return h; } }
        // 노치 · 둥근 모서리에 가리지 않는 영역 (게임 화면 픽셀, 왼쪽 아래 기준). Windows 는 화면 전체
        public static unsafe Rect safeArea { get { float* r = stackalloc float[4]; Platform.NovaScreen_SafeArea(r); return new Rect(r[0], r[1], r[2], r[3]); } }
        // 지금 화면 방향 (가로 = LandscapeLeft, 세로 = Portrait). 넣으면 그 방향으로 돌린다 (안드로이드 — AutoRotation 은 기기 방향을 따른다)
        public static ScreenOrientation orientation
        {
            get => (ScreenOrientation)Platform.NovaScreen_GetOrientation();
            set => Platform.NovaScreen_SetOrientation((int)value);
        }
    }

    // ------------------------------------------------------------------ Input
    public enum KeyCode
    {
        None = 0, Backspace = 8, Tab = 9, Clear = 12, Return = 13, Pause = 19, Escape = 27, Space = 32,
        Quote = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        Semicolon = 59, Equals = 61, LeftBracket = 91, Backslash = 92, RightBracket = 93, BackQuote = 96,
        A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Delete = 127,
        Keypad0 = 256, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        KeypadPeriod = 266, KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, KeypadEnter,
        UpArrow = 273, DownArrow, RightArrow, LeftArrow, Insert, Home, End, PageUp, PageDown,
        F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        CapsLock = 301, RightShift = 303, LeftShift, RightControl, LeftControl, RightAlt, LeftAlt,
        Mouse0 = 323, Mouse1, Mouse2, Mouse3, Mouse4, Mouse5, Mouse6,
    }

    public enum TouchPhase { Began, Moved, Stationary, Ended, Canceled }
    public struct Touch
    {
        public int fingerId;
        public Vector2 position, deltaPosition;
        public float deltaTime;
        public int tapCount;
        public TouchPhase phase;
        public Vector2 rawPosition => position;
        public float pressure => 1f;
        public float radius => 0f;
    }

    public static class Input
    {
        // KeyCode → Windows 가상 키 (마우스는 음수 = 버튼 번호)
        static int ToVk(KeyCode k)
        {
            int c = (int)k;
            if (k >= KeyCode.A && k <= KeyCode.Z) return 'A' + (c - (int)KeyCode.A);
            if (k >= KeyCode.Alpha0 && k <= KeyCode.Alpha9) return '0' + (c - (int)KeyCode.Alpha0);
            if (k >= KeyCode.Keypad0 && k <= KeyCode.Keypad9) return 0x60 + (c - (int)KeyCode.Keypad0);
            if (k >= KeyCode.F1 && k <= KeyCode.F12) return 0x70 + (c - (int)KeyCode.F1);
            if (k >= KeyCode.Mouse0 && k <= KeyCode.Mouse6) return -1 - (c - (int)KeyCode.Mouse0);
            switch (k)
            {
                case KeyCode.Backspace: return 0x08; case KeyCode.Tab: return 0x09; case KeyCode.Return: return 0x0D;
                case KeyCode.KeypadEnter: return 0x0D; case KeyCode.Pause: return 0x13; case KeyCode.Escape: return 0x1B;
                case KeyCode.Space: return 0x20; case KeyCode.Delete: return 0x2E; case KeyCode.Insert: return 0x2D;
                case KeyCode.Home: return 0x24; case KeyCode.End: return 0x23; case KeyCode.PageUp: return 0x21; case KeyCode.PageDown: return 0x22;
                case KeyCode.UpArrow: return 0x26; case KeyCode.DownArrow: return 0x28; case KeyCode.LeftArrow: return 0x25; case KeyCode.RightArrow: return 0x27;
                case KeyCode.LeftShift: return 0xA0; case KeyCode.RightShift: return 0xA1; case KeyCode.LeftControl: return 0xA2;
                case KeyCode.RightControl: return 0xA3; case KeyCode.LeftAlt: return 0xA4; case KeyCode.RightAlt: return 0xA5;
                case KeyCode.CapsLock: return 0x14; case KeyCode.Minus: return 0xBD; case KeyCode.Equals: return 0xBB;
                case KeyCode.Comma: return 0xBC; case KeyCode.Period: return 0xBE; case KeyCode.Slash: return 0xBF;
                case KeyCode.Semicolon: return 0xBA; case KeyCode.Quote: return 0xDE; case KeyCode.LeftBracket: return 0xDB;
                case KeyCode.RightBracket: return 0xDD; case KeyCode.Backslash: return 0xDC; case KeyCode.BackQuote: return 0xC0;
                case KeyCode.KeypadPeriod: return 0x6E; case KeyCode.KeypadDivide: return 0x6F; case KeyCode.KeypadMultiply: return 0x6A;
                case KeyCode.KeypadMinus: return 0x6D; case KeyCode.KeypadPlus: return 0x6B;
            }
            return 0;
        }

        static unsafe bool Key(KeyCode k, int mode)
        {
            int vk = ToVk(k);
            if (vk < 0) return Native.Api.Input_GetMouseButton(-1 - vk, mode) != 0;
            return vk != 0 && Native.Api.Input_GetKey(vk, mode) != 0;
        }

        public static bool GetKey(KeyCode key) => Key(key, 0);
        public static bool GetKeyDown(KeyCode key) => Key(key, 1);
        public static bool GetKeyUp(KeyCode key) => Key(key, 2);
        public static bool GetKey(string name) => Key(Parse(name), 0);
        public static bool GetKeyDown(string name) => Key(Parse(name), 1);
        public static bool GetKeyUp(string name) => Key(Parse(name), 2);

        static KeyCode Parse(string name)
        {
            name = (name ?? "").Trim().ToLowerInvariant();
            if (name.Length == 1 && name[0] >= 'a' && name[0] <= 'z') return KeyCode.A + (name[0] - 'a');
            if (name.Length == 1 && name[0] >= '0' && name[0] <= '9') return KeyCode.Alpha0 + (name[0] - '0');
            switch (name)
            {
                case "space": return KeyCode.Space; case "return": case "enter": return KeyCode.Return; case "escape": return KeyCode.Escape;
                case "tab": return KeyCode.Tab; case "backspace": return KeyCode.Backspace; case "up": return KeyCode.UpArrow;
                case "down": return KeyCode.DownArrow; case "left": return KeyCode.LeftArrow; case "right": return KeyCode.RightArrow;
                case "left shift": return KeyCode.LeftShift; case "right shift": return KeyCode.RightShift;
                case "left ctrl": return KeyCode.LeftControl; case "right ctrl": return KeyCode.RightControl;
                case "left alt": return KeyCode.LeftAlt; case "right alt": return KeyCode.RightAlt;
            }
            if (Enum.TryParse(name, true, out KeyCode k)) return k;
            return KeyCode.None;
        }

        // ---- 터치 (Unity 의 Input.touchCount · GetTouch · touches). 안드로이드 손가락, PC 는 Unity 처럼 0 개
        public static bool touchSupported => Application.platform == RuntimePlatform.Android;
        public static bool multiTouchEnabled { get => true; set { } }
        public static int touchCount => Platform.NovaInput_TouchCount();
        public static unsafe Touch GetTouch(int index)
        {
            float* t = stackalloc float[7];
            if (Platform.NovaInput_GetTouch(index, t) == 0) throw new ArgumentException("Index out of bounds.");
            return new Touch { fingerId = (int)t[0], position = new Vector2(t[1], t[2]), deltaPosition = new Vector2(t[3], t[4]), phase = (TouchPhase)(int)t[5], tapCount = (int)t[6], deltaTime = Time.deltaTime };
        }
        public static Touch[] touches
        {
            get
            {
                var all = new Touch[touchCount];
                for (int i = 0; i < all.Length; i++) all[i] = GetTouch(i);
                return all;
            }
        }

        public static unsafe bool GetMouseButton(int button) => Native.Api.Input_GetMouseButton(button, 0) != 0;
        public static unsafe bool GetMouseButtonDown(int button) => Native.Api.Input_GetMouseButton(button, 1) != 0;
        public static unsafe bool GetMouseButtonUp(int button) => Native.Api.Input_GetMouseButton(button, 2) != 0;
        public static unsafe Vector3 mousePosition { get { Vector4 m; Native.Api.Input_GetMouse(&m); return new Vector3(m.x, m.y, 0); } }
        public static unsafe Vector2 mouseScrollDelta { get { Vector4 m; Native.Api.Input_GetMouse(&m); return new Vector2(m.z, m.w); } }
        public static bool anyKey { get { for (int vk = 8; vk < 256; vk++) if (Native_Key(vk, 0)) return true; return GetMouseButton(0) || GetMouseButton(1); } }
        public static bool anyKeyDown { get { for (int vk = 8; vk < 256; vk++) if (Native_Key(vk, 1)) return true; return GetMouseButtonDown(0) || GetMouseButtonDown(1); } }
        static unsafe bool Native_Key(int vk, int mode) => Native.Api.Input_GetKey(vk, mode) != 0;

        // ---- 가상 축 (Unity 기본 Input Manager 와 같은 이름·값)
        class Axis { public float value; public int frame = -1; }
        static readonly Dictionary<string, Axis> s_Axes = new Dictionary<string, Axis>();
        static Vector3 s_LastMouse; static int s_MouseFrame = -1; static Vector2 s_MouseDelta;

        static float Raw(string axisName)
        {
            switch (axisName)
            {
                case "Horizontal": return (GetKey(KeyCode.D) || GetKey(KeyCode.RightArrow) ? 1f : 0f) - (GetKey(KeyCode.A) || GetKey(KeyCode.LeftArrow) ? 1f : 0f);
                case "Vertical": return (GetKey(KeyCode.W) || GetKey(KeyCode.UpArrow) ? 1f : 0f) - (GetKey(KeyCode.S) || GetKey(KeyCode.DownArrow) ? 1f : 0f);
                case "Mouse X": UpdateMouse(); return s_MouseDelta.x * 0.1f;
                case "Mouse Y": UpdateMouse(); return s_MouseDelta.y * 0.1f;
                case "Mouse ScrollWheel": return mouseScrollDelta.y * 0.1f;
                case "Jump": return GetKey(KeyCode.Space) ? 1f : 0f;
                case "Fire1": return GetKey(KeyCode.LeftControl) || GetMouseButton(0) ? 1f : 0f;
                case "Fire2": return GetKey(KeyCode.LeftAlt) || GetMouseButton(1) ? 1f : 0f;
                case "Fire3": return GetKey(KeyCode.LeftShift) || GetMouseButton(2) ? 1f : 0f;
                case "Submit": return GetKey(KeyCode.Return) || GetKey(KeyCode.Space) ? 1f : 0f;
                case "Cancel": return GetKey(KeyCode.Escape) ? 1f : 0f;
            }
            throw new ArgumentException($"Input Axis {axisName} is not setup.");
        }

        static void UpdateMouse()
        {
            if (s_MouseFrame == Time.frameCount) return;
            Vector3 m = mousePosition;
            s_MouseDelta = s_MouseFrame < 0 ? Vector2.zero : new Vector2(m.x - s_LastMouse.x, m.y - s_LastMouse.y);
            s_LastMouse = m;
            s_MouseFrame = Time.frameCount;
        }

        public static float GetAxisRaw(string axisName) => Raw(axisName);

        // Unity 기본값: gravity 3, sensitivity 3, snap (방향이 바뀌면 0 부터)
        public static float GetAxis(string axisName)
        {
            if (axisName.StartsWith("Mouse")) return Raw(axisName);
            if (!s_Axes.TryGetValue(axisName, out var a)) s_Axes[axisName] = a = new Axis();
            if (a.frame != Time.frameCount)
            {
                float target = Raw(axisName), dt = Time.unscaledDeltaTime;
                if (target != 0 && Math.Sign(target) != Math.Sign(a.value) && a.value != 0) a.value = 0;
                a.value = target != 0 ? Mathf.MoveTowards(a.value, target, 3f * dt) : Mathf.MoveTowards(a.value, 0, 3f * dt);
                a.frame = Time.frameCount;
            }
            return a.value;
        }

        public static bool GetButton(string buttonName) => Raw(buttonName) != 0;
        public static bool GetButtonDown(string buttonName) => ButtonEdge(buttonName, 1);
        public static bool GetButtonUp(string buttonName) => ButtonEdge(buttonName, 2);
        static bool ButtonEdge(string b, int mode)
        {
            switch (b)
            {
                case "Jump": return Key(KeyCode.Space, mode);
                case "Fire1": return Key(KeyCode.LeftControl, mode) || Key(KeyCode.Mouse0, mode);
                case "Fire2": return Key(KeyCode.LeftAlt, mode) || Key(KeyCode.Mouse1, mode);
                case "Fire3": return Key(KeyCode.LeftShift, mode) || Key(KeyCode.Mouse2, mode);
                case "Submit": return Key(KeyCode.Return, mode) || Key(KeyCode.Space, mode);
                case "Cancel": return Key(KeyCode.Escape, mode);
            }
            throw new ArgumentException($"Input Button {b} is not setup.");
        }
    }
}
