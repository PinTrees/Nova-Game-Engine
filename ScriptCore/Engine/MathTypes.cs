using System;
using System.Globalization;
using System.Runtime.InteropServices;

namespace NovaEngine
{
    // Unity 와 같은 수학 타입. 좌표계도 Unity 와 같다 (왼손, Y 위, Z 앞).

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector2 : IEquatable<Vector2>
    {
        public float x, y;
        public Vector2(float x, float y) { this.x = x; this.y = y; }

        public static Vector2 zero => new Vector2(0, 0);
        public static Vector2 one => new Vector2(1, 1);
        public static Vector2 up => new Vector2(0, 1);
        public static Vector2 down => new Vector2(0, -1);
        public static Vector2 left => new Vector2(-1, 0);
        public static Vector2 right => new Vector2(1, 0);

        public float this[int i] { get => i == 0 ? x : y; set { if (i == 0) x = value; else y = value; } }
        public float sqrMagnitude => x * x + y * y;
        public float magnitude => MathF.Sqrt(sqrMagnitude);
        public Vector2 normalized { get { float m = magnitude; return m > 1e-5f ? this / m : zero; } }
        public void Normalize() { this = normalized; }
        public void Set(float nx, float ny) { x = nx; y = ny; }

        public static float Dot(Vector2 a, Vector2 b) => a.x * b.x + a.y * b.y;
        public static float Distance(Vector2 a, Vector2 b) => (a - b).magnitude;
        public static Vector2 Lerp(Vector2 a, Vector2 b, float t) { t = Mathf.Clamp01(t); return a + (b - a) * t; }
        public static Vector2 LerpUnclamped(Vector2 a, Vector2 b, float t) => a + (b - a) * t;
        public static Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDelta)
        {
            Vector2 d = target - current; float m = d.magnitude;
            return m <= maxDelta || m == 0 ? target : current + d / m * maxDelta;
        }
        public static Vector2 ClampMagnitude(Vector2 v, float max) => v.sqrMagnitude > max * max ? v.normalized * max : v;
        public static float Angle(Vector2 a, Vector2 b)
        {
            float d = MathF.Sqrt(a.sqrMagnitude * b.sqrMagnitude);
            return d < 1e-15f ? 0 : MathF.Acos(Mathf.Clamp(Dot(a, b) / d, -1, 1)) * Mathf.Rad2Deg;
        }
        public static Vector2 Scale(Vector2 a, Vector2 b) => new Vector2(a.x * b.x, a.y * b.y);
        public static Vector2 Min(Vector2 a, Vector2 b) => new Vector2(MathF.Min(a.x, b.x), MathF.Min(a.y, b.y));
        public static Vector2 Max(Vector2 a, Vector2 b) => new Vector2(MathF.Max(a.x, b.x), MathF.Max(a.y, b.y));

