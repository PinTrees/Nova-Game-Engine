using NovaEngine;

// 모션 벡터 검사 (Tools/tests/run_tests.ps1 -Only motionvectors): Play 중에 움직이는 물체들
//  MotionMover   : +x 로 speed (m/s), maxX 를 넘으면 minX 로 (왼쪽에서 다시)
//  MotionSpinner : 제자리에서 Y 축으로 degreesPerSecond
//  CameraMover   : 켜면 (enabled) 카메라를 +x 로 speed — 멈춘 물체가 화면에서 왼쪽으로 흐른다
public class MotionMover : MonoBehaviour
{
    public float speed = 2f;
    public float minX = -1.5f;
    public float maxX = 1.5f;

    void Update()
    {
        var p = transform.position;
        p.x += speed * Time.deltaTime;
        if (p.x > maxX) p.x = minX;
        transform.position = p;
    }
}

public class MotionSpinner : MonoBehaviour
{
    public float degreesPerSecond = 180f;

    void Update()
    {
        transform.Rotate(0f, degreesPerSecond * Time.deltaTime, 0f);
    }
}

public class CameraMover : MonoBehaviour
{
    public float speed = 1f;

    void Update()
    {
        var p = transform.position;
        p.x += speed * Time.deltaTime;
        transform.position = p;
    }
}
