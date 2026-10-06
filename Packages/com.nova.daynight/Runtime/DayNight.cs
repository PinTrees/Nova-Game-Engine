using System.Runtime.InteropServices;

namespace NovaEngine
{
    // com.nova.daynight: 장면의 Day Night Cycle 을 스크립트에서 (C++ 컴포넌트 DayNightCycle, Plugins/NovaDayNight.dll)
    //  DayNight.timeOfDay = 18.5f;                 // 노을
    //  if (DayNight.phase == DayPhase.Night) ...   // 가로등 켜기
    internal static unsafe class DayNightNative
    {
        const string Dll = "NovaDayNight";
        [DllImport(Dll)] internal static extern int NovaDayNight_Exists();
        [DllImport(Dll)] internal static extern float NovaDayNight_GetFloat(int prop);
        [DllImport(Dll)] internal static extern void NovaDayNight_SetFloat(int prop, float v);
        [DllImport(Dll)] internal static extern void NovaDayNight_GetSunDirection(Vector3* outDir);
    }

    /// <summary>하루의 단계 (새벽 4:30 · 아침 6:30 · 낮 10:00 · 저녁 16:00 · 노을 18:00 · 밤 19:30 · 은하수 23:00 부터)</summary>
    public enum DayPhase { Dawn = 0, Morning = 1, Day = 2, Evening = 3, Sunset = 4, Night = 5, MilkyWay = 6 }

    [NativeComponent("DayNightCycle")]
    public sealed class DayNightCycle : Component
    {
        internal DayNightCycle() { }
    }

    public static unsafe class DayNight
    {
        /// <summary>장면에 켜진 Day Night Cycle 이 있다</summary>
        public static bool exists => DayNightNative.NovaDayNight_Exists() != 0;

        /// <summary>시각 (0 ~ 24 시)</summary>
        public static float timeOfDay { get => DayNightNative.NovaDayNight_GetFloat(0); set => DayNightNative.NovaDayNight_SetFloat(0, value); }

        /// <summary>실제 몇 분에 하루가 지나는가 (0 = 멈춤)</summary>
        public static float dayLengthMinutes { get => DayNightNative.NovaDayNight_GetFloat(1); set => DayNightNative.NovaDayNight_SetFloat(1, value); }

        public static bool paused { get => DayNightNative.NovaDayNight_GetFloat(2) != 0; set => DayNightNative.NovaDayNight_SetFloat(2, value ? 1f : 0f); }

        /// <summary>지금 단계</summary>
        public static DayPhase phase => (DayPhase)(int)DayNightNative.NovaDayNight_GetFloat(3);

        /// <summary>해 높이 (도, 밤 = 음수)</summary>
        public static float sunElevation => DayNightNative.NovaDayNight_GetFloat(4);

        /// <summary>해가 있는 쪽 (단위 벡터)</summary>
        public static Vector3 sunDirection { get { Vector3 v; DayNightNative.NovaDayNight_GetSunDirection(&v); return v; } }

        public static bool isNight => phase == DayPhase.Night || phase == DayPhase.MilkyWay;

        public static float sunAzimuth { get => DayNightNative.NovaDayNight_GetFloat(5); set => DayNightNative.NovaDayNight_SetFloat(5, value); }
        public static float starBrightness { get => DayNightNative.NovaDayNight_GetFloat(7); set => DayNightNative.NovaDayNight_SetFloat(7, value); }
        public static float milkyWayBrightness { get => DayNightNative.NovaDayNight_GetFloat(8); set => DayNightNative.NovaDayNight_SetFloat(8, value); }

        /// <summary>단계의 가운데 시각으로</summary>
        public static void SetPhase(DayPhase p)
        {
            float[] starts = { 4.5f, 6.5f, 10f, 16f, 18f, 19.5f, 23f };
            int i = (int)p;
            float a = starts[i], b = starts[(i + 1) % 7];
            if (b <= a) b += 24f;
            timeOfDay = ((a + b) * 0.5f) % 24f;
        }
    }
}
