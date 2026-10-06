using System;
using System.Threading.Tasks;

namespace NovaEngine.Tweening
{
    // 설정 · 콜백 · 기다리기 (체인: transform.DOMove(p, 1f).SetEase(Ease.OutBack).SetLoops(2, LoopType.Yoyo).OnComplete(Done))
    public static class TweenSettingsExtensions
    {
        public static T SetEase<T>(this T t, Ease ease) where T : Tween { t.m_Ease = ease; t.m_CustomEase = null; return t; }
        // Back 의 넘침 · Elastic 의 진폭
        public static T SetEase<T>(this T t, Ease ease, float overshoot) where T : Tween { t.m_Ease = ease; t.m_Overshoot = overshoot; t.m_CustomEase = null; return t; }
        public static T SetEase<T>(this T t, Ease ease, float amplitude, float period) where T : Tween { t.m_Ease = ease; t.m_Overshoot = amplitude; t.m_Period = period; t.m_CustomEase = null; return t; }
        public static T SetEase<T>(this T t, EaseFunction customEase) where T : Tween { t.m_CustomEase = customEase; return t; }
        // 0 ~ 1 → 0 ~ 1 함수로 곡선
        public static T SetEase<T>(this T t, Func<float, float> curve) where T : Tween
        {
            if (curve != null) t.m_CustomEase = (time, duration, a, p) => curve(duration > 0f ? time / duration : 1f);
            return t;
        }
        // -1 = 끝없이
        public static T SetLoops<T>(this T t, int loops) where T : Tween { t.m_Loops = loops == 0 ? 1 : loops; return t; }
        public static T SetLoops<T>(this T t, int loops, LoopType loopType) where T : Tween { t.m_Loops = loops == 0 ? 1 : loops; t.m_LoopType = loopType; return t; }
        public static T SetDelay<T>(this T t, float delay) where T : Tween { t.m_Delay = Math.Max(0f, delay); return t; }
        // 끝 값 = 지금 값 + 준 값
        public static T SetRelative<T>(this T t, bool isRelative = true) where T : Tween { t.m_IsRelative = isRelative; return t; }
        public static T SetAutoKill<T>(this T t, bool autoKillOnCompletion = true) where T : Tween { t.m_AutoKill = autoKillOnCompletion; return t; }
        public static T SetId<T>(this T t, object id) where T : Tween { t.id = id; return t; }
        public static T SetId<T>(this T t, string stringId) where T : Tween { t.stringId = stringId; t.id = stringId; return t; }
        public static T SetId<T>(this T t, int intId) where T : Tween { t.intId = intId; t.id = intId; return t; }
        public static T SetTarget<T>(this T t, object target) where T : Tween { t.target = target; return t; }
        // isIndependentUpdate = Time.timeScale 를 무시 (멈춤 화면의 UI 등)
        public static T SetUpdate<T>(this T t, bool isIndependentUpdate) where T : Tween { t.m_IgnoreTimeScale = isIndependentUpdate; return t; }
        public static T SetUpdate<T>(this T t, UpdateType updateType, bool isIndependentUpdate = false) where T : Tween { t.m_UpdateType = updateType; t.m_IgnoreTimeScale = isIndependentUpdate; return t; }
        // 길이 대신 속도 (초당 단위) — 만들 때 준 duration 이 속도가 된다
        public static T SetSpeedBased<T>(this T t, bool isSpeedBased = true) where T : Tween { t.m_SpeedBased = isSpeedBased; return t; }
        public static T SetRecyclable<T>(this T t, bool recyclable = true) where T : Tween => t;
        public static T SetInverted<T>(this T t, bool inverted = true) where T : Tween { if (inverted) { t.isBackwards = true; } return t; }

        // From: 준 값에서 지금 값으로 (바로 그 값으로 옮긴 뒤)
        public static TweenerCore<T> From<T>(this TweenerCore<T> t, bool isRelative = false) { t.m_IsFrom = true; t.m_IsRelative = isRelative; t.EnsureStarted(); return t; }
        public static TweenerCore<T> From<T>(this TweenerCore<T> t, T fromValue, bool setImmediately = true, bool isRelative = false)
        {
            t.m_IsFrom = true;
            t.m_FromValue = fromValue;
            t.m_HasFromValue = true;
            t.m_IsRelative = isRelative;
            if (setImmediately) t.EnsureStarted();
            return t;
        }

        // ---- 콜백
        public static T OnStart<T>(this T t, TweenCallback action) where T : Tween { t.onStart = action; return t; }
        public static T OnPlay<T>(this T t, TweenCallback action) where T : Tween { t.onPlay = action; return t; }
        public static T OnPause<T>(this T t, TweenCallback action) where T : Tween { t.onPause = action; return t; }
        public static T OnRewind<T>(this T t, TweenCallback action) where T : Tween { t.onRewind = action; return t; }
        public static T OnUpdate<T>(this T t, TweenCallback action) where T : Tween { t.onUpdate = action; return t; }
        public static T OnStepComplete<T>(this T t, TweenCallback action) where T : Tween { t.onStepComplete = action; return t; }
        public static T OnComplete<T>(this T t, TweenCallback action) where T : Tween { t.onComplete = action; return t; }
        public static T OnKill<T>(this T t, TweenCallback action) where T : Tween { t.onKill = action; return t; }

        // ---- 기다리기: 코루틴 (yield return t.WaitForCompletion()) · async (await t.AsyncWaitForCompletion())
        public static YieldInstruction WaitForCompletion(this Tween t) => new WaitUntil(() => t == null || !t.IsActive() || t.IsComplete());
        public static YieldInstruction WaitForKill(this Tween t) => new WaitUntil(() => t == null || !t.IsActive());
        public static YieldInstruction WaitForRewind(this Tween t) => new WaitUntil(() => t == null || !t.IsActive() || (!t.IsPlaying() && t.Elapsed() <= 0f));
        public static YieldInstruction WaitForElapsedLoops(this Tween t, int elapsedLoops) => new WaitUntil(() => t == null || !t.IsActive() || t.CompletedLoops() >= elapsedLoops);
        public static YieldInstruction WaitForPosition(this Tween t, float position) => new WaitUntil(() => t == null || !t.IsActive() || t.Elapsed() >= position);
        public static YieldInstruction WaitForStart(this Tween t) => new WaitUntil(() => t == null || !t.IsActive() || t.IsInitialized());

        public static Task AsyncWaitForCompletion(this Tween t)
        {
            var tcs = new TaskCompletionSource<bool>();
            if (t == null || !t.IsActive() || t.IsComplete()) { tcs.SetResult(true); return tcs.Task; }
            TweenCallback prevComplete = t.onComplete, prevKill = t.onKill;
            t.onComplete = () => { prevComplete?.Invoke(); tcs.TrySetResult(true); };
            t.onKill = () => { prevKill?.Invoke(); tcs.TrySetResult(true); };
            return tcs.Task;
        }
        public static Task AsyncWaitForKill(this Tween t)
        {
            var tcs = new TaskCompletionSource<bool>();
            if (t == null || !t.IsActive()) { tcs.SetResult(true); return tcs.Task; }
            TweenCallback prevKill = t.onKill;
            t.onKill = () => { prevKill?.Invoke(); tcs.TrySetResult(true); };
            return tcs.Task;
        }
    }
}
