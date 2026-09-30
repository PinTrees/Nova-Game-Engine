using System;
using NovaEngine.Interop;

namespace NovaEngine
{
    public enum ParticleSystemCurveMode { Constant = 0, Curve = 1, TwoCurves = 2, TwoConstants = 3 }
    public enum ParticleSystemGradientMode { Color = 0, Gradient = 1, TwoColors = 2, TwoGradients = 3, RandomColor = 4 }
    public enum ParticleSystemSimulationSpace { Local = 0, World = 1, Custom = 2 }
    public enum ParticleSystemStopBehavior { StopEmittingAndClear = 0, StopEmitting = 1 }
    // Unity 와 같은 번호 (NOVA 가 지원하는 도형만)
    public enum ParticleSystemShapeType { Sphere = 0, Hemisphere = 2, Cone = 4, Box = 5, Circle = 10, SingleSidedEdge = 12 }

    // Unity 의 ParticleSystem. 모듈(main, emission, shape, colorOverLifetime)은 구조체지만 값을 바꾸면 바로 시스템에 반영된다.
    //   var main = ps.main; main.startColor = Color.red;   ps.Play();   ps.Emit(20);
    public sealed class ParticleSystem : Component
    {
        internal ParticleSystem() { }

        // ---- MinMaxCurve / MinMaxGradient (스크립트에서는 상수 / 두 상수, 색 / 두 색)
        public struct MinMaxCurve
        {
            public ParticleSystemCurveMode mode;
            public float constantMin;
            public float constantMax;
            public float curveMultiplier;
            public MinMaxCurve(float constant) { mode = ParticleSystemCurveMode.Constant; constantMin = 0f; constantMax = constant; curveMultiplier = 1f; }
            public MinMaxCurve(float min, float max) { mode = ParticleSystemCurveMode.TwoConstants; constantMin = min; constantMax = max; curveMultiplier = 1f; }
            public float constant { get => constantMax; set { constantMax = value; mode = ParticleSystemCurveMode.Constant; } }
            public float Evaluate(float time, float lerpFactor = 0f) => mode == ParticleSystemCurveMode.TwoConstants ? constantMin + (constantMax - constantMin) * lerpFactor : constantMax;
            public static implicit operator MinMaxCurve(float constant) => new MinMaxCurve(constant);
            public override string ToString() => mode == ParticleSystemCurveMode.TwoConstants ? $"({constantMin}, {constantMax})" : constantMax.ToString();
        }

        public struct MinMaxGradient
        {
            public ParticleSystemGradientMode mode;
            public Color colorMin;
            public Color colorMax;
            public MinMaxGradient(Color color) { mode = ParticleSystemGradientMode.Color; colorMin = color; colorMax = color; }
            public MinMaxGradient(Color min, Color max) { mode = ParticleSystemGradientMode.TwoColors; colorMin = min; colorMax = max; }
            public Color color { get => colorMax; set { colorMax = value; mode = ParticleSystemGradientMode.Color; } }
            public static implicit operator MinMaxGradient(Color color) => new MinMaxGradient(color);
        }

        // ---- 네이티브 도우미
        internal unsafe float F(int prop) => Native.Api.PS_GetFloat(m_Id, prop);
        internal unsafe void F(int prop, float v) => Native.Api.PS_SetFloat(m_Id, prop, v);
        internal unsafe MinMaxCurve GetCurve(int prop)
        {
            Vector4 v;
            Native.Api.PS_GetCurve(m_Id, prop, &v);
            return new MinMaxCurve { mode = (ParticleSystemCurveMode)(int)v.x, constantMin = v.y, constantMax = v.z, curveMultiplier = v.w };
        }
        internal unsafe void SetCurve(int prop, MinMaxCurve c) => Native.Api.PS_SetCurve(m_Id, prop, (int)c.mode, c.constantMin, c.constantMax);
        internal unsafe MinMaxGradient GetColor(int prop)
        {
            Vector4 a, b;
            int mode = Native.Api.PS_GetColor(m_Id, prop, &a, &b);
            return new MinMaxGradient { mode = (ParticleSystemGradientMode)mode, colorMin = new Color(a.x, a.y, a.z, a.w), colorMax = new Color(b.x, b.y, b.z, b.w) };
        }
        internal unsafe void SetColor(int prop, MinMaxGradient g)
        {
            Vector4 a = new Vector4(g.colorMin.r, g.colorMin.g, g.colorMin.b, g.colorMin.a);
            Vector4 b = new Vector4(g.colorMax.r, g.colorMax.g, g.colorMax.b, g.colorMax.a);
            Native.Api.PS_SetColor(m_Id, prop, (int)g.mode, &a, &b);
        }

        // ---- 재생
        public unsafe void Play(bool withChildren = true) => Native.Api.PS_Call(m_Id, 0, withChildren ? 1 : 0);
        public unsafe void Stop(bool withChildren = true, ParticleSystemStopBehavior stopBehavior = ParticleSystemStopBehavior.StopEmitting)
            => Native.Api.PS_Call(m_Id, 1, (withChildren ? 1 : 0) | (stopBehavior == ParticleSystemStopBehavior.StopEmittingAndClear ? 2 : 0));
        public unsafe void Pause(bool withChildren = true) => Native.Api.PS_Call(m_Id, 2, withChildren ? 1 : 0);
        public unsafe void Clear(bool withChildren = true) => Native.Api.PS_Call(m_Id, 3, withChildren ? 1 : 0);
        public unsafe void Emit(int count) => Native.Api.PS_Call(m_Id, 4, count);
        public bool IsAlive(bool withChildren = true) => F(withChildren ? 17 : 20) != 0f;

