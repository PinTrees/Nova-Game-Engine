using System.Collections;
using NovaEngine;

// 플레이어 (안드로이드 · 웹) 기능 검사: 스킨 천 (치마 · 망토), Starter Assets 차 (스크립트 입력) · 래그돌 표적 (광선),
//  낮 · 밤 (밤 22 시 → NightLight 가로등). 로그: "FeatureProbe start …", "FeatureProbe done …"
public class FeaturesPlayerProbe : MonoBehaviour
{
    static string F(float v) => v.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);

    static void Span(Cloth c, out float top, out float bottom)
    {
        top = -99f; bottom = 99f;
        if (c == null) return;
        var t = c.transform;
        foreach (var lp in c.vertices)
        {
            var p = t.TransformPoint(lp);
            top = Mathf.Max(top, p.y);
            bottom = Mathf.Min(bottom, p.y);
        }
    }

    IEnumerator Start()
    {
        yield return new WaitForSeconds(1.0f);
        var skirtGo = GameObject.Find("Skirt");
        var capeGo = GameObject.Find("Cape");
        var skirt = skirtGo != null ? skirtGo.GetComponent<Cloth>() : null;
        var cape = capeGo != null ? capeGo.GetComponent<Cloth>() : null;
        // 느린 기기 (MuMu) 는 첫 프레임이 길어 첫 물리 스텝 (천을 만든다) 이 늦다 — 시뮬레이션이 시작될 때까지 (5 초까지)
        float wait0 = Time.time;
        while ((skirt == null || !skirt.isSimulating || cape == null || !cape.isSimulating) && Time.time - wait0 < 5f)
            yield return null;
        Span(skirt, out float sTop0, out float sBot0);
        var carGo = GameObject.Find("Car");
        var car = carGo != null ? carGo.GetComponent<StarterAssets.CarController>() : null;
        if (car != null) { car.readKeyboard = false; car.throttle = 1f; }
        var dummy = GameObject.Find("Dummy");
        var target = dummy != null ? dummy.GetComponent<StarterAssets.RagdollTarget>() : null;
        var shooter = Camera.main != null ? Camera.main.GetComponent<StarterAssets.RagdollShooter>() : null;
        bool hit = false;
        if (dummy != null && shooter != null)
        {
            shooter.readMouse = false;
            var chest = dummy.GetComponent<Animator>().GetBonePosition(HumanBodyBones.Chest);
            hit = shooter.Shoot(new Ray(chest + new Vector3(0f, 0f, -2f), Vector3.forward));
        }
        DayNight.paused = true;
        DayNight.timeOfDay = 22f;
        Debug.Log($"FeatureProbe start platform={Application.platform} cloth={(skirt != null && skirt.isSimulating)},{(cape != null && cape.isSimulating)} skirtTop={F(sTop0)} car={car != null} shot={hit} daynight={DayNight.exists}");

        yield return new WaitForSeconds(3.5f);
        Span(skirt, out float sTop1, out float sBot1);
        float pelvisY = 9f;
        if (dummy != null)
            foreach (var rb in dummy.GetComponentsInChildren<Rigidbody>())
                if (rb.gameObject.name.Contains("Pelvis")) pelvisY = rb.position.y;
        var lampGo = GameObject.Find("Lamp");
        var lamp = lampGo != null ? lampGo.GetComponent<NightLight>() : null;
        Debug.Log($"FeatureProbe done skirt={F(sTop0)}>{F(sTop1)},{F(sBot0)}>{F(sBot1)} car={(car != null ? F(car.speed) : "none")},{(car != null ? car.groundedWheels : 0)} ragdoll={(target != null && target.down)},{F(pelvisY)} night={DayNight.isNight},{(lamp != null && lamp.isOn)}");
    }
}
