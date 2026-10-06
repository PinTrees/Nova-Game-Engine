using NovaEngine.UI;

namespace NovaEngine.Tweening
{
    public enum TweenAnimationType
    {
        Move, LocalMove, Rotate, LocalRotate, Scale,
        Color, Fade, FillAmount,
        PunchPosition, PunchRotation, PunchScale,
        ShakePosition, ShakeRotation, ShakeScale,
        Jump, AnchorPos, UIWidthHeight, CameraFieldOfView, LightIntensity,
    }

    // Inspector 에서 고르는 트윈 (코드 없이): 종류 · 끝 값 · 시간 · 곡선 · 반복 … — Play 하면 Start 에서 만들어 (autoPlay 면) 재생한다.
    //  대상은 이 오브젝트 (Target 에 다른 Transform 을 넣으면 그것). 색 · 투명도는 SpriteRenderer → Image → Text → Light → MeshRenderer 재질 차례로 찾는다
    public class TweenAnimation : MonoBehaviour
    {
        public TweenAnimationType animationType = TweenAnimationType.Move;
        public Transform target;
        public Vector3 endValue = Vector3.zero;
        public float endValueFloat = 1f;
        public Color endValueColor = new Color(1f, 1f, 1f, 1f);
        public float duration = 1f;
        public float delay;
        public Ease easeType = Ease.OutQuad;
        public int loops = 1;
        public LoopType loopType = LoopType.Restart;
        public bool isRelative;
        public bool isFrom;
        public bool isIndependentUpdate;
        public bool autoPlay = true;
        public bool autoKill = true;
        public string id = "";
        // 튀기기 · 흔들기 · 뛰기
        public int vibrato = 10;
        public float elasticity = 1f;
        public float randomness = 90f;
        public float jumpPower = 1f;
        public int numJumps = 1;

        public Tween tween { get; private set; }

        void Start()
        {
            CreateTween();
            if (tween != null && !autoPlay) tween.Pause();
        }

        void OnDestroy()
        {
            if (tween != null && tween.IsActive()) tween.Kill();
        }

        public void CreateTween()
        {
            if (tween != null && tween.IsActive()) tween.Kill();
            Transform tr = target != null ? target : transform;
            Tween t = Build(tr);
            if (t == null)
            {
                Debug.LogWarning($"TweenAnimation on {name}: no target component for {animationType}");
                return;
            }
            t.SetDelay(delay).SetLoops(loops, loopType).SetAutoKill(autoKill).SetUpdate(isIndependentUpdate);
            bool shapeTween = animationType >= TweenAnimationType.PunchPosition && animationType <= TweenAnimationType.Jump;
            if (!shapeTween) t.SetEase(easeType);
            if (!string.IsNullOrEmpty(id)) t.SetId(id);
            if (isRelative && !shapeTween) t.SetRelative();
            if (isFrom && !shapeTween) MakeFrom(t);
            tween = t;
        }