        public static Vector2 operator +(Vector2 a, Vector2 b) => new Vector2(a.x + b.x, a.y + b.y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new Vector2(a.x - b.x, a.y - b.y);
        public static Vector2 operator -(Vector2 a) => new Vector2(-a.x, -a.y);
        public static Vector2 operator *(Vector2 a, float d) => new Vector2(a.x * d, a.y * d);
        public static Vector2 operator *(float d, Vector2 a) => new Vector2(a.x * d, a.y * d);
        public static Vector2 operator *(Vector2 a, Vector2 b) => new Vector2(a.x * b.x, a.y * b.y);
        public static Vector2 operator /(Vector2 a, float d) => new Vector2(a.x / d, a.y / d);
        public static bool operator ==(Vector2 a, Vector2 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector2 a, Vector2 b) => !(a == b);
        public static implicit operator Vector3(Vector2 v) => new Vector3(v.x, v.y, 0);
        public static implicit operator Vector2(Vector3 v) => new Vector2(v.x, v.y);

        public bool Equals(Vector2 o) => x == o.x && y == o.y;
        public override bool Equals(object o) => o is Vector2 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F2}, {1:F2})", x, y);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3 : IEquatable<Vector3>
    {
        public float x, y, z;
        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        public Vector3(float x, float y) { this.x = x; this.y = y; z = 0; }

        public static Vector3 zero => new Vector3(0, 0, 0);
        public static Vector3 one => new Vector3(1, 1, 1);
        public static Vector3 up => new Vector3(0, 1, 0);
        public static Vector3 down => new Vector3(0, -1, 0);
        public static Vector3 left => new Vector3(-1, 0, 0);
        public static Vector3 right => new Vector3(1, 0, 0);
        public static Vector3 forward => new Vector3(0, 0, 1);
        public static Vector3 back => new Vector3(0, 0, -1);
        public static Vector3 positiveInfinity => new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
        public static Vector3 negativeInfinity => new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

        public float this[int i]
        {
            get => i == 0 ? x : (i == 1 ? y : z);
            set { if (i == 0) x = value; else if (i == 1) y = value; else z = value; }
        }
        public float sqrMagnitude => x * x + y * y + z * z;
        public float magnitude => MathF.Sqrt(sqrMagnitude);
        public Vector3 normalized { get { float m = magnitude; return m > 1e-5f ? this / m : zero; } }
        public void Normalize() { this = normalized; }
        public void Set(float nx, float ny, float nz) { x = nx; y = ny; z = nz; }

        public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;
        public static Vector3 Cross(Vector3 a, Vector3 b) => new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        public static float Distance(Vector3 a, Vector3 b) => (a - b).magnitude;
        public static Vector3 Normalize(Vector3 v) => v.normalized;
        public static float Magnitude(Vector3 v) => v.magnitude;
        public static float SqrMagnitude(Vector3 v) => v.sqrMagnitude;
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t) { t = Mathf.Clamp01(t); return a + (b - a) * t; }
        public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t) => a + (b - a) * t;
        public static Vector3 Slerp(Vector3 a, Vector3 b, float t)
        {
            t = Mathf.Clamp01(t);
            float ma = a.magnitude, mb = b.magnitude;
            if (ma < 1e-6f || mb < 1e-6f) return Lerp(a, b, t);
            Vector3 na = a / ma, nb = b / mb;
            float dot = Mathf.Clamp(Dot(na, nb), -1, 1);
            float theta = MathF.Acos(dot) * t;
            Vector3 rel = (nb - na * dot).normalized;
            return (na * MathF.Cos(theta) + rel * MathF.Sin(theta)) * Mathf.Lerp(ma, mb, t);
        }
        public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDelta)
        {
            Vector3 d = target - current; float m = d.magnitude;
            return m <= maxDelta || m == 0 ? target : current + d / m * maxDelta;
        }
        public static Vector3 ClampMagnitude(Vector3 v, float max) => v.sqrMagnitude > max * max ? v.normalized * max : v;
        public static float Angle(Vector3 a, Vector3 b)
        {
            float d = MathF.Sqrt(a.sqrMagnitude * b.sqrMagnitude);
            return d < 1e-15f ? 0 : MathF.Acos(Mathf.Clamp(Dot(a, b) / d, -1, 1)) * Mathf.Rad2Deg;
        }
        public static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
        {
            float angle = Angle(from, to);
            return Dot(axis, Cross(from, to)) < 0 ? -angle : angle;
        }
        public static Vector3 Project(Vector3 v, Vector3 onNormal)
        {
            float s = Dot(onNormal, onNormal);
            return s < 1e-15f ? zero : onNormal * (Dot(v, onNormal) / s);
        }
        public static Vector3 ProjectOnPlane(Vector3 v, Vector3 planeNormal) => v - Project(v, planeNormal);
        public static Vector3 Reflect(Vector3 dir, Vector3 normal) => dir - 2f * Dot(normal, dir) * normal;
        public static Vector3 Scale(Vector3 a, Vector3 b) => new Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
        public static Vector3 Min(Vector3 a, Vector3 b) => new Vector3(MathF.Min(a.x, b.x), MathF.Min(a.y, b.y), MathF.Min(a.z, b.z));
        public static Vector3 Max(Vector3 a, Vector3 b) => new Vector3(MathF.Max(a.x, b.x), MathF.Max(a.y, b.y), MathF.Max(a.z, b.z));
        public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 velocity, float smoothTime, float maxSpeed = float.PositiveInfinity, float deltaTime = -1)
        {
            if (deltaTime < 0) deltaTime = Time.deltaTime;
            float vx = velocity.x, vy = velocity.y, vz = velocity.z;
            var r = new Vector3(
                Mathf.SmoothDamp(current.x, target.x, ref vx, smoothTime, maxSpeed, deltaTime),
                Mathf.SmoothDamp(current.y, target.y, ref vy, smoothTime, maxSpeed, deltaTime),
                Mathf.SmoothDamp(current.z, target.z, ref vz, smoothTime, maxSpeed, deltaTime));
            velocity = new Vector3(vx, vy, vz);
            return r;
        }

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.x, -a.y, -a.z);
        public static Vector3 operator *(Vector3 a, float d) => new Vector3(a.x * d, a.y * d, a.z * d);
        public static Vector3 operator *(float d, Vector3 a) => new Vector3(a.x * d, a.y * d, a.z * d);
        public static Vector3 operator /(Vector3 a, float d) => new Vector3(a.x / d, a.y / d, a.z / d);
        public static bool operator ==(Vector3 a, Vector3 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector3 a, Vector3 b) => !(a == b);

        public bool Equals(Vector3 o) => x == o.x && y == o.y && z == o.z;
        public override bool Equals(object o) => o is Vector3 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y, z);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F2}, {1:F2}, {2:F2})", x, y, z);
        public string ToString(string format) => string.Format(CultureInfo.InvariantCulture, "({0}, {1}, {2})", x.ToString(format, CultureInfo.InvariantCulture), y.ToString(format, CultureInfo.InvariantCulture), z.ToString(format, CultureInfo.InvariantCulture));
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector4 : IEquatable<Vector4>
    {
        public float x, y, z, w;
        public Vector4(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }
        public static Vector4 zero => new Vector4(0, 0, 0, 0);
        public static Vector4 one => new Vector4(1, 1, 1, 1);
        public float magnitude => MathF.Sqrt(x * x + y * y + z * z + w * w);
        public static Vector4 Lerp(Vector4 a, Vector4 b, float t) { t = Mathf.Clamp01(t); return a + (b - a) * t; }
        public static Vector4 operator +(Vector4 a, Vector4 b) => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
        public static Vector4 operator -(Vector4 a, Vector4 b) => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
        public static Vector4 operator *(Vector4 a, float d) => new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
        public static Vector4 operator /(Vector4 a, float d) => new Vector4(a.x / d, a.y / d, a.z / d, a.w / d);
        public static implicit operator Vector4(Vector3 v) => new Vector4(v.x, v.y, v.z, 0);
        public static implicit operator Vector3(Vector4 v) => new Vector3(v.x, v.y, v.z);
        public bool Equals(Vector4 o) => x == o.x && y == o.y && z == o.z && w == o.w;
        public override bool Equals(object o) => o is Vector4 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y, z, w);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F2}, {1:F2}, {2:F2}, {3:F2})", x, y, z, w);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Quaternion : IEquatable<Quaternion>
    {
        public float x, y, z, w;
        public Quaternion(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }

        public static Quaternion identity => new Quaternion(0, 0, 0, 1);

        // Unity 와 같은 회전 순서: Z → X → Y (q = qy * qx * qz)
        public static Quaternion Euler(float x, float y, float z)
        {
            float hx = x * Mathf.Deg2Rad * 0.5f, hy = y * Mathf.Deg2Rad * 0.5f, hz = z * Mathf.Deg2Rad * 0.5f;
            float sx = MathF.Sin(hx), cx = MathF.Cos(hx), sy = MathF.Sin(hy), cy = MathF.Cos(hy), sz = MathF.Sin(hz), cz = MathF.Cos(hz);
            return new Quaternion(
                cy * sx * cz + sy * cx * sz,
                sy * cx * cz - cy * sx * sz,
                cy * cx * sz - sy * sx * cz,
                cy * cx * cz + sy * sx * sz);
        }
        public static Quaternion Euler(Vector3 e) => Euler(e.x, e.y, e.z);

        public Vector3 eulerAngles
        {
            get
            {
                float sinx = 2f * (w * x - y * z);
                float ex, ey, ez;
                if (MathF.Abs(sinx) >= 0.99999f)
                {
                    ex = MathF.CopySign(MathF.PI / 2, sinx);
                    ey = 2f * MathF.Atan2(y, w);
                    ez = 0;
                }
                else
                {
                    ex = MathF.Asin(sinx);
                    ey = MathF.Atan2(2f * (w * y + x * z), 1f - 2f * (x * x + y * y));
                    ez = MathF.Atan2(2f * (w * z + x * y), 1f - 2f * (x * x + z * z));
                }
                return new Vector3(Mathf.Repeat(ex * Mathf.Rad2Deg, 360f), Mathf.Repeat(ey * Mathf.Rad2Deg, 360f), Mathf.Repeat(ez * Mathf.Rad2Deg, 360f));
            }
            set => this = Euler(value);
        }

        public static Quaternion AngleAxis(float angle, Vector3 axis)
        {
            axis = axis.normalized;
            float h = angle * Mathf.Deg2Rad * 0.5f, s = MathF.Sin(h);
            return new Quaternion(axis.x * s, axis.y * s, axis.z * s, MathF.Cos(h));
        }

        public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.up);
        public static Quaternion LookRotation(Vector3 forward, Vector3 upwards)
        {
            Vector3 f = forward.normalized;
            if (f.sqrMagnitude < 1e-10f) return identity;
            Vector3 r = Vector3.Cross(upwards, f);
            if (r.sqrMagnitude < 1e-10f) r = Vector3.Cross(MathF.Abs(f.y) < 0.99f ? Vector3.up : Vector3.forward, f);
            r = r.normalized;
            Vector3 u = Vector3.Cross(f, r);
            return FromBasis(r, u, f);
        }

        // 열 = 오른쪽, 위, 앞 인 회전 행렬 → 쿼터니언
        static Quaternion FromBasis(Vector3 r, Vector3 u, Vector3 f)
        {
            float m00 = r.x, m01 = u.x, m02 = f.x;
            float m10 = r.y, m11 = u.y, m12 = f.y;
            float m20 = r.z, m21 = u.z, m22 = f.z;
            float trace = m00 + m11 + m22;
            Quaternion q;
            if (trace > 0)
            {
                float s = MathF.Sqrt(trace + 1f) * 2f;
                q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s = MathF.Sqrt(1f + m00 - m11 - m22) * 2f;
                q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
            }
            else if (m11 > m22)
            {
                float s = MathF.Sqrt(1f + m11 - m00 - m22) * 2f;
                q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
            }
            else
            {
                float s = MathF.Sqrt(1f + m22 - m00 - m11) * 2f;
                q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
            }
            return q.normalized;
        }

        public static Quaternion FromToRotation(Vector3 from, Vector3 to)
        {
            Vector3 a = from.normalized, b = to.normalized;
            float d = Vector3.Dot(a, b);
            if (d > 0.999999f) return identity;
            if (d < -0.999999f)
            {
                Vector3 axis = Vector3.Cross(Vector3.right, a);
                if (axis.sqrMagnitude < 1e-6f) axis = Vector3.Cross(Vector3.up, a);
                return AngleAxis(180f, axis);
            }
            Vector3 c = Vector3.Cross(a, b);
            return new Quaternion(c.x, c.y, c.z, 1f + d).normalized;
        }

        public static Quaternion Inverse(Quaternion q)
        {
            float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            return n < 1e-12f ? identity : new Quaternion(-q.x / n, -q.y / n, -q.z / n, q.w / n);
        }
        public static float Dot(Quaternion a, Quaternion b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        public static float Angle(Quaternion a, Quaternion b)
        {
            float d = MathF.Min(MathF.Abs(Dot(a, b)), 1f);
            return d > 0.999999f ? 0f : MathF.Acos(d) * 2f * Mathf.Rad2Deg;
        }
        public static Quaternion Slerp(Quaternion a, Quaternion b, float t) => SlerpUnclamped(a, b, Mathf.Clamp01(t));
        public static Quaternion SlerpUnclamped(Quaternion a, Quaternion b, float t)
        {
            float d = Dot(a, b);
            if (d < 0) { b = new Quaternion(-b.x, -b.y, -b.z, -b.w); d = -d; }
            if (d > 0.9995f) return LerpUnclamped(a, b, t);
            float theta = MathF.Acos(d), s = MathF.Sin(theta);
            float wa = MathF.Sin((1 - t) * theta) / s, wb = MathF.Sin(t * theta) / s;
            return new Quaternion(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
        }
        public static Quaternion Lerp(Quaternion a, Quaternion b, float t) => LerpUnclamped(a, b, Mathf.Clamp01(t));
        public static Quaternion LerpUnclamped(Quaternion a, Quaternion b, float t)
        {
            if (Dot(a, b) < 0) b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
            return new Quaternion(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t).normalized;
        }
        public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
        {
            float angle = Angle(from, to);
            return angle == 0f ? to : SlerpUnclamped(from, to, MathF.Min(1f, maxDegreesDelta / angle));
        }
        public Quaternion normalized
        {
            get
            {
                float n = MathF.Sqrt(x * x + y * y + z * z + w * w);
                return n < 1e-12f ? identity : new Quaternion(x / n, y / n, z / n, w / n);
            }
        }
        public void Normalize() { this = normalized; }

        // Unity 와 같이 a * b = b 를 먼저 적용한 뒤 a
        public static Quaternion operator *(Quaternion a, Quaternion b) => new Quaternion(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
            a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
        public static Vector3 operator *(Quaternion q, Vector3 v)
        {
            Vector3 u = new Vector3(q.x, q.y, q.z);
            Vector3 t = 2f * Vector3.Cross(u, v);
            return v + q.w * t + Vector3.Cross(u, t);
        }
        public static bool operator ==(Quaternion a, Quaternion b) => Dot(a, b) > 0.999999f;
        public static bool operator !=(Quaternion a, Quaternion b) => !(a == b);

        public bool Equals(Quaternion o) => x == o.x && y == o.y && z == o.z && w == o.w;
        public override bool Equals(object o) => o is Quaternion q && Equals(q);
        public override int GetHashCode() => HashCode.Combine(x, y, z, w);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F5}, {1:F5}, {2:F5}, {3:F5})", x, y, z, w);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Color : IEquatable<Color>
    {
        public float r, g, b, a;
        public Color(float r, float g, float b, float a = 1f) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public static Color red => new Color(1, 0, 0);
        public static Color green => new Color(0, 1, 0);
        public static Color blue => new Color(0, 0, 1);
        public static Color white => new Color(1, 1, 1);
        public static Color black => new Color(0, 0, 0);
        public static Color yellow => new Color(1, 0.92156863f, 0.015686275f);
        public static Color cyan => new Color(0, 1, 1);
        public static Color magenta => new Color(1, 0, 1);
        public static Color gray => new Color(0.5f, 0.5f, 0.5f);
        public static Color grey => gray;
        public static Color clear => new Color(0, 0, 0, 0);
        public static Color Lerp(Color x, Color y, float t) { t = Mathf.Clamp01(t); return new Color(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t); }
        public static Color operator *(Color c, float d) => new Color(c.r * d, c.g * d, c.b * d, c.a * d);
        public static Color operator *(Color x, Color y) => new Color(x.r * y.r, x.g * y.g, x.b * y.b, x.a * y.a);
        public static Color operator +(Color x, Color y) => new Color(x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a);
        public bool Equals(Color o) => r == o.r && g == o.g && b == o.b && a == o.a;
        public override bool Equals(object o) => o is Color c && Equals(c);
        public override int GetHashCode() => HashCode.Combine(r, g, b, a);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "RGBA({0:F3}, {1:F3}, {2:F3}, {3:F3})", r, g, b, a);
    }

    public static class Mathf
    {
        public const float PI = MathF.PI;
        public const float Deg2Rad = MathF.PI / 180f;
        public const float Rad2Deg = 180f / MathF.PI;
        public const float Infinity = float.PositiveInfinity;
        public const float NegativeInfinity = float.NegativeInfinity;
        public static readonly float Epsilon = float.Epsilon;

        public static float Abs(float f) => MathF.Abs(f);
        public static int Abs(int v) => Math.Abs(v);
        public static float Min(float a, float b) => a < b ? a : b;
        public static float Min(params float[] v) { float m = v[0]; foreach (var f in v) if (f < m) m = f; return m; }
        public static int Min(int a, int b) => a < b ? a : b;
        public static float Max(float a, float b) => a > b ? a : b;
        public static float Max(params float[] v) { float m = v[0]; foreach (var f in v) if (f > m) m = f; return m; }
        public static int Max(int a, int b) => a > b ? a : b;
        public static float Clamp(float v, float min, float max) => v < min ? min : (v > max ? max : v);
        public static int Clamp(int v, int min, int max) => v < min ? min : (v > max ? max : v);
        public static float Clamp01(float v) => v < 0 ? 0 : (v > 1 ? 1 : v);
        public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
        public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;
        public static float InverseLerp(float a, float b, float v) => a != b ? Clamp01((v - a) / (b - a)) : 0f;
        public static float MoveTowards(float current, float target, float maxDelta) => MathF.Abs(target - current) <= maxDelta ? target : current + MathF.Sign(target - current) * maxDelta;
        public static float SmoothStep(float from, float to, float t) { t = Clamp01(t); t = -2f * t * t * t + 3f * t * t; return to * t + from * (1f - t); }
        public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed = Infinity, float deltaTime = -1)
        {
            if (deltaTime < 0) deltaTime = Time.deltaTime;
            smoothTime = Max(0.0001f, smoothTime);
            float omega = 2f / smoothTime, x = omega * deltaTime;
            float exp = 1f / (1f + x + 0.48f * x * x + 0.235f * x * x * x);
            float change = current - target, originalTo = target;
            float maxChange = maxSpeed * smoothTime;
            change = Clamp(change, -maxChange, maxChange);
            target = current - change;
            float temp = (currentVelocity + omega * change) * deltaTime;
            currentVelocity = (currentVelocity - omega * temp) * exp;
            float output = target + (change + temp) * exp;
            if (originalTo - current > 0f == output > originalTo) { output = originalTo; currentVelocity = (output - originalTo) / deltaTime; }
            return output;
        }
        public static float Sin(float f) => MathF.Sin(f);
        public static float Cos(float f) => MathF.Cos(f);
        public static float Tan(float f) => MathF.Tan(f);
        public static float Asin(float f) => MathF.Asin(f);
        public static float Acos(float f) => MathF.Acos(f);
        public static float Atan(float f) => MathF.Atan(f);
        public static float Atan2(float y, float x) => MathF.Atan2(y, x);
        public static float Sqrt(float f) => MathF.Sqrt(f);
        public static float Pow(float f, float p) => MathF.Pow(f, p);
        public static float Exp(float p) => MathF.Exp(p);
        public static float Log(float f) => MathF.Log(f);
        public static float Log(float f, float p) => MathF.Log(f, p);
        public static float Log10(float f) => MathF.Log10(f);
        public static float Floor(float f) => MathF.Floor(f);
        public static float Ceil(float f) => MathF.Ceiling(f);
        public static float Round(float f) => MathF.Round(f, MidpointRounding.ToEven);
        public static int FloorToInt(float f) => (int)MathF.Floor(f);
        public static int CeilToInt(float f) => (int)MathF.Ceiling(f);
        public static int RoundToInt(float f) => (int)MathF.Round(f, MidpointRounding.ToEven);
        public static float Sign(float f) => f >= 0f ? 1f : -1f;
        public static float Repeat(float t, float length) => Clamp(t - MathF.Floor(t / length) * length, 0f, length);
        public static float PingPong(float t, float length) { t = Repeat(t, length * 2f); return length - MathF.Abs(t - length); }
        public static bool Approximately(float a, float b) => MathF.Abs(b - a) < MathF.Max(1E-06f * MathF.Max(MathF.Abs(a), MathF.Abs(b)), float.Epsilon * 8f);
        public static float DeltaAngle(float current, float target) { float d = Repeat(target - current, 360f); if (d > 180f) d -= 360f; return d; }
        /// <summary>각도(도)를 가장 짧은 쪽으로 부드럽게 (Unity Mathf.SmoothDampAngle)</summary>
        public static float SmoothDampAngle(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed = Infinity, float deltaTime = -1)
        {
            target = current + DeltaAngle(current, target);
            return SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, deltaTime);
        }
        public static float LerpAngle(float a, float b, float t) { float d = Repeat(b - a, 360f); if (d > 180f) d -= 360f; return a + d * Clamp01(t); }
        public static float MoveTowardsAngle(float current, float target, float maxDelta) { float d = DeltaAngle(current, target); if (-maxDelta < d && d < maxDelta) return target; return MoveTowards(current, current + d, maxDelta); }
        public static bool IsPowerOfTwo(int v) => (v & (v - 1)) == 0;
        public static int NextPowerOfTwo(int v) { v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; return v + 1; }
    }

    public static class Random
    {
        static System.Random s_Rng = new System.Random();
        public static void InitState(int seed) { s_Rng = new System.Random(seed); }
        public static float value => (float)s_Rng.NextDouble();
        /// <summary>[min, max] (float 은 max 포함)</summary>
        public static float Range(float min, float max) => min + (max - min) * (float)s_Rng.NextDouble();
        /// <summary>[min, max) (int 는 max 제외, Unity 와 같음)</summary>
        public static int Range(int min, int max) => max > min ? s_Rng.Next(min, max) : min;
        public static Vector3 insideUnitSphere { get { Vector3 v; do v = new Vector3(Range(-1f, 1f), Range(-1f, 1f), Range(-1f, 1f)); while (v.sqrMagnitude > 1f); return v; } }
        public static Vector3 onUnitSphere => insideUnitSphere.normalized is var n && n.sqrMagnitude > 0 ? n : Vector3.up;
        public static Vector2 insideUnitCircle { get { Vector2 v; do v = new Vector2(Range(-1f, 1f), Range(-1f, 1f)); while (v.sqrMagnitude > 1f); return v; } }
        public static Quaternion rotation => Quaternion.Euler(Range(0f, 360f), Range(0f, 360f), Range(0f, 360f));
        public static Color ColorHSV() => new Color(value, value, value);
    }
}
