using System;
using NovaEngine.UI;
using NovaEngine.Rendering.Universal;

namespace NovaEngine.Tweening
{
    // 한 줄 트윈 (Unity 의 대표 트윈 도구와 같은 이름): transform.DOMove(p, 1f), image.DOFade(0f, 0.3f), light.DOIntensity(3f, 2f) …
    //  대상 = 그 컴포넌트 (NovaTween.Kill(transform) · transform.DOKill() 로 한꺼번에), 대상 오브젝트가 지워지면 트윈도 조용히 끝난다
    public static class ShortcutExtensions
    {
        static T Tgt<T>(T t, object target) where T : Tween { t.target = target; return t; }

        // ---------------------------------------------------------------- Transform
        public static TweenerCore<Vector3> DOMove(this Transform target, Vector3 endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.position, x => target.position = snapping ? Snap(x) : x, endValue, duration), target);
        public static TweenerCore<float> DOMoveX(this Transform target, float endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.position.x, x => { var p = target.position; p.x = snapping ? MathF.Round(x) : x; target.position = p; }, endValue, duration), target);
        public static TweenerCore<float> DOMoveY(this Transform target, float endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.position.y, x => { var p = target.position; p.y = snapping ? MathF.Round(x) : x; target.position = p; }, endValue, duration), target);
        public static TweenerCore<float> DOMoveZ(this Transform target, float endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.position.z, x => { var p = target.position; p.z = snapping ? MathF.Round(x) : x; target.position = p; }, endValue, duration), target);
        public static TweenerCore<Vector3> DOLocalMove(this Transform target, Vector3 endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.localPosition, x => target.localPosition = snapping ? Snap(x) : x, endValue, duration), target);
        public static TweenerCore<float> DOLocalMoveX(this Transform target, float endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.localPosition.x, x => { var p = target.localPosition; p.x = snapping ? MathF.Round(x) : x; target.localPosition = p; }, endValue, duration), target);
        public static TweenerCore<float> DOLocalMoveY(this Transform target, float endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.localPosition.y, x => { var p = target.localPosition; p.y = snapping ? MathF.Round(x) : x; target.localPosition = p; }, endValue, duration), target);
        public static TweenerCore<float> DOLocalMoveZ(this Transform target, float endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.localPosition.z, x => { var p = target.localPosition; p.z = snapping ? MathF.Round(x) : x; target.localPosition = p; }, endValue, duration), target);

        static Vector3 Snap(Vector3 v) => new Vector3(MathF.Round(v.x), MathF.Round(v.y), MathF.Round(v.z));

        // 회전: Fast = 가장 짧은 길, FastBeyond360 = 오일러 각 그대로 (360 넘게 돈다), WorldAxisAdd · LocalAxisAdd = 지금 회전에 더하기
        public static Tweener DORotate(this Transform target, Vector3 endValue, float duration, RotateMode mode = RotateMode.Fast) => Rotate(target, endValue, duration, mode, false);
        public static Tweener DOLocalRotate(this Transform target, Vector3 endValue, float duration, RotateMode mode = RotateMode.Fast) => Rotate(target, endValue, duration, mode, true);
        public static TweenerCore<Quaternion> DORotateQuaternion(this Transform target, Quaternion endValue, float duration)
            => Tgt(NovaTween.To(() => target.rotation, x => target.rotation = x, endValue, duration), target);
        public static TweenerCore<Quaternion> DOLocalRotateQuaternion(this Transform target, Quaternion endValue, float duration)
            => Tgt(NovaTween.To(() => target.localRotation, x => target.localRotation = x, endValue, duration), target);

        static Tweener Rotate(Transform target, Vector3 endValue, float duration, RotateMode mode, bool local)
        {
            switch (mode)
            {
                case RotateMode.FastBeyond360:
                {
                    var t = local ? NovaTween.To(() => target.localEulerAngles, x => target.localRotation = Quaternion.Euler(x), endValue, duration)
                                  : NovaTween.To(() => target.eulerAngles, x => target.rotation = Quaternion.Euler(x), endValue, duration);
                    return Tgt(t, target);
                }
                case RotateMode.WorldAxisAdd:
                case RotateMode.LocalAxisAdd:
                {
                    // 오일러 더하기를 0 → endValue 로 늘리며, 시작 회전에 곱한다 (월드 축 = 앞에서, 로컬 축 = 뒤에서)
                    Quaternion start = Quaternion.identity;
                    Vector3 applied = Vector3.zero;
                    var t = NovaTween.To(() => applied, x =>
                    {
                        applied = x;
                        Quaternion add = Quaternion.Euler(x);
                        Quaternion r = mode == RotateMode.WorldAxisAdd ? add * start : start * add;
                        if (local) target.localRotation = r; else target.rotation = r;
                    }, endValue, duration);
                    t.m_StartHook = () => { start = local ? target.localRotation : target.rotation; applied = Vector3.zero; };
                    return Tgt(t, target);
                }
                default:
                {
                    var t = local ? NovaTween.To(() => target.localRotation, x => target.localRotation = x, Quaternion.Euler(endValue), duration)
                                  : NovaTween.To(() => target.rotation, x => target.rotation = x, Quaternion.Euler(endValue), duration);
                    return Tgt(t, target);
                }
            }
        }

        public static TweenerCore<Quaternion> DOLookAt(this Transform target, Vector3 towards, float duration, Vector3? up = null)
        {
            Vector3 u = up ?? Vector3.up;
            var t = NovaTween.To(() => target.rotation, x => target.rotation = x, Quaternion.identity, duration);
            t.m_StartHook = () =>
            {
                Vector3 dir = towards - target.position;
                if (dir.sqrMagnitude > 1e-8f) t.endValue = Quaternion.LookRotation(dir, u);
            };
            return Tgt(t, target);
        }

        public static TweenerCore<Vector3> DOScale(this Transform target, Vector3 endValue, float duration)
            => Tgt(NovaTween.To(() => target.localScale, x => target.localScale = x, endValue, duration), target);
        public static TweenerCore<Vector3> DOScale(this Transform target, float endValue, float duration) => DOScale(target, new Vector3(endValue, endValue, endValue), duration);
        public static TweenerCore<float> DOScaleX(this Transform target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.localScale.x, x => { var s = target.localScale; s.x = x; target.localScale = s; }, endValue, duration), target);
        public static TweenerCore<float> DOScaleY(this Transform target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.localScale.y, x => { var s = target.localScale; s.y = x; target.localScale = s; }, endValue, duration), target);
        public static TweenerCore<float> DOScaleZ(this Transform target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.localScale.z, x => { var s = target.localScale; s.z = x; target.localScale = s; }, endValue, duration), target);

        // ---- 튀기기 (Punch): punch 방향으로 튀었다가 vibrato 번 흔들리며 제자리로 (elasticity 0 = 앞으로만, 1 = 앞뒤 같은 세기)
        public static Tweener DOPunchPosition(this Transform target, Vector3 punch, float duration, int vibrato = 10, float elasticity = 1f, bool snapping = false)
            => Tgt(NovaTween.Offset(() => target.localPosition, x => target.localPosition = snapping ? Snap(x) : x, duration, k => punch * PunchCurve(k, vibrato, elasticity)), target);
        public static Tweener DOPunchScale(this Transform target, Vector3 punch, float duration, int vibrato = 10, float elasticity = 1f)
            => Tgt(NovaTween.Offset(() => target.localScale, x => target.localScale = x, duration, k => punch * PunchCurve(k, vibrato, elasticity)), target);
        public static Tweener DOPunchRotation(this Transform target, Vector3 punch, float duration, int vibrato = 10, float elasticity = 1f)
            => Tgt(NovaTween.Offset(() => target.localEulerAngles, x => target.localRotation = Quaternion.Euler(x), duration, k => punch * PunchCurve(k, vibrato, elasticity)), target);

        // ---- 흔들기 (Shake): 세기 strength 로 vibrato 번 무작위 방향 (randomness = 방향이 바뀌는 각도), fadeOut = 끝으로 갈수록 약하게
        public static Tweener DOShakePosition(this Transform target, float duration, float strength = 1f, int vibrato = 10, float randomness = 90f, bool snapping = false, bool fadeOut = true)
            => DOShakePosition(target, duration, new Vector3(strength, strength, strength), vibrato, randomness, snapping, fadeOut);
        public static Tweener DOShakePosition(this Transform target, float duration, Vector3 strength, int vibrato = 10, float randomness = 90f, bool snapping = false, bool fadeOut = true)
        {
            var shake = ShakeCurve(strength, vibrato, randomness, fadeOut);
            return Tgt(NovaTween.Offset(() => target.localPosition, x => target.localPosition = snapping ? Snap(x) : x, duration, shake), target);
        }
        public static Tweener DOShakeRotation(this Transform target, float duration, float strength = 90f, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
            => DOShakeRotation(target, duration, new Vector3(strength, strength, strength), vibrato, randomness, fadeOut);
        public static Tweener DOShakeRotation(this Transform target, float duration, Vector3 strength, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
        {
            var shake = ShakeCurve(strength, vibrato, randomness, fadeOut);
            return Tgt(NovaTween.Offset(() => target.localEulerAngles, x => target.localRotation = Quaternion.Euler(x), duration, shake), target);
        }
        public static Tweener DOShakeScale(this Transform target, float duration, float strength = 1f, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
            => DOShakeScale(target, duration, new Vector3(strength, strength, strength), vibrato, randomness, fadeOut);
        public static Tweener DOShakeScale(this Transform target, float duration, Vector3 strength, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
        {
            var shake = ShakeCurve(strength, vibrato, randomness, fadeOut);
            return Tgt(NovaTween.Offset(() => target.localScale, x => target.localScale = x, duration, shake), target);
        }

        // ---- 뛰기 (Jump): 끝 위치로 가며 jumpPower 높이로 numJumps 번 (포물선)
        public static Tweener DOJump(this Transform target, Vector3 endValue, float jumpPower, int numJumps, float duration, bool snapping = false) => Jump(target, endValue, jumpPower, numJumps, duration, snapping, false);
        public static Tweener DOLocalJump(this Transform target, Vector3 endValue, float jumpPower, int numJumps, float duration, bool snapping = false) => Jump(target, endValue, jumpPower, numJumps, duration, snapping, true);

        static Tweener Jump(Transform target, Vector3 endValue, float jumpPower, int numJumps, float duration, bool snapping, bool local)
        {
            int jumps = Math.Max(1, numJumps);
            var t = NovaTween.To(() => local ? target.localPosition : target.position, x =>
            {
                if (snapping) x = Snap(x);
                if (local) target.localPosition = x; else target.position = x;
            }, endValue, duration);
            t.m_Custom = (s, e, k) =>
            {
                Vector3 p = new Vector3(s.x + (e.x - s.x) * k, s.y + (e.y - s.y) * k, s.z + (e.z - s.z) * k);
                float f = k * jumps - MathF.Floor(k * jumps);
                if (k >= 1f) f = 0f;
                p.y += 4f * jumpPower * f * (1f - f);
                return p;
            };
            t.m_Ease = Ease.Linear;
            return Tgt(t, target);
        }

        // ---- 경로: 점들을 지나 (Linear = 곧게, CatmullRom = 부드럽게). closePath = 처음 점으로 돌아온다
        public static Tweener DOPath(this Transform target, Vector3[] path, float duration, PathType pathType = PathType.Linear, bool closePath = false)
            => Path(target, path, duration, pathType, closePath, false);
        public static Tweener DOLocalPath(this Transform target, Vector3[] path, float duration, PathType pathType = PathType.Linear, bool closePath = false)
            => Path(target, path, duration, pathType, closePath, true);

        static Tweener Path(Transform target, Vector3[] path, float duration, PathType pathType, bool closePath, bool local)
        {
            Vector3[] pts = null;
            float[] lengths = null;
            float total = 0f;
            var t = NovaTween.To(() => 0f, k =>
            {
                if (pts == null) return;
                Vector3 p = PathPoint(pts, lengths, total, k, pathType);
                if (local) target.localPosition = p; else target.position = p;
            }, 1f, duration);
            t.m_StartHook = () =>
            {
                int n = (path?.Length ?? 0) + 1 + (closePath ? 1 : 0);
                pts = new Vector3[n];
                pts[0] = local ? target.localPosition : target.position;
                for (int i = 0; i < (path?.Length ?? 0); i++) pts[i + 1] = path[i];
                if (closePath) pts[n - 1] = pts[0];
                lengths = new float[n];
                for (int i = 1; i < n; i++) { total += (pts[i] - pts[i - 1]).magnitude; lengths[i] = total; }
            };
            return Tgt(t, target);
        }

        static Vector3 PathPoint(Vector3[] pts, float[] lengths, float total, float k, PathType type)
        {
            if (pts.Length == 1 || total <= 0f) return pts[pts.Length - 1];
            float d = Math.Clamp(k, 0f, 1f) * total;
            int seg = 1;
            while (seg < pts.Length - 1 && lengths[seg] < d) seg++;
            float segLen = lengths[seg] - lengths[seg - 1];
            float u = segLen > 0f ? (d - lengths[seg - 1]) / segLen : 1f;
            Vector3 a = pts[seg - 1], b = pts[seg];
            if (type == PathType.Linear) return a + (b - a) * u;
            Vector3 p0 = seg >= 2 ? pts[seg - 2] : a, p3 = seg + 1 < pts.Length ? pts[seg + 1] : b;
            float u2 = u * u, u3 = u2 * u;
            return 0.5f * ((2f * a) + (-p0 + b) * u + (2f * p0 - 5f * a + 4f * b - p3) * u2 + (-p0 + 3f * a - 3f * b + p3) * u3);
        }

        // ---------------------------------------------------------------- 색 · 투명도 (Material · SpriteRenderer · Image · Text · Light)
        public static TweenerCore<Color> DOColor(this Material target, Color endValue, float duration)
            => Tgt(NovaTween.To(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<Color> DOColor(this Material target, Color endValue, string property, float duration)
            => Tgt(NovaTween.To(() => target.GetColor(property), x => target.SetColor(property, x), endValue, duration), target);
        public static TweenerCore<Color> DOFade(this Material target, float endValue, float duration)
            => Tgt(NovaTween.ToAlpha(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<float> DOFloat(this Material target, float endValue, string property, float duration)
            => Tgt(NovaTween.To(() => target.GetFloat(property), x => target.SetFloat(property, x), endValue, duration), target);

        public static TweenerCore<Color> DOColor(this SpriteRenderer target, Color endValue, float duration)
            => Tgt(NovaTween.To(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<Color> DOFade(this SpriteRenderer target, float endValue, float duration)
            => Tgt(NovaTween.ToAlpha(() => target.color, x => target.color = x, endValue, duration), target);

        public static TweenerCore<Color> DOColor(this Graphic target, Color endValue, float duration)
            => Tgt(NovaTween.To(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<Color> DOFade(this Graphic target, float endValue, float duration)
            => Tgt(NovaTween.ToAlpha(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<float> DOFillAmount(this Image target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.fillAmount, x => target.fillAmount = x, Math.Clamp(endValue, 0f, 1f), duration), target);
        // 타자기: 앞에서부터 한 글자씩
        public static TweenerCore<string> DOText(this Text target, string endValue, float duration, bool richTextEnabled = true, ScrambleMode scrambleMode = ScrambleMode.None, string scrambleChars = null)
            => Tgt(NovaTween.To(() => target.text, x => target.text = x, endValue, duration), target);
        // 숫자 세기 (점수 등)
        public static TweenerCore<int> DOCounter(this Text target, int fromValue, int endValue, float duration, string format = null)
        {
            int v = fromValue;
            return Tgt(NovaTween.To(() => v, x => { v = x; target.text = format != null ? x.ToString(format) : x.ToString(); }, endValue, duration), target);
        }

        public static TweenerCore<Color> DOColor(this Light target, Color endValue, float duration)
            => Tgt(NovaTween.To(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<float> DOIntensity(this Light target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.intensity, x => target.intensity = x, endValue, duration), target);
        public static TweenerCore<float> DOShadowStrength(this Light target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.shadowStrength, x => target.shadowStrength = x, endValue, duration), target);

        // 2D 빛 (Light 2D)
        public static TweenerCore<Color> DOColor(this Light2D target, Color endValue, float duration)
            => Tgt(NovaTween.To(() => target.color, x => target.color = x, endValue, duration), target);
        public static TweenerCore<float> DOIntensity(this Light2D target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.intensity, x => target.intensity = x, endValue, duration), target);
        public static TweenerCore<float> DOShadowIntensity(this Light2D target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.shadowIntensity, x => target.shadowIntensity = x, endValue, duration), target);
        public static TweenerCore<float> DORadius(this Light2D target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.pointLightOuterRadius, x => target.pointLightOuterRadius = x, endValue, duration), target);

        // ---------------------------------------------------------------- UI (RectTransform)
        public static TweenerCore<Vector2> DOAnchorPos(this RectTransform target, Vector2 endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.anchoredPosition, x => target.anchoredPosition = snapping ? new Vector2(MathF.Round(x.x), MathF.Round(x.y)) : x, endValue, duration), target);
        public static TweenerCore<float> DOAnchorPosX(this RectTransform target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.anchoredPosition.x, x => { var p = target.anchoredPosition; p.x = x; target.anchoredPosition = p; }, endValue, duration), target);
        public static TweenerCore<float> DOAnchorPosY(this RectTransform target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.anchoredPosition.y, x => { var p = target.anchoredPosition; p.y = x; target.anchoredPosition = p; }, endValue, duration), target);
        public static TweenerCore<Vector2> DOSizeDelta(this RectTransform target, Vector2 endValue, float duration, bool snapping = false)
            => Tgt(NovaTween.To(() => target.sizeDelta, x => target.sizeDelta = snapping ? new Vector2(MathF.Round(x.x), MathF.Round(x.y)) : x, endValue, duration), target);
        public static Tweener DOPunchAnchorPos(this RectTransform target, Vector2 punch, float duration, int vibrato = 10, float elasticity = 1f)
        {
            var t = NovaTween.To(() => target.anchoredPosition, x => target.anchoredPosition = x, Vector2.zero, duration);
            t.m_IsRelative = true;
            t.m_Ease = Ease.Linear;
            t.m_Custom = (s, e, k) => s + punch * PunchCurve(k, vibrato, elasticity);
            return Tgt(t, target);
        }
        public static Tweener DOShakeAnchorPos(this RectTransform target, float duration, float strength = 100f, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
        {
            var shake = ShakeCurve(new Vector3(strength, strength, 0f), vibrato, randomness, fadeOut);
            var t = NovaTween.To(() => target.anchoredPosition, x => target.anchoredPosition = x, Vector2.zero, duration);
            t.m_IsRelative = true;
            t.m_Ease = Ease.Linear;
            t.m_Custom = (s, e, k) => { Vector3 o = shake(k); return new Vector2(s.x + o.x, s.y + o.y); };
            return Tgt(t, target);
        }

        // ---------------------------------------------------------------- 카메라 · 소리
        public static TweenerCore<float> DOFieldOfView(this Camera target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.fieldOfView, x => target.fieldOfView = x, endValue, duration), target);
        public static TweenerCore<float> DOOrthoSize(this Camera target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.orthographicSize, x => target.orthographicSize = x, endValue, duration), target);
        public static Tweener DOShakePosition(this Camera target, float duration, float strength = 3f, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
            => DOShakePosition(target.transform, duration, strength, vibrato, randomness, false, fadeOut).SetTarget(target);
        public static Tweener DOShakeRotation(this Camera target, float duration, float strength = 90f, int vibrato = 10, float randomness = 90f, bool fadeOut = true)
            => DOShakeRotation(target.transform, duration, strength, vibrato, randomness, fadeOut).SetTarget(target);

        public static TweenerCore<float> DOFade(this AudioSource target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.volume, x => target.volume = x, Math.Clamp(endValue, 0f, 1f), duration), target);
        public static TweenerCore<float> DOPitch(this AudioSource target, float endValue, float duration)
            => Tgt(NovaTween.To(() => target.pitch, x => target.pitch = x, endValue, duration), target);

        // ---------------------------------------------------------------- 대상의 트윈을 한꺼번에
        public static int DOKill(this Component target, bool complete = false) => NovaTween.Kill(target, complete) + (target is Transform ? 0 : NovaTween.Kill(target.transform, complete));
        public static int DOKill(this Material target, bool complete = false) => NovaTween.Kill(target, complete);
        public static int DOComplete(this Component target, bool withCallbacks = false) => NovaTween.Complete(target, withCallbacks);
        public static int DOPause(this Component target) => NovaTween.Pause(target);
        public static int DOPlay(this Component target) => NovaTween.Play(target);
        public static int DORestart(this Component target, bool includeDelay = true) => NovaTween.Restart(target, includeDelay);
        public static int DORewind(this Component target, bool includeDelay = true) => NovaTween.Rewind(target, includeDelay);
        public static int DOTogglePause(this Component target) => NovaTween.TogglePause(target);

        // ---------------------------------------------------------------- 곡선
        // 튀기기: 처음 1/vibrato 동안 튀고, 그 뒤 vibrato 번 흔들리며 줄어든다
        static float PunchCurve(float k, int vibrato, float elasticity)
        {
            if (k <= 0f || k >= 1f) return 0f;
            int n = Math.Max(1, vibrato);
            float decay = 1f - k;
            float wave = MathF.Sin(k * n * MathF.PI);
            if (wave < 0f) wave *= Math.Clamp(elasticity, 0f, 1f);
            return wave * decay;
        }

        // 흔들기: vibrato 개의 무작위 점 사이를 잇는다 (다음 점은 앞 방향에서 randomness 도 안쪽으로 꺾인다)
        static Func<float, Vector3> ShakeCurve(Vector3 strength, int vibrato, float randomness, bool fadeOut)
        {
            int n = Math.Max(2, vibrato);
            var pts = new Vector3[n + 1];
            float angle = Random.Range(0f, 360f);
            for (int i = 0; i < n; i++)
            {
                float fade = fadeOut ? 1f - (float)i / n : 1f;
                angle += 180f + Random.Range(-randomness, randomness);
                float rad = angle * MathF.PI / 180f;
                float pitch = Random.Range(-1f, 1f);
                pts[i] = new Vector3(MathF.Cos(rad) * strength.x, MathF.Sin(rad) * strength.y, pitch * strength.z) * fade;
            }
            pts[n] = Vector3.zero;
            return k =>
            {
                if (k <= 0f || k >= 1f) return Vector3.zero;
                float f = k * n;
                int i = (int)f;
                float u = f - i;
                Vector3 a = i == 0 ? Vector3.zero : pts[i - 1], b = pts[i];
                return a + (b - a) * u;
            };
        }
    }

    public enum PathType { Linear, CatmullRom }
}
