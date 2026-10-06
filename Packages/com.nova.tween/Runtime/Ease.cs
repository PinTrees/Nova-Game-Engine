using System;

namespace NovaEngine.Tweening
{
    // 곡선 (Robert Penner 식). 기본 = OutQuad (Unity 의 대표 트윈 도구와 같음)
    public enum Ease
    {
        Unset = 0, Linear,
        InSine, OutSine, InOutSine,
        InQuad, OutQuad, InOutQuad,
        InCubic, OutCubic, InOutCubic,
        InQuart, OutQuart, InOutQuart,
        InQuint, OutQuint, InOutQuint,
        InExpo, OutExpo, InOutExpo,
        InCirc, OutCirc, InOutCirc,
        InElastic, OutElastic, InOutElastic,
        InBack, OutBack, InOutBack,
        InBounce, OutBounce, InOutBounce,
    }

    // 사용자 곡선: (경과 시간, 길이, overshootOrAmplitude, period) → 0 ~ 1 (넘어도 된다)
    public delegate float EaseFunction(float time, float duration, float overshootOrAmplitude, float period);

    public static class EaseManager
    {
        const float PiOver2 = MathF.PI * 0.5f;
        const float TwoPi = MathF.PI * 2f;

        // t = 0 ~ 1 → 곡선 값. overshoot = Back 의 넘침 (기본 1.70158) · Elastic 의 진폭, period = Elastic 의 주기 (0 = 기본)
        public static float Evaluate(Ease ease, float t, float overshootOrAmplitude = 1.70158f, float period = 0f)
        {
            if (t <= 0f) return 0f;   // 모든 곡선이 0 에서 시작해 1 에서 끝난다
            if (t >= 1f) return 1f;
            return Shape(ease, t, overshootOrAmplitude, period);
        }

        static float Shape(Ease ease, float t, float s, float p)
        {
            switch (ease)
            {
                case Ease.Linear: return t;
                case Ease.InSine: return 1f - MathF.Cos(t * PiOver2);
                case Ease.OutSine: return MathF.Sin(t * PiOver2);
                case Ease.InOutSine: return -0.5f * (MathF.Cos(MathF.PI * t) - 1f);
                case Ease.InQuad: return t * t;
                case Ease.Unset:
                case Ease.OutQuad: return -t * (t - 2f);
                case Ease.InOutQuad: return t < 0.5f ? 2f * t * t : -1f + (4f - 2f * t) * t;
                case Ease.InCubic: return t * t * t;
                case Ease.OutCubic: { float u = t - 1f; return u * u * u + 1f; }
                case Ease.InOutCubic: return t < 0.5f ? 4f * t * t * t : (t - 1f) * (2f * t - 2f) * (2f * t - 2f) + 1f;
                case Ease.InQuart: return t * t * t * t;
                case Ease.OutQuart: { float u = t - 1f; return 1f - u * u * u * u; }
                case Ease.InOutQuart: { if (t < 0.5f) return 8f * t * t * t * t; float u = t - 1f; return 1f - 8f * u * u * u * u; }
                case Ease.InQuint: return t * t * t * t * t;
                case Ease.OutQuint: { float u = t - 1f; return 1f + u * u * u * u * u; }
                case Ease.InOutQuint: { if (t < 0.5f) return 16f * t * t * t * t * t; float u = t - 1f; return 1f + 16f * u * u * u * u * u; }
                case Ease.InExpo: return t == 0f ? 0f : MathF.Pow(2f, 10f * (t - 1f));
                case Ease.OutExpo: return t == 1f ? 1f : 1f - MathF.Pow(2f, -10f * t);
                case Ease.InOutExpo:
                    if (t == 0f || t == 1f) return t;
                    return t < 0.5f ? 0.5f * MathF.Pow(2f, 20f * t - 10f) : 1f - 0.5f * MathF.Pow(2f, -20f * t + 10f);
                case Ease.InCirc: return 1f - MathF.Sqrt(MathF.Max(0f, 1f - t * t));
                case Ease.OutCirc: { float u = t - 1f; return MathF.Sqrt(MathF.Max(0f, 1f - u * u)); }
                case Ease.InOutCirc:
                    return t < 0.5f ? 0.5f * (1f - MathF.Sqrt(MathF.Max(0f, 1f - 4f * t * t)))
                                    : 0.5f * (MathF.Sqrt(MathF.Max(0f, 1f - (2f * t - 2f) * (2f * t - 2f))) + 1f);
                case Ease.InElastic: return 1f - Elastic(1f - t, s, p);
                case Ease.OutElastic: return Elastic(t, s, p);
                case Ease.InOutElastic: return t < 0.5f ? 0.5f * (1f - Elastic(1f - 2f * t, s, p)) : 0.5f * Elastic(2f * t - 1f, s, p) + 0.5f;
                case Ease.InBack: return t * t * ((s + 1f) * t - s);
                case Ease.OutBack: { float u = t - 1f; return u * u * ((s + 1f) * u + s) + 1f; }
                case Ease.InOutBack:
                {
                    float k = s * 1.525f;
                    if (t < 0.5f) { float a = 2f * t; return 0.5f * (a * a * ((k + 1f) * a - k)); }
                    float b = 2f * t - 2f;
                    return 0.5f * (b * b * ((k + 1f) * b + k) + 2f);
                }
                case Ease.InBounce: return 1f - Bounce(1f - t);
                case Ease.OutBounce: return Bounce(t);
                case Ease.InOutBounce: return t < 0.5f ? 0.5f * (1f - Bounce(1f - 2f * t)) : 0.5f * Bounce(2f * t - 1f) + 0.5f;
            }
            return t;
        }

        // OutElastic: 진폭 a (1 이상 — 넘치는 정도), 주기 p (기본 0.3)
        static float Elastic(float t, float amplitude, float period)
        {
            if (t <= 0f) return 0f;
            if (t >= 1f) return 1f;
            float p = period > 0f ? period : 0.3f;
            float a = amplitude < 1f ? 1f : amplitude;
            float s = p / TwoPi * MathF.Asin(1f / a);
            return a * MathF.Pow(2f, -10f * t) * MathF.Sin((t - s) * TwoPi / p) + 1f;
        }

        static float Bounce(float t)
        {
            if (t < 1f / 2.75f) return 7.5625f * t * t;
            if (t < 2f / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
            if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
            t -= 2.625f / 2.75f;
            return 7.5625f * t * t + 0.984375f;
        }
    }
}
