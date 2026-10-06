using System.Collections;
using System.Threading.Tasks;
using NovaEngine;
using NovaEngine.Tweening;

// Tween probe (Tools/tests/run_tests.ps1 -Only tween): com.nova.tween — tweens, sequences, loops, eases, punch/shake/jump, waits
public class TweenProbe : MonoBehaviour
{
    static string F(float v) => v.ToString("F2");
    static string V(Vector3 v) => $"{F(v.x)},{F(v.y)},{F(v.z)}";

    IEnumerator Start()
    {
        // 곡선 값 (표준 Penner 식)
        Debug.Log($"TweenProbe ease outBounce={EaseManager.Evaluate(Ease.OutBounce, 0.5f):F4} inOutQuad={EaseManager.Evaluate(Ease.InOutQuad, 0.25f):F4} outBack1={EaseManager.Evaluate(Ease.OutBack, 1f):F4} inCubic={EaseManager.Evaluate(Ease.InCubic, 0.5f):F4} linear={EaseManager.Evaluate(Ease.Linear, 0.3f):F2}");

        // 1) 이동 + 끝 콜백 + 코루틴 기다리기
        var a = new GameObject("TA").transform;
        int completes = 0;
        float t0 = Time.time;
        var move = a.DOMoveX(5f, 0.5f).SetEase(Ease.Linear).OnComplete(() => completes++);
        yield return move.WaitForCompletion();
        Debug.Log($"TweenProbe move x={F(a.position.x)} completes={completes} took={(Time.time - t0 >= 0.45f && Time.time - t0 < 0.8f)} active={move.IsActive()}");

        // 2) 시퀀스: Append · Join · 간격 · 콜백 · 회전
        var b = new GameObject("TB").transform;
        string order = "";
        var seq = NovaTween.Sequence()
            .Append(b.DOMoveY(2f, 0.3f).OnComplete(() => order += "y"))
            .Join(b.DOScale(2f, 0.3f).OnComplete(() => order += "s"))
            .AppendInterval(0.1f)
            .AppendCallback(() => order += "c")
            .Append(b.DORotate(new Vector3(0, 90, 0), 0.3f).OnComplete(() => order += "r"));
        float seqDur = seq.Duration();
        yield return seq.WaitForCompletion();
        Debug.Log($"TweenProbe sequence pos={V(b.position)} scale={F(b.localScale.x)} rotY={F(b.eulerAngles.y)} order={order} duration={F(seqDur)}");

        // 3) Yoyo 2 번 → 제자리, 한 바퀴마다 알림
        var c = new GameObject("TC").transform;
        int steps = 0;
        var yoyo = c.DOLocalMoveZ(1f, 0.2f).SetLoops(2, LoopType.Yoyo).OnStepComplete(() => steps++);
        float mid = 0f;
        yield return new WaitForSeconds(0.22f);
        mid = c.localPosition.z;
        yield return yoyo.WaitForCompletion();
        Debug.Log($"TweenProbe yoyo z={F(c.localPosition.z)} steps={steps} mid={(mid > 0.7f)}");

        // 4) From: 0 에서 지금 크기로 · 5) Relative + Incremental 3 번 = +3
        var d = new GameObject("TD").transform;
        var from = d.DOScale(0f, 0.2f).From();
        float startScale = d.localScale.x;
        var e = new GameObject("TE").transform;
        var inc = e.DOMoveX(1f, 0.1f).SetRelative().SetLoops(3, LoopType.Incremental).SetEase(Ease.Linear);
        yield return inc.WaitForCompletion();
        yield return from.WaitForCompletion();
        Debug.Log($"TweenProbe from start={F(startScale)} end={F(d.localScale.x)} incremental x={F(e.position.x)}");

        // 6) 튀기기 · 흔들기 → 제자리, 뛰기 → 끝 위치 (가운데는 높다)
        var p = new GameObject("TP").transform;
        p.position = new Vector3(1, 1, 1);
        var punch = p.DOPunchPosition(new Vector3(0, 2, 0), 0.3f);
        var s = new GameObject("TS").transform;
        var shake = s.DOShakePosition(0.3f, 1f);
        float shakeMax = 0f;
        var j = new GameObject("TJ").transform;
        var jump = j.DOJump(new Vector3(4, 0, 0), 2f, 1, 0.4f);
        float jumpPeak = 0f;
        while (punch.IsActive() || shake.IsActive() || jump.IsActive())
        {
            shakeMax = Mathf.Max(shakeMax, s.position.magnitude);
            jumpPeak = Mathf.Max(jumpPeak, j.position.y);
            yield return null;
        }
        Debug.Log($"TweenProbe punch end={V(p.position)} shake end={V(s.position)} moved={(shakeMax > 0.2f)} jump end={V(j.position)} peak={(jumpPeak > 1.5f)}");

        // 7) 값 트윈 · 늦게 부르기 · async
        float virt = 0f;
        DOVirtual.Float(0f, 10f, 0.2f, v => virt = v);
        bool delayed = false;
        DOVirtual.DelayedCall(0.1f, () => delayed = true);
        Task task = new GameObject("TT").transform.DOMoveZ(3f, 0.2f).AsyncWaitForCompletion();
        yield return new WaitForSeconds(0.35f);
        Debug.Log($"TweenProbe virtual={F(virt)} delayed={delayed} task={task.IsCompleted}");

        // 8) 대상을 지우면 트윈도 조용히 끝, Kill(대상) · 거꾸로 재생
        var k = new GameObject("TK").transform;
        var killed = k.DOMoveY(5f, 1f);
        Destroy(k.gameObject);
        var m = new GameObject("TM").transform;
        m.DOMoveX(9f, 1f);
        int n = NovaTween.Kill(m);
        var r = new GameObject("TR").transform;
        var back = r.DOMoveX(2f, 0.2f).SetEase(Ease.Linear).SetAutoKill(false);
        yield return back.WaitForCompletion();
        back.PlayBackwards();
        yield return new WaitForSeconds(0.3f);
        yield return null;
        Debug.Log($"TweenProbe destroyedTarget={!killed.IsActive()} killByTarget={n} backwards x={F(r.position.x)} active={NovaTween.TotalActiveTweens()}");

        // 9) 빛 · 카메라 · TweenAnimation 컴포넌트 (씬에 둔 것)
        var lightGo = GameObject.Find("Directional Light");
        var light = lightGo != null ? lightGo.GetComponent<Light>() : null;
        var cam = Camera.main;
        float fov = cam != null ? cam.fieldOfView : 0f;
        Tween lt = light != null ? light.DOIntensity(3f, 0.2f) : null;
        Tween ct = cam != null ? cam.DOFieldOfView(30f, 0.2f) : null;
        yield return new WaitForSeconds(0.3f);
        var anim = GameObject.Find("Animated");
        Debug.Log($"TweenProbe light={(light != null ? F(light.intensity) : "none")} fovFrom={F(fov)} fov={(cam != null ? F(cam.fieldOfView) : "none")} animated={(anim != null ? V(anim.transform.position) : "none")}");
        Debug.Log("TweenProbe done");
    }
}
