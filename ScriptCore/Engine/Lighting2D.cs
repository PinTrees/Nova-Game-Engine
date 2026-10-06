using NovaEngine.Interop;

namespace NovaEngine.Rendering.Universal
{
    // Unity (URP 2D) 의 Light 2D: 스프라이트를 비추는 2D 빛 (Global · Spot). 씬에 하나라도 있으면 스프라이트가 빛을 받는다
    [NativeComponent("Light2D")]
    public sealed unsafe class Light2D : Behaviour
    {
        // Unity 와 같은 값 (Freeform · Sprite 모양은 아직 없다 — Spot 으로)
        public enum LightType { Parametric = 0, Freeform = 1, Sprite = 2, Point = 3, Global = 4 }

        internal Light2D() { }
        float F(int p) => Native.Api.L2D_GetFloat(m_Id, p);
        void S(int p, float v) => Native.Api.L2D_SetFloat(m_Id, p, v);

        public LightType lightType { get => F(0) < 0.5f ? LightType.Global : LightType.Point; set => S(0, value == LightType.Global ? 0f : 1f); }
        public Color color
        {
            get { Vector4 v; Native.Api.L2D_GetColor(m_Id, &v); return new Color(v.x, v.y, v.z, 1f); }
            set { Vector4 v = new Vector4(value.r, value.g, value.b, 1f); Native.Api.L2D_SetColor(m_Id, &v); }
        }
        public float intensity { get => F(1); set => S(1, value); }
        public float pointLightInnerRadius { get => F(2); set => S(2, value); }
        public float pointLightOuterRadius { get => F(3); set => S(3, value); }
        public float pointLightInnerAngle { get => F(4); set => S(4, value); }
        public float pointLightOuterAngle { get => F(5); set => S(5, value); }
        public float falloffIntensity { get => F(6); set => S(6, value); }
        public bool shadowsEnabled { get => F(7) > 0.5f; set => S(7, value ? 1f : 0f); }
        public float shadowIntensity { get => F(8); set => S(8, value); }
        public float normalMapDistance { get => F(9); set => S(9, value); }
    }

    // Unity 의 Shadow Caster 2D: 2D 빛 (Shadows 켬) 의 그림자를 드리운다 — 모양 = 2D 콜라이더 윤곽 (없으면 스프라이트 사각형)
    [NativeComponent("ShadowCaster2D")]
    public sealed unsafe class ShadowCaster2D : Behaviour
    {
        internal ShadowCaster2D() { }
        public bool castsShadows { get => Native.Api.SC2D_Get(m_Id, 0) != 0; set => Native.Api.SC2D_Set(m_Id, 0, value ? 1 : 0); }
        public bool selfShadows { get => Native.Api.SC2D_Get(m_Id, 1) != 0; set => Native.Api.SC2D_Set(m_Id, 1, value ? 1 : 0); }
    }
}
