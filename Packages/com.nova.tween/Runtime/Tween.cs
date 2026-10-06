using System;
using System.Collections.Generic;

namespace NovaEngine.Tweening
{
    public enum LoopType { Restart, Yoyo, Incremental }
    // 어느 업데이트에서 진행할지: Normal = Update, Late = LateUpdate, Fixed = FixedUpdate, Manual = NovaTween.ManualUpdate 로만
    public enum UpdateType { Normal, Late, Fixed, Manual }
    public enum RotateMode { Fast, FastBeyond360, WorldAxisAdd, LocalAxisAdd }
    public enum AutoPlay { None, AutoPlaySequences, AutoPlayTweeners, All }
    public enum ScrambleMode { None, All, Uppercase, Lowercase, Numerals }

    public delegate void TweenCallback();
    public delegate void TweenCallback<in T>(T value);
    public delegate T DOGetter<out T>();
    public delegate void DOSetter<in T>(T value);

    // 모든 트윈 (값 하나를 바꾸는 Tweener · 여러 트윈을 차례로 묶는 Sequence) 의 공통 부분: 시간 (지연 · 반복 · 거꾸로), 곡선, 알림
    public abstract class Tween
    {
        public float timeScale = 1f;
        public bool isBackwards;
        public object id;
        public string stringId;
        public int intId = -999;
        public object target;

        public TweenCallback onPlay, onPause, onRewind, onUpdate, onStepComplete, onComplete, onKill;
        public TweenCallback<int> onWaypointChange;
        internal TweenCallback onStart;
        internal Action m_StartHook;         // 단축 메서드의 시작 준비 (경로 · LookAt · 축 더하기) — 사용자 OnStart 와 따로

        internal float m_Duration;           // 한 번 도는 길이 (초)
        internal int m_Loops = 1;            // -1 = 끝없이
        internal LoopType m_LoopType = LoopType.Restart;
        internal float m_Delay, m_DelayElapsed;
        internal Ease m_Ease = Ease.OutQuad;
        internal EaseFunction m_CustomEase;
        internal float m_Overshoot = 1.70158f, m_Period;
        internal bool m_AutoKill = true;
        internal bool m_IsRelative, m_IsFrom, m_SpeedBased;
        internal UpdateType m_UpdateType = UpdateType.Normal;
        internal bool m_IgnoreTimeScale;
        internal bool m_Playing, m_Complete, m_Killed, m_Started, m_PlayedOnce;
        internal float m_Position;           // 전체 위치 (지연 빼고, 0 ~ 길이 × 반복)
        internal int m_CompletedLoops;
        internal Sequence m_Parent;          // Sequence 안이면 그쪽이 시간을 준다
        internal bool m_Registered;

        protected Tween()
        {
            m_Ease = NovaTween.defaultEaseType;
            m_AutoKill = NovaTween.defaultAutoKill;
            m_LoopType = NovaTween.defaultLoopType;
        }

        public float Duration(bool includeLoops = true) => includeLoops ? (m_Loops < 0 ? float.PositiveInfinity : m_Duration * m_Loops) : m_Duration;
        internal float FullDuration => m_Loops < 0 ? float.PositiveInfinity : m_Duration * Math.Max(1, m_Loops);
        // Sequence 안에서 차지하는 길이 (끝없는 반복은 한 번으로)
        internal float SequencedDuration => m_Delay + m_Duration * Math.Max(1, m_Loops);

        // 시작 값 잡기 (지연이 끝나 처음 움직일 때 — 앞 트윈이 바꾼 값에서 이어진다)
        internal abstract void Startup();
        // 한 바퀴 안의 위치 → 값 (eased = 곡선 값, loopIndex = Incremental 의 몇 번째)
        internal abstract void Apply(float eased, int loopIndex);
        internal virtual void OnKilledInternal() { }

        internal float EasedAt(float t) => m_CustomEase != null ? m_CustomEase(t * m_Duration, m_Duration, m_Overshoot, m_Period) : EaseManager.Evaluate(m_Ease, t, m_Overshoot, m_Period);

        internal void EnsureStarted()
        {
            if (m_Started) return;
            m_Started = true;
            m_StartHook?.Invoke();
            Startup();
            onStart?.Invoke();
        }

        // 한 프레임 진행 (dt = 이미 timeScale 을 곱한 초)
        internal void Tick(float dt)
        {
            if (!m_Playing || m_Killed) return;
            if (target is Object o && !o) { Kill(); return; }   // 대상이 지워졌으면 조용히 끝 (안전 모드)
            dt *= timeScale;
            if (!isBackwards && m_DelayElapsed < m_Delay)
            {
                m_DelayElapsed += dt;
                if (m_DelayElapsed < m_Delay) return;
                dt = m_DelayElapsed - m_Delay;
                m_DelayElapsed = m_Delay;
            }
            EnsureStarted();
            GotoInternal(m_Position + (isBackwards ? -dt : dt), true);
        }

