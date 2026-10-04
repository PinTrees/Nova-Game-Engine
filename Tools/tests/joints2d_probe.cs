using System;
using System.Text.Json;
using NovaEngine;

// Copied only into the standalone test project by joints2d.ps1.
public class JointBreak2DProbe : MonoBehaviour
{
    public static int forceBreaks, torqueBreaks;
    public static bool typed = true, readable = true;
    void OnJointBreak2D(Joint2D joint)
    {
        if (gameObject.name == "BreakForce") { forceBreaks++; typed &= joint is FixedJoint2D; readable &= joint.reactionForce.magnitude > 1; }
        else { torqueBreaks++; typed &= joint is HingeJoint2D; readable &= Math.Abs(joint.reactionTorque) > 0.1f; }
        readable &= joint.enabled && joint.gameObject.name == gameObject.name && joint == GetComponent<Joint2D>();
    }
}

public class Joint2DProbe : MonoBehaviour
{
    public static float elapsed, hingePivotError, hingeTravel, limitedAngle, freeSpeed, fixedError, sliderError, sliderRange;
    public static float ropeMin, ropeMax, springMin, springMax, dampedError, dampedMin, wheelSpeed, rearWheelSpeed, wheelTravel, wheelAxisError;
    public static float finalLimitedAngle, autoDistance, autoAngle;
    public static bool changed, recovered, createdBodyImmediately, distinctComponents;
    bool reported;
    static GameObject Body(string name, float x, float y, bool gravity = false)
    {
        var g = new GameObject(name); g.transform.position = new Vector3(x, y, 0);
        var c = g.AddComponent<BoxCollider2D>(); c.size = new Vector2(0.4f, 0.4f);
        return g;
    }
    static T Joint<T>(string name, float x, float y, bool gravity = false) where T : Joint2D
    {
        var g = Body(name, x, y, gravity);
        var j = g.AddComponent<T>();
        var rb = g.GetComponent<Rigidbody2D>();
        createdBodyImmediately &= rb != null;
        rb.gravityScale = gravity ? 1 : 0; rb.angularDamping = 0;
        return j;
    }
    public static string Setup()
    {
        createdBodyImmediately = true;
        var h = Joint<HingeJoint2D>("Hinge", 0, 4, true); h.anchor = new Vector2(1, 0);
        h.gameObject.GetComponent<BoxCollider2D>().size = new Vector2(2, 0.25f);
        var limited = Joint<HingeJoint2D>("Limited", 5, 4);
        limited.useMotor = true; limited.motor = new JointMotor2D { motorSpeed = 90, maxMotorTorque = 100 };
        limited.useLimits = true; limited.limits = new JointAngleLimits2D { min = -30, max = 30 };
        var motor = Joint<HingeJoint2D>("Motor", 8, 4);
        motor.useMotor = true; motor.motor = new JointMotor2D { motorSpeed = 90, maxMotorTorque = 100 };
        var pairA = Body("FixedA", 12, 4); var pairRb = pairA.AddComponent<Rigidbody2D>(); pairRb.angularDamping = 0;
        var fixedJ = Joint<FixedJoint2D>("FixedB", 13, 4, true); fixedJ.connectedBody = pairRb;
        var spring = Joint<SpringJoint2D>("Spring", 20, 2);
        spring.autoConfigureConnectedAnchor = false; spring.connectedAnchor = new Vector2(18, 2);
        spring.autoConfigureDistance = false; spring.distance = 1; spring.frequency = 2; spring.dampingRatio = 0;
        var damped = Joint<SpringJoint2D>("Damped", 20, 7);
        damped.autoConfigureConnectedAnchor = false; damped.connectedAnchor = new Vector2(18, 7);
        damped.autoConfigureDistance = false; damped.distance = 1; damped.frequency = 2; damped.dampingRatio = 1;
        var distanceAnchor = Body("DistanceAnchor", 25, 6).AddComponent<Rigidbody2D>();
        distanceAnchor.angularDamping = 0;
        var dist = Joint<DistanceJoint2D>("Distance", 25, 4, true);
        dist.connectedBody = distanceAnchor;
        dist.autoConfigureConnectedAnchor = false; dist.connectedAnchor = Vector2.zero; dist.distance = 2; dist.autoConfigureDistance = false;
        dist.attachedRigidbody.velocity = new Vector2(1, 0);
        var rope = Joint<DistanceJoint2D>("Rope", 30, 4);
        rope.autoConfigureConnectedAnchor = false; rope.connectedAnchor = new Vector2(30, 6); rope.autoConfigureDistance = false;
        rope.distance = 2; rope.maxDistanceOnly = true; rope.attachedRigidbody.velocity = new Vector2(0, 2);
        var chassis = Body("Chassis", 36, 4); chassis.GetComponent<BoxCollider2D>().size = new Vector2(2, 0.4f);
        var chassisRb = chassis.AddComponent<Rigidbody2D>(); chassisRb.mass = 3; chassisRb.angularDamping = 1; chassisRb.freezeRotation = true;
        for (int i = 0; i < 2; i++)
        {
            float offset = i == 0 ? -0.75f : 0.75f;
            var wheelGo = new GameObject(i == 0 ? "Wheel" : "RearWheel"); wheelGo.transform.position = new Vector3(36 + offset, 3.5f, 0);
            var circle = wheelGo.AddComponent<CircleCollider2D>(); circle.radius = 0.5f; circle.friction = 1;
            var wheel = wheelGo.AddComponent<WheelJoint2D>(); wheel.connectedBody = chassisRb;
            wheel.autoConfigureConnectedAnchor = false; wheel.connectedAnchor = new Vector2(offset, -0.5f);
            wheel.suspension = new JointSuspension2D { frequency = 4, dampingRatio = 0.8f, angle = 90 };
            wheel.useMotor = true; wheel.motor = new JointMotor2D { motorSpeed = -360, maxMotorTorque = 30 };
        }
        var ground = new GameObject("Ground"); ground.transform.position = new Vector3(40, 0, 0);
        var groundCollider = ground.AddComponent<BoxCollider2D>(); groundCollider.size = new Vector2(14, 1); groundCollider.friction = 1;
        var slider = Joint<SliderJoint2D>("Slider", 50, 4, true);
        slider.autoConfigureAngle = false; slider.angle = 0; slider.useLimits = true;
        slider.limits = new JointTranslationLimits2D { min = -1, max = 1 };
        slider.useMotor = true; slider.motor = new JointMotor2D { motorSpeed = 2, maxMotorTorque = 100 };
        var breakForce = Joint<FixedJoint2D>("BreakForce", 55, 6, true); breakForce.breakForce = 1;
        breakForce.gameObject.AddComponent<JointBreak2DProbe>();
        var breakTorque = Joint<HingeJoint2D>("BreakTorque", 60, 6);
        breakTorque.useMotor = true; breakTorque.motor = new JointMotor2D { motorSpeed = 360, maxMotorTorque = 100 }; breakTorque.breakTorque = 0.1f;
        breakTorque.gameObject.AddComponent<JointBreak2DProbe>();
        var automatic = Joint<DistanceJoint2D>("Automatic", 70, 5);
        automatic.connectedAnchor = new Vector2(70, 8);
        var autoSlider = Joint<SliderJoint2D>("AutoSlider", 80, 5);
        autoSlider.autoConfigureConnectedAnchor = false; autoSlider.connectedAnchor = new Vector2(80, 8);
        var multi = Joint<HingeJoint2D>("Multiple", 90, 4);
        var second = multi.gameObject.AddComponent<HingeJoint2D>(); second.motor = new JointMotor2D { motorSpeed = 123, maxMotorTorque = 45 }; second.enabled = false;
        distinctComponents = multi != second && multi.motor.motorSpeed == 0 && second.motor.motorSpeed == 123 && multi.gameObject.GetComponents<Joint2D>().Length == 2;
        var parent = new GameObject("JointGroup");
        var a = Body("GroupA", 100, 5); a.transform.parent = parent.transform; a.AddComponent<Rigidbody2D>().gravityScale = 0;
        var b = Joint<FixedJoint2D>("GroupB", 101, 5); b.transform.parent = parent.transform; b.connectedBody = a.GetComponent<Rigidbody2D>();
        new GameObject("Recorder").AddComponent<Joint2DProbe>();
        return createdBodyImmediately + "," + distinctComponents;
    }
    public void Start()
    {
        GameObject.Find("Distance").GetComponent<Rigidbody2D>().velocity = new Vector2(1, 0);
        GameObject.Find("DistanceAnchor").GetComponent<Rigidbody2D>().velocity = new Vector2(1, 0);
        GameObject.Find("Rope").GetComponent<Rigidbody2D>().velocity = new Vector2(0, 2);
        elapsed = hingePivotError = hingeTravel = limitedAngle = fixedError = sliderError = sliderRange = ropeMax = wheelTravel = wheelAxisError = 0;
        ropeMin = springMin = dampedMin = 100; springMax = freeSpeed = dampedError = wheelSpeed = rearWheelSpeed = 0;
        changed = recovered = false; JointBreak2DProbe.forceBreaks = JointBreak2DProbe.torqueBreaks = 0;
        JointBreak2DProbe.typed = JointBreak2DProbe.readable = true;
    }
    static Vector2 Position(string name) { var p = GameObject.Find(name).transform.position; return new Vector2(p.x, p.y); }
    public void Update()
    {
        elapsed += Time.deltaTime;
        // A sustained external load exercises reaction torque, rather than a motor's
        // startup impulse which can finish inside one of Box2D's four substeps.
        GameObject.Find("BreakTorque").GetComponent<Rigidbody2D>().AddTorque(10);
        var hinge = GameObject.Find("Hinge");
        var pivot3 = hinge.transform.TransformPoint(new Vector3(1, 0, 0));
        hingePivotError = Math.Max(hingePivotError, new Vector2(pivot3.x - 1, pivot3.y - 4).magnitude);
        hingeTravel = Math.Max(hingeTravel, Math.Abs(hinge.GetComponent<HingeJoint2D>().jointAngle));
        var limited = GameObject.Find("Limited").GetComponent<HingeJoint2D>();
        limitedAngle = Math.Max(limitedAngle, Math.Abs(limited.jointAngle)); finalLimitedAngle = limited.jointAngle;
        freeSpeed = GameObject.Find("Motor").GetComponent<HingeJoint2D>().jointSpeed;
        fixedError = Math.Max(fixedError, (Position("FixedB") - Position("FixedA") - new Vector2(1, 0)).magnitude);
        var slider = GameObject.Find("Slider").GetComponent<SliderJoint2D>();
        sliderError = Math.Max(sliderError, Math.Abs(Position("Slider").y - 4)); sliderRange = Math.Max(sliderRange, Math.Abs(slider.jointTranslation));
        float rope = (Position("Rope") - new Vector2(30, 6)).magnitude;
        ropeMin = Math.Min(ropeMin, rope); ropeMax = Math.Max(ropeMax, rope);
        float spring = (Position("Spring") - new Vector2(18, 2)).magnitude;
        springMin = Math.Min(springMin, spring); springMax = Math.Max(springMax, spring);
        float dampedLength = (Position("Damped") - new Vector2(18, 7)).magnitude;
        dampedError = Math.Abs(dampedLength - 1); dampedMin = Math.Min(dampedMin, dampedLength);
        var wheel = GameObject.Find("Wheel").GetComponent<WheelJoint2D>();
        wheelSpeed = wheel.jointSpeed; wheelTravel = Position("Chassis").x - 36;
        rearWheelSpeed = GameObject.Find("RearWheel").GetComponent<WheelJoint2D>().jointSpeed;
        // Both suspensions follow the connected chassis's local axis after creation.
        for (int i = 0; i < 2; i++)
        {
            var cp = GameObject.Find("Chassis").transform.TransformPoint(new Vector3(i == 0 ? -0.75f : 0.75f, -0.5f, 0));
            var relative = Position(i == 0 ? "Wheel" : "RearWheel") - new Vector2(cp.x, cp.y);
            var axis3 = GameObject.Find("Chassis").transform.TransformDirection(Vector3.up);
            wheelAxisError = Math.Max(wheelAxisError, Math.Abs(relative.x * axis3.y - relative.y * axis3.x));
        }
        autoDistance = GameObject.Find("Automatic").GetComponent<DistanceJoint2D>().distance;
        autoAngle = GameObject.Find("AutoSlider").GetComponent<SliderJoint2D>().angle;
        if (elapsed > 1.2f && !changed)
        {
            limited.limits = new JointAngleLimits2D { min = -15, max = 15 };
            limited.motor = new JointMotor2D { motorSpeed = -90, maxMotorTorque = 100 };
            GameObject.Find("Motor").GetComponent<Rigidbody2D>().bodyType = RigidbodyType2D.Kinematic;
            changed = true;
        }
        if (elapsed > 1.5f && !recovered)
        {
            GameObject.Find("Motor").GetComponent<Rigidbody2D>().bodyType = RigidbodyType2D.Dynamic;
            recovered = true;
        }
        if (elapsed > 2.5f && !reported) { reported = true; Debug.Log("JOINTS2D_REPORT " + Report()); }
        if (!Application.isEditor && elapsed > 3.0f) Application.Quit();
    }
    public static string Report()
    {
        var distance = (Position("Distance") - Position("DistanceAnchor")).magnitude;
        var distanceAnchorY = Position("DistanceAnchor").y;
        return JsonSerializer.Serialize(new { elapsed, hingePivotError, hingeTravel, limitedAngle, freeSpeed, fixedError, sliderError, sliderRange,
            ropeMin, ropeMax, springMin, springMax, dampedError, dampedMin, wheelSpeed, rearWheelSpeed, wheelTravel, wheelAxisError, finalLimitedAngle, autoDistance, autoAngle,
            distance, distanceAnchorY, changed, recovered, createdBodyImmediately, distinctComponents,
            forceBreaks = JointBreak2DProbe.forceBreaks, torqueBreaks = JointBreak2DProbe.torqueBreaks,
            typed = JointBreak2DProbe.typed, readable = JointBreak2DProbe.readable,
            forceRemoved = GameObject.Find("BreakForce").GetComponent<FixedJoint2D>() == null,
            torqueRemoved = GameObject.Find("BreakTorque").GetComponent<HingeJoint2D>() == null,
            fixedY = Position("FixedA").y });
    }
}
