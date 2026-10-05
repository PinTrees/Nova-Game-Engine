using System.Runtime.InteropServices;

namespace NovaEngine
{
    // com.nova.weather: 장면의 Weather Controller 를 스크립트에서 (C++ 컴포넌트 WeatherController, Plugins/NovaWeather.dll)
    //  Weather.Set("Storm", 10f);  // 10 초에 걸쳐 폭풍으로
    //  if (Weather.rain > 0.5f) ...  // 지금 (전환 중이면 섞인) 값
    internal static unsafe class WeatherNative
    {
        const string Dll = "NovaWeather";
        [DllImport(Dll)] internal static extern int NovaWeather_Exists();
        [DllImport(Dll)] internal static extern int NovaWeather_Set(byte* profile, float seconds);
        [DllImport(Dll)] internal static extern byte* NovaWeather_GetProfile();
        [DllImport(Dll)] internal static extern float NovaWeather_GetFloat(int prop);
        [DllImport(Dll)] internal static extern void NovaWeather_Strike();
    }

    [NativeComponent("WeatherController")]
    public sealed class WeatherController : Component
    {
        internal WeatherController() { }
    }

    public static unsafe class Weather
    {
        /// <summary>장면에 Weather Controller 가 있다</summary>
        public static bool exists => WeatherNative.NovaWeather_Exists() != 0;

        /// <summary>프로필로 넘어간다: Clear, Cloudy, Rain, Storm, Snow, Blizzard 또는 .weather 경로 (Assets/...).
        /// seconds &lt; 0 = Controller 의 Transition Time, 0 = 바로. 없는 프로필이면 false</summary>
        public static bool Set(string profile, float seconds = -1f)
        {
            byte[] b = System.Text.Encoding.UTF8.GetBytes((profile ?? string.Empty) + "\0");
            fixed (byte* p = b) return WeatherNative.NovaWeather_Set(p, seconds) != 0;
        }

        /// <summary>지금 향하는 프로필 이름 (또는 경로)</summary>
        public static string profile
        {
            get
            {
                byte* p = WeatherNative.NovaWeather_GetProfile();
                return p == null ? string.Empty : Marshal.PtrToStringUTF8((System.IntPtr)p);
            }
        }

        /// <summary>번개 하나 (연출)</summary>
        public static void Strike() => WeatherNative.NovaWeather_Strike();

        public static float rain => WeatherNative.NovaWeather_GetFloat(0);
        public static float snow => WeatherNative.NovaWeather_GetFloat(1);
        /// <summary>m/s</summary>
        public static float wind => WeatherNative.NovaWeather_GetFloat(2);
        /// <summary>도 (바람이 불어 가는 쪽, +Z 기준 시계 방향)</summary>
        public static float windDirection => WeatherNative.NovaWeather_GetFloat(3);
        public static float gust => WeatherNative.NovaWeather_GetFloat(4);
        public static float clouds => WeatherNative.NovaWeather_GetFloat(5);
        public static float fog => WeatherNative.NovaWeather_GetFloat(6);
        public static float fogDistance => WeatherNative.NovaWeather_GetFloat(7);
        /// <summary>분당 번개 수</summary>
        public static float lightning => WeatherNative.NovaWeather_GetFloat(8);
        public static float wetness => WeatherNative.NovaWeather_GetFloat(9);
        public static float snowCover => WeatherNative.NovaWeather_GetFloat(10);
        /// <summary>전환 진행 (0 → 1, 1 = 끝)</summary>
        public static float transitionProgress => WeatherNative.NovaWeather_GetFloat(11);
    }
}