        // 전체 위치로 (반복 · Yoyo · 끝 · 알림)
        internal void GotoInternal(float position, bool fromUpdate)
        {
            if (m_Killed) return;
            float full = FullDuration;
            if (position < 0f) position = 0f;
            if (position > full) position = full;
            int prevLoops = m_CompletedLoops;
            bool complete = !float.IsInfinity(full) && position >= full;
            int loopIndex;
            float local;
            if (m_Duration <= 0f) { loopIndex = complete ? Math.Max(1, m_Loops) - 1 : 0; local = complete || position > 0f ? 1f : 0f; }
            else
            {
                loopIndex = (int)(position / m_Duration);
                local = (position - loopIndex * m_Duration) / m_Duration;
                if (complete) { loopIndex = Math.Max(1, m_Loops) - 1; local = 1f; }
                else if (local == 0f && loopIndex > 0 && isBackwards) { loopIndex--; local = 1f; }
            }
            m_Position = position;
            m_CompletedLoops = complete ? Math.Max(1, m_Loops) : loopIndex;
            float t = m_LoopType == LoopType.Yoyo && (loopIndex & 1) == 1 ? 1f - local : local;
            Apply(EasedAt(t), m_LoopType == LoopType.Incremental ? loopIndex : 0);
            if (!fromUpdate && m_Parent != null) return;
            onUpdate?.Invoke();
            if (m_CompletedLoops != prevLoops && onStepComplete != null)
                for (int i = 0, n = Math.Abs(m_CompletedLoops - prevLoops); i < n; i++) onStepComplete();
            if (complete && !isBackwards && fromUpdate)
            {
                m_Playing = false;
                if (!m_Complete)
                {
                    m_Complete = true;
                    onComplete?.Invoke();
                }
                if (m_AutoKill) Kill();
            }
            else if (position <= 0f && isBackwards && fromUpdate)
            {
                m_Playing = false;
                onRewind?.Invoke();
            }
            else m_Complete = complete;
        }

        // ---- 제어 (Unity 의 대표 트윈 도구와 같은 이름)
        public void Play()
        {
            if (m_Killed || m_Playing) return;
            if (m_Complete && !isBackwards) return;
            m_Playing = true;
            NovaTween.Register(this);
            onPlay?.Invoke();
        }
        public void Pause() { if (m_Playing) { m_Playing = false; onPause?.Invoke(); } }
        public void TogglePause() { if (m_Playing) Pause(); else Play(); }
        public void PlayForward() { isBackwards = false; m_Complete = m_Position >= FullDuration; Play(); }
        public void PlayBackwards() { isBackwards = true; Play(); }
        public void Flip() { isBackwards = !isBackwards; }

        public void Restart(bool includeDelay = true)
        {
            if (m_Killed) return;
            isBackwards = false;
            m_Complete = false;
            m_DelayElapsed = includeDelay ? 0f : m_Delay;
            if (m_Started) GotoInternal(0f, false);
            m_Position = 0f;
            m_CompletedLoops = 0;
            m_Playing = false;
            Play();
        }

        public void Rewind(bool includeDelay = true)
        {
            if (m_Killed) return;
            m_Playing = false;
            m_Complete = false;
            if (m_Started) GotoInternal(0f, false);
            m_Position = 0f;
            m_CompletedLoops = 0;
            if (includeDelay) m_DelayElapsed = 0f;
            onRewind?.Invoke();
        }

        // 끝으로 바로 (OnComplete 를 부르고 autoKill 이면 끝낸다). 끝없는 반복은 무시
        public void Complete() => Complete(false);
        public void Complete(bool withCallbacks)
        {
            if (m_Killed || m_Loops < 0) return;
            EnsureStarted();
            m_DelayElapsed = m_Delay;
            isBackwards = false;
            GotoInternal(FullDuration, true);
        }

        // 위치로 바로 (초, 지연 빼고). andPlay = 그 자리에서 이어서 재생
        public void Goto(float to, bool andPlay = false)
        {
            if (m_Killed) return;
            EnsureStarted();
            m_DelayElapsed = m_Delay;
            m_Complete = false;
            GotoInternal(to, false);
            if (andPlay) Play(); else Pause();
        }

        public void Kill(bool complete = false)
        {
            if (m_Killed) return;
            if (complete && m_Loops >= 0) { EnsureStarted(); GotoInternal(FullDuration, false); }
            m_Killed = true;
            m_Playing = false;
            OnKilledInternal();
            onKill?.Invoke();
            NovaTween.Unregister(this);
        }