        Tween Build(Transform tr)
        {
            switch (animationType)
            {
                case TweenAnimationType.Move: return tr.DOMove(endValue, duration);
                case TweenAnimationType.LocalMove: return tr.DOLocalMove(endValue, duration);
                case TweenAnimationType.Rotate: return tr.DORotate(endValue, duration);
                case TweenAnimationType.LocalRotate: return tr.DOLocalRotate(endValue, duration);
                case TweenAnimationType.Scale: return tr.DOScale(endValue, duration);
                case TweenAnimationType.PunchPosition: return tr.DOPunchPosition(endValue, duration, vibrato, elasticity);
                case TweenAnimationType.PunchRotation: return tr.DOPunchRotation(endValue, duration, vibrato, elasticity);
                case TweenAnimationType.PunchScale: return tr.DOPunchScale(endValue, duration, vibrato, elasticity);
                case TweenAnimationType.ShakePosition: return tr.DOShakePosition(duration, endValue, vibrato, randomness);
                case TweenAnimationType.ShakeRotation: return tr.DOShakeRotation(duration, endValue, vibrato, randomness);
                case TweenAnimationType.ShakeScale: return tr.DOShakeScale(duration, endValue, vibrato, randomness);
                case TweenAnimationType.Jump: return tr.DOJump(endValue, jumpPower, numJumps, duration);
                case TweenAnimationType.AnchorPos:
                {
                    var rt = tr.gameObject.GetComponent<RectTransform>();
                    return rt != null ? rt.DOAnchorPos(new Vector2(endValue.x, endValue.y), duration) : null;
                }
                case TweenAnimationType.UIWidthHeight:
                {
                    var rt = tr.gameObject.GetComponent<RectTransform>();
                    return rt != null ? rt.DOSizeDelta(new Vector2(endValue.x, endValue.y), duration) : null;
                }
                case TweenAnimationType.CameraFieldOfView:
                {
                    var cam = tr.gameObject.GetComponent<Camera>();
                    return cam != null ? cam.DOFieldOfView(endValueFloat, duration) : null;
                }
                case TweenAnimationType.LightIntensity:
                {
                    var light = tr.gameObject.GetComponent<Light>();
                    return light != null ? light.DOIntensity(endValueFloat, duration) : null;
                }
                case TweenAnimationType.FillAmount:
                {
                    var img = tr.gameObject.GetComponent<Image>();
                    return img != null ? img.DOFillAmount(endValueFloat, duration) : null;
                }
                case TweenAnimationType.Color:
                case TweenAnimationType.Fade:
                    return ColorTween(tr.gameObject, animationType == TweenAnimationType.Fade);
            }
            return null;
        }

        Tween ColorTween(GameObject go, bool fade)
        {
            var sr = go.GetComponent<SpriteRenderer>();
            if (sr != null) return fade ? (Tween)sr.DOFade(endValueFloat, duration) : sr.DOColor(endValueColor, duration);
            var img = go.GetComponent<Image>();
            if (img != null) return fade ? (Tween)img.DOFade(endValueFloat, duration) : img.DOColor(endValueColor, duration);
            var txt = go.GetComponent<Text>();
            if (txt != null) return fade ? (Tween)txt.DOFade(endValueFloat, duration) : txt.DOColor(endValueColor, duration);
            var light = go.GetComponent<Light>();
            if (light != null && !fade) return light.DOColor(endValueColor, duration);
            var mr = go.GetComponent<MeshRenderer>();
            if (mr != null) return fade ? (Tween)mr.material.DOFade(endValueFloat, duration) : mr.material.DOColor(endValueColor, duration);
            return null;
        }

        static void MakeFrom(Tween t)
        {
            switch (t)
            {
                case TweenerCore<Vector3> v: v.From(v.m_IsRelative); break;
                case TweenerCore<Vector2> v: v.From(v.m_IsRelative); break;
                case TweenerCore<float> v: v.From(v.m_IsRelative); break;
                case TweenerCore<Color> v: v.From(v.m_IsRelative); break;
                case TweenerCore<Quaternion> v: v.From(v.m_IsRelative); break;
            }
        }

        // ---- 제어 (UI Button 의 OnClick 등에서)
        public void DOPlay() { if (tween == null || !tween.IsActive()) CreateTween(); tween?.Play(); }
        public void DOPause() => tween?.Pause();
        public void DOTogglePause() => tween?.TogglePause();
        public void DORestart() { if (tween == null || !tween.IsActive()) CreateTween(); tween?.Restart(); }
        public void DORewind() => tween?.Rewind();
        public void DOComplete() => tween?.Complete();
        public void DOKill() { tween?.Kill(); tween = null; }
        public void DOPlayForward() { if (tween == null || !tween.IsActive()) CreateTween(); tween?.PlayForward(); }
        public void DOPlayBackwards() => tween?.PlayBackwards();
    }
}