        public bool isPlaying => F(5) != 0f;
        public bool isPaused => F(6) != 0f;
        public bool isStopped => F(7) != 0f;
        public bool isEmitting => F(8) != 0f;
        public int particleCount => (int)F(1);
        public float time { get => F(0); set => F(0, value); }

        public MainModule main => new MainModule(this);
        public EmissionModule emission => new EmissionModule(this);
        public ShapeModule shape => new ShapeModule(this);
        public ColorOverLifetimeModule colorOverLifetime => new ColorOverLifetimeModule(this);
        public TrailModule trails => new TrailModule(this);
        public CollisionModule collision => new CollisionModule(this);
        public SubEmittersModule subEmitters => new SubEmittersModule(this);

        // 켜기/끄기만 (나머지 값은 Inspector)
        public struct TrailModule { readonly ParticleSystem s; internal TrailModule(ParticleSystem p) { s = p; } public bool enabled { get => s.F(21) != 0f; set => s.F(21, value ? 1f : 0f); } }
        public struct CollisionModule { readonly ParticleSystem s; internal CollisionModule(ParticleSystem p) { s = p; } public bool enabled { get => s.F(22) != 0f; set => s.F(22, value ? 1f : 0f); } }
        public struct SubEmittersModule { readonly ParticleSystem s; internal SubEmittersModule(ParticleSystem p) { s = p; } public bool enabled { get => s.F(23) != 0f; set => s.F(23, value ? 1f : 0f); } }

        public struct MainModule
        {
            readonly ParticleSystem s;
            internal MainModule(ParticleSystem system) { s = system; }
            public float duration { get => s.F(2); set => s.F(2, value); }
            public bool loop { get => s.F(9) != 0f; set => s.F(9, value ? 1f : 0f); }
            public bool playOnAwake { get => s.F(10) != 0f; set => s.F(10, value ? 1f : 0f); }
            public float simulationSpeed { get => s.F(3); set => s.F(3, value); }
            public int maxParticles { get => (int)s.F(4); set => s.F(4, value); }
            public ParticleSystemSimulationSpace simulationSpace { get => (ParticleSystemSimulationSpace)(int)s.F(11); set => s.F(11, value == ParticleSystemSimulationSpace.World ? 1f : 0f); }
            public MinMaxCurve startDelay { get => s.GetCurve(0); set => s.SetCurve(0, value); }
            public MinMaxCurve startLifetime { get => s.GetCurve(1); set => s.SetCurve(1, value); }
            public MinMaxCurve startSpeed { get => s.GetCurve(2); set => s.SetCurve(2, value); }
            public MinMaxCurve startSize { get => s.GetCurve(3); set => s.SetCurve(3, value); }
            // Unity: 스크립트의 startRotation 은 라디안
            public MinMaxCurve startRotation
            {
                get { var c = s.GetCurve(4); c.constantMin *= Mathf.Deg2Rad; c.constantMax *= Mathf.Deg2Rad; return c; }
                set { value.constantMin *= Mathf.Rad2Deg; value.constantMax *= Mathf.Rad2Deg; s.SetCurve(4, value); }
            }
            public MinMaxCurve gravityModifier { get => s.GetCurve(5); set => s.SetCurve(5, value); }
            public MinMaxGradient startColor { get => s.GetColor(0); set => s.SetColor(0, value); }
        }

        public struct EmissionModule
        {
            readonly ParticleSystem s;
            internal EmissionModule(ParticleSystem system) { s = system; }
            public bool enabled { get => s.F(12) != 0f; set => s.F(12, value ? 1f : 0f); }
            public MinMaxCurve rateOverTime { get => s.GetCurve(6); set => s.SetCurve(6, value); }
            public MinMaxCurve rateOverDistance { get => s.GetCurve(7); set => s.SetCurve(7, value); }
        }

        public struct ShapeModule
        {
            readonly ParticleSystem s;
            internal ShapeModule(ParticleSystem system) { s = system; }
            static readonly ParticleSystemShapeType[] kTypes = { ParticleSystemShapeType.Sphere, ParticleSystemShapeType.Hemisphere, ParticleSystemShapeType.Cone, ParticleSystemShapeType.Box, ParticleSystemShapeType.Circle, ParticleSystemShapeType.SingleSidedEdge };
            public bool enabled { get => s.F(13) != 0f; set => s.F(13, value ? 1f : 0f); }
            public float radius { get => s.F(14); set => s.F(14, value); }
            public float angle { get => s.F(15); set => s.F(15, value); }
            public float arc { get => s.F(18); set => s.F(18, value); }
            public ParticleSystemShapeType shapeType
            {
                get => kTypes[Math.Clamp((int)s.F(16), 0, kTypes.Length - 1)];
                set { int i = Array.IndexOf(kTypes, value); if (i >= 0) s.F(16, i); }
            }
        }

        public struct ColorOverLifetimeModule
        {
            readonly ParticleSystem s;
            internal ColorOverLifetimeModule(ParticleSystem system) { s = system; }
            public bool enabled { get => s.F(19) != 0f; set => s.F(19, value ? 1f : 0f); }
            // 색 하나를 주면 그 색에서 투명으로 사라지는 그라디언트가 된다 (그라디언트 편집은 Inspector)
            public MinMaxGradient color { get => s.GetColor(1); set => s.SetColor(1, value); }
        }
    }
}