        // ---- 상태
        public bool IsActive() => !m_Killed;
        public bool IsPlaying() => m_Playing && !m_Killed;
        public bool IsComplete() => m_Complete;
        public bool IsBackwards() => isBackwards;
        public bool IsInitialized() => m_Started;
        public float Delay() => m_Delay;
        public float ElapsedDelay() => m_DelayElapsed;
        public float Elapsed(bool includeLoops = true) => includeLoops ? m_Position : (m_Duration > 0f ? m_Position - m_CompletedLoops * m_Duration : 0f);
        public float ElapsedPercentage(bool includeLoops = true)
        {
            if (includeLoops) return float.IsInfinity(FullDuration) || FullDuration <= 0f ? 0f : m_Position / FullDuration;
            return m_Duration <= 0f ? 1f : Elapsed(false) / m_Duration;
        }
        public int CompletedLoops() => m_CompletedLoops;
        public int Loops() => m_Loops;
    }

    // 값 하나 (float · int · Vector2/3/4 · Quaternion · Color · string) 를 getter / setter 로 바꾸는 트윈
    public abstract class Tweener : Tween
    {
        // 끝 값 · 시간을 바꾼다 (snapStartValue = 지금 값에서 다시 시작)
        public abstract Tweener ChangeEndValue(object newEndValue, float newDuration = -1f, bool snapStartValue = false);
    }

    public sealed class TweenerCore<T> : Tweener
    {
        internal readonly DOGetter<T> getter;
        internal readonly DOSetter<T> setter;
        public T startValue, endValue, changeValue;
        internal T m_FromValue;
        internal bool m_HasFromValue;
        readonly ITweenPlugin<T> m_Plugin;
        internal Func<T, T, float, T> m_Custom;   // (시작, 끝, 곡선 값) → 값 (흔들기 · 튀기기 · 뛰기)
        internal float m_Speed;

        internal TweenerCore(DOGetter<T> getter, DOSetter<T> setter, T endValue, float duration, ITweenPlugin<T> plugin)
        {
            this.getter = getter;
            this.setter = setter;
            this.endValue = endValue;
            m_Duration = Math.Max(0f, duration);
            m_Speed = duration;
            m_Plugin = plugin;
        }

        internal override void Startup()
        {
            T current = getter();
            if (m_IsFrom)
            {
                // From: 준 값에서 지금 값으로 (Relative 면 지금 값 + 준 값에서)
                T from = m_HasFromValue ? m_FromValue : endValue;
                if (m_IsRelative) from = m_Plugin.Add(current, from);
                endValue = current;
                startValue = from;
            }
            else
            {
                startValue = current;
                if (m_IsRelative) endValue = m_Plugin.Add(current, endValue);
            }
            changeValue = m_Plugin.Subtract(endValue, startValue);
            if (m_SpeedBased)
            {
                float distance = m_Plugin.Distance(startValue, endValue);
                m_Duration = m_Speed > 0f ? distance / m_Speed : 0f;
            }
            if (m_IsFrom) setter(startValue);
        }

        internal override void Apply(float eased, int loopIndex)
        {
            T start = startValue;
            if (loopIndex > 0) start = m_Plugin.Add(startValue, m_Plugin.Scale(changeValue, loopIndex));
            T end = m_Plugin.Add(start, changeValue);
            T value = m_Custom != null ? m_Custom(start, end, eased) : m_Plugin.Lerp(start, end, eased);
            try { setter(value); }
            catch (Exception e) { Debug.LogException(e); Kill(); }
        }

        public override Tweener ChangeEndValue(object newEndValue, float newDuration = -1f, bool snapStartValue = false)
        {
            if (newEndValue is T v) endValue = v;
            if (newDuration >= 0f) m_Duration = newDuration;
            if (snapStartValue || !m_Started)
            {
                m_Started = false;
                m_IsRelative = false;
                EnsureStarted();
            }
            else changeValue = m_Plugin.Subtract(endValue, startValue);
            return this;
        }

        public TweenerCore<T> ChangeStartValue(T newStartValue, float newDuration = -1f)
        {
            EnsureStarted();
            startValue = newStartValue;
            changeValue = m_Plugin.Subtract(endValue, startValue);
            if (newDuration >= 0f) m_Duration = newDuration;
            return this;
        }

        public TweenerCore<T> ChangeValues(T newStartValue, T newEndValue, float newDuration = -1f)
        {
            EnsureStarted();
            startValue = newStartValue;
            endValue = newEndValue;
            changeValue = m_Plugin.Subtract(endValue, startValue);
            if (newDuration >= 0f) m_Duration = newDuration;
            return this;
        }
    }

