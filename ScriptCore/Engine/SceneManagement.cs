using System;
using NovaEngine.Interop;

namespace NovaEngine.SceneManagement
{
    // Unity 의 Scene (이름, 경로, 빌드 인덱스)
    public struct Scene
    {
        internal string m_Path;
        internal int m_BuildIndex;
        public string path => m_Path ?? "";
        public string name => string.IsNullOrEmpty(m_Path) ? "" : System.IO.Path.GetFileNameWithoutExtension(m_Path);
        public int buildIndex => m_BuildIndex;
        public bool isLoaded => IsValid();
        public bool IsValid() => !string.IsNullOrEmpty(m_Path);
        public override string ToString() => name;
    }

    public enum LoadSceneMode { Single = 0, Additive = 1 }

    // Unity 의 SceneManager: Build Settings 에 넣은 씬을 이름/인덱스로 읽는다 (이번 프레임이 끝난 뒤 바뀜)
    public static class SceneManager
    {
        public static unsafe int sceneCountInBuildSettings => Native.Api.Scene_Count();
        public static int sceneCount => 1;

        public static unsafe Scene GetActiveScene()
        {
            int index;
            string p = Native.Str(Native.Api.Scene_Active(&index));
            return new Scene { m_Path = p, m_BuildIndex = index };
        }

        public static unsafe void LoadScene(string sceneName, LoadSceneMode mode = LoadSceneMode.Single)
        {
            if (mode == LoadSceneMode.Additive) Debug.LogWarning("LoadSceneMode.Additive is not supported yet; loading as Single.");
            int ok;
            fixed (byte* p = Native.Utf8(sceneName)) ok = Native.Api.Scene_Load(p, -1);
            if (ok == 0)
                Debug.LogError($"Scene '{sceneName}' couldn't be loaded because it has not been added to the build settings or the AssetBundle has not been loaded.\nTo add a scene to the build settings use the menu File->Build Settings...");
        }

        public static unsafe void LoadScene(int sceneBuildIndex, LoadSceneMode mode = LoadSceneMode.Single)
        {
            if (mode == LoadSceneMode.Additive) Debug.LogWarning("LoadSceneMode.Additive is not supported yet; loading as Single.");
            if (Native.Api.Scene_Load(null, sceneBuildIndex) == 0)
                Debug.LogError($"Cannot load scene: Invalid scene build index ({sceneBuildIndex}). Make sure the scene has been added to the build settings (File > Build Settings).");
        }
    }

    public static class SceneUtility
    {
        public static unsafe string GetScenePathByBuildIndex(int buildIndex) => Native.Str(Native.Api.Scene_PathAt(buildIndex)) ?? "";
        public static int GetBuildIndexByScenePath(string scenePath)
        {
            for (int i = 0; i < SceneManager.sceneCountInBuildSettings; i++)
                if (string.Equals(GetScenePathByBuildIndex(i).Replace('/', '\\'), (scenePath ?? "").Replace('/', '\\'), StringComparison.OrdinalIgnoreCase)) return i;
            return -1;
        }
    }
}
