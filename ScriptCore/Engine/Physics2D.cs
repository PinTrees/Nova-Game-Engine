using System;
using NovaEngine.Interop;

namespace NovaEngine
{
    // Unity 의 2D 물리 (Box2D): Rigidbody2D · Collider2D · Physics2D · Collision2D · RaycastHit2D
    public enum RigidbodyType2D { Dynamic = 0, Kinematic = 1, Static = 2 }
    public enum ForceMode2D { Force = 0, Impulse = 1 }
    public enum CollisionDetectionMode2D { Discrete = 0, Continuous = 1 }

    [NativeComponent("Rigidbody2D")]
    public sealed unsafe class Rigidbody2D : Component
    {
        internal Rigidbody2D() { }
        Vector2 GetV(int p) { Vector2 v; Native.Api.R2_GetVec(m_Id, p, &v); return v; }
        void SetV(int p, Vector2 v) => Native.Api.R2_SetVec(m_Id, p, &v);
        float GetF(int p) => Native.Api.R2_GetFloat(m_Id, p);
        void SetF(int p, float v) => Native.Api.R2_SetFloat(m_Id, p, v);

        public RigidbodyType2D bodyType { get => (RigidbodyType2D)(int)GetF(6); set => SetF(6, (int)value); }
        public bool isKinematic { get => bodyType == RigidbodyType2D.Kinematic; set => bodyType = value ? RigidbodyType2D.Kinematic : RigidbodyType2D.Dynamic; }
        public Vector2 velocity { get => GetV(0); set => SetV(0, value); }
        public Vector2 linearVelocity { get => GetV(0); set => SetV(0, value); }
        /// <summary>도 / 초 (Unity 와 같음)</summary>
        public float angularVelocity { get => GetF(0); set => SetF(0, value); }
        public Vector2 position { get => GetV(1); set => SetV(1, value); }
        /// <summary>Z 회전 (도)</summary>
        public float rotation { get => GetF(1); set => SetF(1, value); }
        public float mass { get => GetF(2); set => SetF(2, value); }
        public float gravityScale { get => GetF(3); set => SetF(3, value); }
        public float linearDamping { get => GetF(4); set => SetF(4, value); }
        public float drag { get => GetF(4); set => SetF(4, value); }
        public float angularDamping { get => GetF(5); set => SetF(5, value); }
        public float angularDrag { get => GetF(5); set => SetF(5, value); }
        public bool freezeRotation { get => GetF(7) != 0; set => SetF(7, value ? 1 : 0); }
        public CollisionDetectionMode2D collisionDetectionMode { get => (CollisionDetectionMode2D)(int)GetF(8); set => SetF(8, (int)value); }

        public void AddForce(Vector2 force, ForceMode2D mode = ForceMode2D.Force) => Native.Api.R2_Act(m_Id, 0, &force, null, 0, (int)mode);
        public void AddForceAtPosition(Vector2 force, Vector2 position, ForceMode2D mode = ForceMode2D.Force) => Native.Api.R2_Act(m_Id, 1, &force, &position, 0, (int)mode);
        public void AddTorque(float torque, ForceMode2D mode = ForceMode2D.Force) => Native.Api.R2_Act(m_Id, 2, null, null, torque, (int)mode);
        /// <summary>다음 물리 단계에 그곳으로 (Kinematic 은 사이를 쓸고 지나간다)</summary>
        public void MovePosition(Vector2 position) => Native.Api.R2_Act(m_Id, 3, &position, null, 0, 0);
        public void MoveRotation(float angle) => Native.Api.R2_Act(m_Id, 4, null, null, angle, 0);
    }

    [NativeComponent("Collider2D")]
    public unsafe class Collider2D : Component
    {
        internal Collider2D() { }
        internal static Collider2D Of(ulong gameObject) => gameObject == 0 ? null : new Collider2D { m_Id = gameObject };
        internal virtual string TypeName => "";
        internal float Get(int prop, int i = 0)
        {
            float* v = stackalloc float[4];
            fixed (byte* t = Native.Utf8(TypeName)) Native.Api.C2_Get(m_Id, t, prop, v);
            return v[i];
        }
        internal Vector2 Get2(int prop)
        {
            float* v = stackalloc float[4];
            fixed (byte* t = Native.Utf8(TypeName)) Native.Api.C2_Get(m_Id, t, prop, v);
            return new Vector2(v[0], v[1]);
        }
        internal void Set(int prop, float a, float b = 0)
        {
            float* v = stackalloc float[4] { a, b, 0, 0 };
            fixed (byte* t = Native.Utf8(TypeName)) Native.Api.C2_Set(m_Id, t, prop, v);
        }
        public bool isTrigger { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }
        public Vector2 offset { get => Get2(1); set => Set(1, value.x, value.y); }
        public float friction { get => Get(4); set => Set(4, value); }
        public float bounciness { get => Get(5); set => Set(5, value); }
        public bool enabled { get => Get(6) != 0; set => Set(6, value ? 1 : 0); }
        /// <summary>이 콜라이더가 붙은 Rigidbody2D (자기 또는 부모)</summary>
        public Rigidbody2D attachedRigidbody
        {
            get
            {
                for (Transform t = transform; t != null; t = t.parent)
                    if (t.gameObject.GetComponent<Rigidbody2D>() is Rigidbody2D rb) return rb;
                return null;
            }
        }
    }

    [NativeComponent("BoxCollider2D")]
    public sealed class BoxCollider2D : Collider2D
    {
        internal BoxCollider2D() { }
        internal override string TypeName => "BoxCollider2D";
        public Vector2 size { get => Get2(2); set => Set(2, value.x, value.y); }
    }

