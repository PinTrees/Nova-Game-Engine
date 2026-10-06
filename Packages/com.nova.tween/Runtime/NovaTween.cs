using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace NovaEngine.Tweening
{
    // 값의 종류마다 섞기 · 더하기 · 빼기 (트윈이 시작 값 + 변화량 × 곡선으로 값을 만든다)
    public interface ITweenPlugin<T>
    {
        T Lerp(T a, T b, float t);
        T Add(T a, T b);
        T Subtract(T a, T b);
        T Scale(T a, int times);
        float Distance(T a, T b);
    }

    internal sealed class FloatPlugin : ITweenPlugin<float>
    {
        internal static readonly FloatPlugin I = new FloatPlugin();
        public float Lerp(float a, float b, float t) => a + (b - a) * t;
        public float Add(float a, float b) => a + b;
        public float Subtract(float a, float b) => a - b;
        public float Scale(float a, int n) => a * n;
        public float Distance(float a, float b) => Math.Abs(b - a);
    }
    internal sealed class IntPlugin : ITweenPlugin<int>
    {
        internal static readonly IntPlugin I = new IntPlugin();
        public int Lerp(int a, int b, float t) => (int)MathF.Round(a + (b - a) * t);
        public int Add(int a, int b) => a + b;
        public int Subtract(int a, int b) => a - b;
        public int Scale(int a, int n) => a * n;
        public float Distance(int a, int b) => Math.Abs(b - a);
    }
    internal sealed class Vector2Plugin : ITweenPlugin<Vector2>
    {
        internal static readonly Vector2Plugin I = new Vector2Plugin();
        public Vector2 Lerp(Vector2 a, Vector2 b, float t) => new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        public Vector2 Add(Vector2 a, Vector2 b) => new Vector2(a.x + b.x, a.y + b.y);
        public Vector2 Subtract(Vector2 a, Vector2 b) => new Vector2(a.x - b.x, a.y - b.y);
        public Vector2 Scale(Vector2 a, int n) => new Vector2(a.x * n, a.y * n);
        public float Distance(Vector2 a, Vector2 b) => (b - a).magnitude;
    }
    internal sealed class Vector3Plugin : ITweenPlugin<Vector3>
    {
        internal static readonly Vector3Plugin I = new Vector3Plugin();
        public Vector3 Lerp(Vector3 a, Vector3 b, float t) => new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
        public Vector3 Add(Vector3 a, Vector3 b) => a + b;
        public Vector3 Subtract(Vector3 a, Vector3 b) => a - b;
        public Vector3 Scale(Vector3 a, int n) => a * n;
        public float Distance(Vector3 a, Vector3 b) => (b - a).magnitude;
    }
    internal sealed class Vector4Plugin : ITweenPlugin<Vector4>
    {
        internal static readonly Vector4Plugin I = new Vector4Plugin();
        public Vector4 Lerp(Vector4 a, Vector4 b, float t) => new Vector4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        public Vector4 Add(Vector4 a, Vector4 b) => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
        public Vector4 Subtract(Vector4 a, Vector4 b) => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
        public Vector4 Scale(Vector4 a, int n) => new Vector4(a.x * n, a.y * n, a.z * n, a.w * n);
        public float Distance(Vector4 a, Vector4 b) { var d = Subtract(b, a); return MathF.Sqrt(d.x * d.x + d.y * d.y + d.z * d.z + d.w * d.w); }
    }
    internal sealed class ColorPlugin : ITweenPlugin<Color>
    {
        internal static readonly ColorPlugin I = new ColorPlugin();
        public Color Lerp(Color a, Color b, float t) => new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
        public Color Add(Color a, Color b) => new Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
        public Color Subtract(Color a, Color b) => new Color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
        public Color Scale(Color a, int n) => new Color(a.r * n, a.g * n, a.b * n, a.a * n);
        public float Distance(Color a, Color b) => MathF.Abs(b.r - a.r) + MathF.Abs(b.g - a.g) + MathF.Abs(b.b - a.b) + MathF.Abs(b.a - a.a);
    }
    // 회전: 가장 짧은 길로 (Slerp)
    internal sealed class QuaternionPlugin : ITweenPlugin<Quaternion>
    {
        internal static readonly QuaternionPlugin I = new QuaternionPlugin();
        public Quaternion Lerp(Quaternion a, Quaternion b, float t) => Quaternion.SlerpUnclamped(a, b, t);
        public Quaternion Add(Quaternion a, Quaternion b) => a * b;
        public Quaternion Subtract(Quaternion a, Quaternion b) => Quaternion.Inverse(b) * a;
        public Quaternion Scale(Quaternion a, int n) { var r = Quaternion.identity; for (int i = 0; i < n; i++) r *= a; return r; }
        public float Distance(Quaternion a, Quaternion b) => Quaternion.Angle(a, b);
    }
    // 글자: 앞에서부터 하나씩 (타자기)
    internal sealed class StringPlugin : ITweenPlugin<string>
    {
        internal static readonly StringPlugin I = new StringPlugin();
        public string Lerp(string a, string b, float t)
        {
            a ??= ""; b ??= "";
            int n = (int)MathF.Round(b.Length * Math.Clamp(t, 0f, 1f));
            return n >= b.Length ? b : b.Substring(0, n) + (n < a.Length && !b.StartsWith(a, StringComparison.Ordinal) ? a.Substring(Math.Min(n, a.Length)) : "");
        }
        public string Add(string a, string b) => (a ?? "") + (b ?? "");
        public string Subtract(string a, string b) => a;
        public string Scale(string a, int n) => a;
        public float Distance(string a, string b) => (b ?? "").Length;
    }

    // 트윈 엔진: 살아 있는 트윈을 프레임마다 진행한다 (Play 중 숨긴 "[NovaTween]" 오브젝트의 Update · LateUpdate · FixedUpdate).
    //  Unity 의 대표 트윈 도구 (DOTween) 와 같은 쓰임 — DOTween.To → NovaTween.To, DOTween.Sequence() → NovaTween.Sequence()
    public static class NovaTween
    {
        public static Ease defaultEaseType = Ease.OutQuad;
        public static bool defaultAutoKill = true;
        public static AutoPlay defaultAutoPlay = AutoPlay.All;
        public static LoopType defaultLoopType = LoopType.Restart;
        public static bool defaultTimeScaleIndependent;
        public static UpdateType defaultUpdateType = UpdateType.Normal;
        public static float timeScale = 1f;
        public static bool useSafeMode = true;

        static readonly List<Tween> s_Active = new List<Tween>();
        static TweenRunner s_Runner;
        static bool s_Updating;

        public static void Init(bool recycleAllByDefault = false, bool useSafeMode = true) { NovaTween.useSafeMode = useSafeMode; EnsureRunner(); }
        public static void SetTweensCapacity(int tweenersCapacity, int sequencesCapacity) { if (s_Active.Capacity < tweenersCapacity + sequencesCapacity) s_Active.Capacity = tweenersCapacity + sequencesCapacity; }

        static void EnsureRunner()
        {
            if (s_Runner != null && s_Runner) return;
            var go = new GameObject("[NovaTween]");
            Object.DontDestroyOnLoad(go);
            s_Runner = go.AddComponent<TweenRunner>();
        }

        internal static void Register(Tween t)
        {
            if (t.m_Parent != null || t.m_Registered || t.m_Killed) return;
            t.m_Registered = true;
            s_Active.Add(t);
            EnsureRunner();
        }

        internal static void Unregister(Tween t)
        {
            if (!t.m_Registered) return;
            t.m_Registered = false;
            if (!s_Updating) s_Active.Remove(t);
        }

        // 새 트윈: 다음 프레임부터 (autoPlay) — 만든 줄에서 SetDelay · SetLoops 등을 붙일 수 있게
        static T Spawn<T>(T t) where T : Tween
        {
            t.m_UpdateType = defaultUpdateType;
            t.m_IgnoreTimeScale = defaultTimeScaleIndependent;
            bool auto = t is Sequence ? defaultAutoPlay == AutoPlay.All || defaultAutoPlay == AutoPlay.AutoPlaySequences
                                      : defaultAutoPlay == AutoPlay.All || defaultAutoPlay == AutoPlay.AutoPlayTweeners;
            Register(t);
            if (auto) t.m_Playing = true;
            return t;
        }

        // ---- 만들기
        public static TweenerCore<float> To(DOGetter<float> getter, DOSetter<float> setter, float endValue, float duration) => Spawn(new TweenerCore<float>(getter, setter, endValue, duration, FloatPlugin.I));
        public static TweenerCore<int> To(DOGetter<int> getter, DOSetter<int> setter, int endValue, float duration) => Spawn(new TweenerCore<int>(getter, setter, endValue, duration, IntPlugin.I));
        public static TweenerCore<Vector2> To(DOGetter<Vector2> getter, DOSetter<Vector2> setter, Vector2 endValue, float duration) => Spawn(new TweenerCore<Vector2>(getter, setter, endValue, duration, Vector2Plugin.I));
        public static TweenerCore<Vector3> To(DOGetter<Vector3> getter, DOSetter<Vector3> setter, Vector3 endValue, float duration) => Spawn(new TweenerCore<Vector3>(getter, setter, endValue, duration, Vector3Plugin.I));
        public static TweenerCore<Vector4> To(DOGetter<Vector4> getter, DOSetter<Vector4> setter, Vector4 endValue, float duration) => Spawn(new TweenerCore<Vector4>(getter, setter, endValue, duration, Vector4Plugin.I));
        public static TweenerCore<Quaternion> To(DOGetter<Quaternion> getter, DOSetter<Quaternion> setter, Quaternion endValue, float duration) => Spawn(new TweenerCore<Quaternion>(getter, setter, endValue, duration, QuaternionPlugin.I));
        public static TweenerCore<Color> To(DOGetter<Color> getter, DOSetter<Color> setter, Color endValue, float duration) => Spawn(new TweenerCore<Color>(getter, setter, endValue, duration, ColorPlugin.I));
        public static TweenerCore<string> To(DOGetter<string> getter, DOSetter<string> setter, string endValue, float duration) => Spawn(new TweenerCore<string>(getter, setter, endValue, duration, StringPlugin.I));
        // 투명도만 (색의 a)
        public static TweenerCore<Color> ToAlpha(DOGetter<Color> getter, DOSetter<Color> setter, float endValue, float duration)
        {
            var t = To(getter, setter, new Color(0, 0, 0, endValue), duration);
            t.m_Custom = (s, e, k) => { Color c = getter(); c.a = s.a + (e.a - s.a) * k; return c; };   // 색은 그대로, a 만
            return t;
        }
        // 0 → 1 (또는 from → to) 로 가며 값을 알려 준다 (대상 없이)
        public static Tweener To(DOSetter<float> setter, float startValue, float endValue, float duration)
        {
            float v = startValue;
            var t = To(() => v, x => { v = x; setter(x); }, endValue, duration);
            return t;
        }
        // 흔들기 · 튀기기 · 뛰기처럼 시작 값에 곡선을 더하는 트윈
        internal static TweenerCore<Vector3> Offset(DOGetter<Vector3> getter, DOSetter<Vector3> setter, float duration, Func<float, Vector3> offsetAt)
        {
            var t = To(getter, setter, Vector3.zero, duration);
            t.m_IsRelative = true;
            t.m_Custom = (s, e, k) => s + offsetAt(k);
            t.m_Ease = Ease.Linear;
            return t;
        }

        public static Sequence Sequence() => Spawn(new Sequence());
        public static Sequence Sequence(object target) { var s = Sequence(); s.target = target; return s; }

        // ---- 한꺼번에 (대상 · id 로)
        static bool Match(Tween t, object targetOrId) =>
            targetOrId == null || Equals(t.target, targetOrId) || Equals(t.id, targetOrId) || (targetOrId is string s && t.stringId == s) || (targetOrId is int i && t.intId == i);

        static int ForEach(object targetOrId, Action<Tween> action)
        {
            int n = 0;
            foreach (Tween t in s_Active.ToArray())
                if (!t.m_Killed && Match(t, targetOrId)) { action(t); n++; }
            return n;
        }

        public static int Kill(object targetOrId, bool complete = false) => targetOrId == null ? 0 : ForEach(targetOrId, t => t.Kill(complete));
        public static int KillAll(bool complete = false) => ForEach(null, t => t.Kill(complete));
        public static int Complete(object targetOrId, bool withCallbacks = false) => ForEach(targetOrId, t => t.Complete(withCallbacks));
        public static int CompleteAll(bool withCallbacks = false) => ForEach(null, t => t.Complete(withCallbacks));
        public static int Pause(object targetOrId) => ForEach(targetOrId, t => t.Pause());
        public static int PauseAll() => ForEach(null, t => t.Pause());
        public static int Play(object targetOrId) => ForEach(targetOrId, t => t.Play());
        public static int PlayAll() => ForEach(null, t => t.Play());
        public static int Restart(object targetOrId, bool includeDelay = true) => ForEach(targetOrId, t => t.Restart(includeDelay));
        public static int RestartAll(bool includeDelay = true) => ForEach(null, t => t.Restart(includeDelay));
        public static int Rewind(object targetOrId, bool includeDelay = true) => ForEach(targetOrId, t => t.Rewind(includeDelay));
        public static int RewindAll(bool includeDelay = true) => ForEach(null, t => t.Rewind(includeDelay));
        public static int TogglePause(object targetOrId) => ForEach(targetOrId, t => t.TogglePause());
        public static int Goto(object targetOrId, float to, bool andPlay = false) => ForEach(targetOrId, t => t.Goto(to, andPlay));
        public static int Flip(object targetOrId) => ForEach(targetOrId, t => t.Flip());
        public static int PlayForward(object targetOrId) => ForEach(targetOrId, t => t.PlayForward());
        public static int PlayBackwards(object targetOrId) => ForEach(targetOrId, t => t.PlayBackwards());

        public static bool IsTweening(object targetOrId, bool alsoCheckIfIsPlaying = false)
        {
            foreach (Tween t in s_Active)
                if (!t.m_Killed && Match(t, targetOrId) && (!alsoCheckIfIsPlaying || t.m_Playing)) return true;
            return false;
        }
        public static int TotalActiveTweens() { int n = 0; foreach (Tween t in s_Active) if (!t.m_Killed) n++; return n; }
        public static int TotalPlayingTweens() { int n = 0; foreach (Tween t in s_Active) if (t.m_Playing && !t.m_Killed) n++; return n; }
        public static List<Tween> PlayingTweens() { var l = new List<Tween>(); foreach (Tween t in s_Active) if (t.m_Playing && !t.m_Killed) l.Add(t); return l; }
        public static List<Tween> TweensByTarget(object target, bool playingOnly = false)
        {
            var l = new List<Tween>();
            foreach (Tween t in s_Active) if (!t.m_Killed && Equals(t.target, target) && (!playingOnly || t.m_Playing)) l.Add(t);
            return l;
        }

        // Play 를 멈출 때 · 엔진 쪽 정리 (Play 마다 정적 상태가 남지 않게)
        public static void Clear(bool destroy = false)
        {
            foreach (Tween t in s_Active.ToArray()) { t.m_Killed = true; t.m_Playing = false; t.m_Registered = false; }
            s_Active.Clear();
            if (destroy && s_Runner != null && s_Runner) Object.Destroy(s_Runner.gameObject);
            s_Runner = null;
        }

        // UpdateType.Manual 트윈을 직접 진행
        public static void ManualUpdate(float deltaTime, float unscaledDeltaTime) => Step(UpdateType.Manual, deltaTime, unscaledDeltaTime);

        internal static void Step(UpdateType type, float dt, float unscaledDt)
        {
            if (s_Active.Count == 0) return;
            s_Updating = true;
            try
            {
                for (int i = 0; i < s_Active.Count; i++)
                {
                    Tween t = s_Active[i];
                    if (t.m_Killed || !t.m_Playing || t.m_UpdateType != type) continue;
                    try { t.Tick((t.m_IgnoreTimeScale ? unscaledDt : dt) * timeScale); }
                    catch (Exception e) { Debug.LogException(e); t.Kill(); }
                }
            }
            finally { s_Updating = false; }
            s_Active.RemoveAll(t => t.m_Killed || !t.m_Registered);
        }

        internal static void OnRunnerDestroyed(TweenRunner runner)
        {
            if (s_Runner == runner) Clear();
        }
    }

    // 프레임마다 트윈 진행 (숨긴 오브젝트 하나 — Play 를 멈추면 함께 사라지고 트윈도 정리된다)
    public sealed class TweenRunner : MonoBehaviour
    {
        void Update() => NovaTween.Step(UpdateType.Normal, Time.deltaTime, Time.unscaledDeltaTime);
        void LateUpdate() => NovaTween.Step(UpdateType.Late, Time.deltaTime, Time.unscaledDeltaTime);
        void FixedUpdate() => NovaTween.Step(UpdateType.Fixed, Time.fixedDeltaTime, Time.fixedDeltaTime);
        void OnDestroy() => NovaTween.OnRunnerDestroyed(this);
    }

    // DOVirtual: 대상 없는 값 트윈 · 늦게 부르기
    public static class DOVirtual
    {
        public static Tweener Float(float from, float to, float duration, TweenCallback<float> onVirtualUpdate)
        {
            float v = from;
            return NovaTween.To(() => v, x => { v = x; onVirtualUpdate?.Invoke(x); }, to, duration);
        }
        public static Tweener Int(int from, int to, float duration, TweenCallback<int> onVirtualUpdate)
        {
            int v = from;
            return NovaTween.To(() => v, x => { v = x; onVirtualUpdate?.Invoke(x); }, to, duration);
        }
        public static Tweener Vector3(Vector3 from, Vector3 to, float duration, TweenCallback<Vector3> onVirtualUpdate)
        {
            Vector3 v = from;
            return NovaTween.To(() => v, x => { v = x; onVirtualUpdate?.Invoke(x); }, to, duration);
        }
        public static Tweener Color(Color from, Color to, float duration, TweenCallback<Color> onVirtualUpdate)
        {
            Color v = from;
            return NovaTween.To(() => v, x => { v = x; onVirtualUpdate?.Invoke(x); }, to, duration);
        }
        public static float EasedValue(float from, float to, float lifetimePercentage, Ease easeType) => from + (to - from) * EaseManager.Evaluate(easeType, lifetimePercentage);
        public static Tween DelayedCall(float delay, TweenCallback callback, bool ignoreTimeScale = true)
        {
            var s = NovaTween.Sequence();
            s.AppendInterval(delay).AppendCallback(callback);
            s.m_IgnoreTimeScale = ignoreTimeScale;
            return s;
        }
    }
}