    // 여러 트윈 · 콜백을 시간표에 놓는다 (Append = 뒤에, Join = 앞 것과 같이, Insert = 그 시각에)
    public sealed class Sequence : Tween
    {
        struct Item { public float At; public Tween Tween; public TweenCallback Callback; }
        readonly List<Item> m_Items = new List<Item>();
        float m_LastAppendAt;
        float m_LastLocal;   // 지난 Apply 의 시퀀스 안 위치 (콜백 지나침 판정)

        internal Sequence() { m_Ease = Ease.Linear; m_Started = false; }

        Sequence Add(float at, Tween t)
        {
            if (t == null || t.m_Killed) return this;
            if (t.m_Parent != null) { Debug.LogWarning("NovaTween: a tween can be nested in one Sequence only"); return this; }
            NovaTween.Unregister(t);
            t.m_Parent = this;
            t.m_Playing = false;
            if (t.m_Loops < 0) t.m_Loops = 1;   // 끝없는 반복은 Sequence 안에서 한 번
            m_Items.Add(new Item { At = at, Tween = t });
            m_Duration = Math.Max(m_Duration, at + t.SequencedDuration);
            return this;
        }

        public Sequence Append(Tween t) { m_LastAppendAt = m_Duration; return Add(m_Duration, t); }
        public Sequence Join(Tween t) => Add(m_LastAppendAt, t);
        public Sequence Insert(float atPosition, Tween t) => Add(Math.Max(0f, atPosition), t);
        public Sequence Prepend(Tween t)
        {
            if (t == null) return this;
            Shift(t.SequencedDuration);
            m_LastAppendAt = 0f;
            return Add(0f, t);
        }
        public Sequence AppendInterval(float interval) { m_LastAppendAt = m_Duration; m_Duration += Math.Max(0f, interval); return this; }
        public Sequence PrependInterval(float interval) { Shift(Math.Max(0f, interval)); return this; }
        public Sequence AppendCallback(TweenCallback callback) => InsertCallback(m_Duration, callback);
        public Sequence PrependCallback(TweenCallback callback) => InsertCallback(0f, callback);
        public Sequence InsertCallback(float atPosition, TweenCallback callback)
        {
            if (callback == null) return this;
            m_Items.Add(new Item { At = Math.Max(0f, atPosition), Callback = callback });
            m_Duration = Math.Max(m_Duration, atPosition);
            return this;
        }

        void Shift(float by)
        {
            for (int i = 0; i < m_Items.Count; i++) { var it = m_Items[i]; it.At += by; m_Items[i] = it; }
            m_Duration += by;
            m_LastAppendAt += by;
        }

        internal override void Startup() { m_Items.Sort((a, b) => a.At.CompareTo(b.At)); }

        internal override void Apply(float eased, int loopIndex)
        {
            float local = eased * m_Duration;
            bool forward = local >= m_LastLocal;
            if (forward)
            {
                for (int i = 0; i < m_Items.Count; i++) Drive(m_Items[i], local, true);
            }
            else
            {
                for (int i = m_Items.Count - 1; i >= 0; i--) Drive(m_Items[i], local, false);
            }
            m_LastLocal = local;
        }

        void Drive(Item it, float local, bool forward)
        {
            if (it.Callback != null)
            {
                bool crossed = forward ? (m_LastLocal < it.At || (m_LastLocal == 0f && it.At == 0f && local > 0f)) && local >= it.At
                                       : m_LastLocal >= it.At && local < it.At;
                if (crossed && forward)
                {
                    try { it.Callback(); } catch (Exception e) { Debug.LogException(e); }
                }
                return;
            }
            Tween t = it.Tween;
            if (t.m_Killed) return;
            float raw = local - it.At - t.m_Delay;
            if (raw < 0f && !t.m_Started) return;   // 아직 시작 전
            if (raw >= 0f) t.EnsureStarted();
            t.GotoInternal(raw, false);
            if (raw > 0f && !t.m_Complete)
                t.onUpdate?.Invoke();
            if (raw >= t.FullDuration && forward && !t.m_PlayedOnce)
            {
                t.m_PlayedOnce = true;
                t.onComplete?.Invoke();
            }
            else if (raw < t.FullDuration) t.m_PlayedOnce = false;
        }

        internal override void OnKilledInternal()
        {
            foreach (var it in m_Items)
                if (it.Tween != null && !it.Tween.m_Killed) { it.Tween.m_Killed = true; it.Tween.onKill?.Invoke(); }
        }
    }
}