    [NativeComponent("CircleCollider2D")]
    public sealed class CircleCollider2D : Collider2D
    {
        internal CircleCollider2D() { }
        internal override string TypeName => "CircleCollider2D";
        public float radius { get => Get(3); set => Set(3, value); }
    }

    [NativeComponent("CapsuleCollider2D")]
    public sealed class CapsuleCollider2D : Collider2D
    {
        internal CapsuleCollider2D() { }
        internal override string TypeName => "CapsuleCollider2D";
        public Vector2 size { get => Get2(2); set => Set(2, value.x, value.y); }
    }

    [NativeComponent("PolygonCollider2D")]
    public sealed class PolygonCollider2D : Collider2D
    {
        internal PolygonCollider2D() { }
        internal override string TypeName => "PolygonCollider2D";
    }

    [NativeComponent("EdgeCollider2D")]
    public sealed class EdgeCollider2D : Collider2D
    {
        internal EdgeCollider2D() { }
        internal override string TypeName => "EdgeCollider2D";
    }

    public struct ContactPoint2D
    {
        public Vector2 point;
        /// <summary>상대 표면에서 나를 향하는 법선 (바닥에 선 캐릭터 = 위)</summary>
        public Vector2 normal;
    }

    /// <summary>OnCollisionEnter2D 등이 받는 충돌 정보 (상대 = gameObject · collider)</summary>
    public unsafe class Collision2D
    {
        private readonly ulong m_Self, m_Other;
        private bool m_Read;
        private ContactPoint2D m_Contact;
        private Vector2 m_RelVel;
        private int m_Count;
        internal Collision2D(ulong self, ulong other) { m_Self = self; m_Other = other; }
        void Read()
        {
            if (m_Read) return;
            m_Read = true;
            float* v = stackalloc float[6];
            m_Count = Native.Api.P2_Contact(m_Self, m_Other, v);
            if (m_Count != 0)
            {
                m_Contact = new ContactPoint2D { point = new Vector2(v[0], v[1]), normal = new Vector2(v[2], v[3]) };
                m_RelVel = new Vector2(v[4], v[5]);
            }
        }
        public GameObject gameObject => new GameObject(m_Other);
        public Transform transform => gameObject.transform;
        public Collider2D collider => Collider2D.Of(m_Other);
        public Collider2D otherCollider => Collider2D.Of(m_Self);
        public Rigidbody2D rigidbody => gameObject.GetComponent<Rigidbody2D>();
        public Vector2 relativeVelocity { get { Read(); return m_RelVel; } }
        public int contactCount { get { Read(); return m_Count; } }
        public ContactPoint2D GetContact(int index) { Read(); return m_Contact; }
        public ContactPoint2D[] contacts { get { Read(); return m_Count > 0 ? new[] { m_Contact } : Array.Empty<ContactPoint2D>(); } }
    }

    public struct RaycastHit2D
    {
        internal ulong m_GameObject;
        public Vector2 point;
        public Vector2 normal;
        public float distance;
        public float fraction;
        public Collider2D collider => Collider2D.Of(m_GameObject);
        public Transform transform => m_GameObject == 0 ? null : new Transform(m_GameObject);
        public Rigidbody2D rigidbody => collider?.attachedRigidbody;
        /// <summary>맞았으면 true (Unity 처럼 if (hit) …)</summary>
        public static implicit operator bool(RaycastHit2D hit) => hit.m_GameObject != 0;
    }

    public static unsafe class Physics2D
    {
        public const int DefaultRaycastLayers = ~(1 << 2);
        public const int AllLayers = ~0;
        public const int IgnoreRaycastLayer = 1 << 2;

        /// <summary>중력 (Project Settings > Physics 2D 의 값으로 시작, Play 중 바꾸면 그 Play 동안만)</summary>
        public static Vector2 gravity
        {
            get { Vector2 g; Native.Api.P2_Gravity(0, &g); return g; }
            set { Native.Api.P2_Gravity(1, &value); }
        }

        public static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float distance = float.PositiveInfinity, int layerMask = DefaultRaycastLayers)
        {
            Ray2DData d;
            var hit = new RaycastHit2D();
            if (Native.Api.P2_Raycast(&origin, &direction, distance, layerMask, &d) != 0)
            {
                hit.m_GameObject = d.gameObject;
                hit.point = d.point;
                hit.normal = d.normal;
                hit.distance = d.distance;
                hit.fraction = d.fraction;
            }
            return hit;
        }

        public static Collider2D OverlapPoint(Vector2 point, int layerMask = DefaultRaycastLayers) => Collider2D.Of(Native.Api.P2_Overlap(0, &point, null, 0, layerMask));
        public static Collider2D OverlapCircle(Vector2 point, float radius, int layerMask = DefaultRaycastLayers) => Collider2D.Of(Native.Api.P2_Overlap(1, &point, null, radius, layerMask));
        public static Collider2D OverlapBox(Vector2 point, Vector2 size, float angle, int layerMask = DefaultRaycastLayers) => Collider2D.Of(Native.Api.P2_Overlap(2, &point, &size, angle, layerMask));

        /// <summary>두 레이어가 부딪히지 않게 (Play 동안만 — 멈추면 Project Settings 값)</summary>
        public static void IgnoreLayerCollision(int layer1, int layer2, bool ignore = true) => Native.Api.P2_IgnoreLayer(layer1, layer2, ignore ? 1 : 0);
        public static bool GetIgnoreLayerCollision(int layer1, int layer2) => Native.Api.P2_GetIgnoreLayer(layer1, layer2) != 0;
    }
}
