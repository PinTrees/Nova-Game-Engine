using System;
using System.Collections.Generic;
using NovaEngine.Events;
using NovaEngine.Interop;

namespace NovaEngine
{
    // Unity 의 AsyncOperation: 씬 읽기 · 내리기 (SceneManager.LoadSceneAsync · UnloadSceneAsync). 코루틴에서 yield return 으로 기다린다
    public class AsyncOperation : YieldInstruction
    {
        internal readonly int m_Op;
        internal bool m_CompletedFired;
        Action<AsyncOperation> m_Completed;
        internal AsyncOperation(int op) { m_Op = op; }

        unsafe int State(out float progress)
        {
            float p = 0f; int h = 0;
            int s = Native.Api.Scene_OpState(m_Op, &p, &h);
            progress = s < 0 ? 1f : p;
            return s;
        }

        public bool isDone { get { int s = State(out _); return s < 0 || (s & 1) != 0; } }
        // 0 ~ 0.9 = 읽는 중, allowSceneActivation 이 false 면 0.9 에서 멈춘다, 1 = 끝 (Unity 와 같음)
        public float progress { get { State(out float p); return p; } }
        public bool allowSceneActivation
        {
            get { int s = State(out _); return s < 0 || (s & 4) != 0; }
            set => SetAllow(value);
        }
        unsafe void SetAllow(bool v) => Native.Api.Scene_OpAllow(m_Op, v ? 1 : 0);
        public int priority { get; set; }

        // 끝나면 한 번. 이미 끝난 뒤에 더하면 바로 부른다 (Unity 와 같음)
        public event Action<AsyncOperation> completed
        {
            add
            {
                if (m_CompletedFired) { value?.Invoke(this); return; }
                m_Completed += value;
            }
            remove => m_Completed -= value;
        }

        internal void FireCompleted()
        {
            if (m_CompletedFired) return;
            m_CompletedFired = true;
            var c = m_Completed;
            m_Completed = null;
            c?.Invoke(this);
        }
    }
}

namespace NovaEngine.SceneManagement
{
    // Unity 의 Scene: 실행 중 읽은 씬 하나 (핸들). 이름 · 경로 · 빌드 번호 · 읽혀 있는지 · 루트 오브젝트
    public struct Scene : IEquatable<Scene>
    {
        internal int m_Handle;     // 0 = Play 중이 아닐 때 (에디터의 nova exec 등 — 지금 열린 씬)
        internal string m_Path;

        internal static Scene FromHandle(int handle) => new Scene { m_Handle = handle };

        public int handle => m_Handle;
        public unsafe string path
        {
            get
            {
                if (m_Handle == 0 || m_Handle == SceneManager.DontDestroyOnLoadHandle) return m_Handle == 0 ? m_Path ?? "" : "";
                int loaded, roots;
                return Native.Str(Native.Api.Scene_HandleInfo(m_Handle, &loaded, &roots)) ?? "";
            }
        }
        public string name
        {
            get
            {
                if (m_Handle == SceneManager.DontDestroyOnLoadHandle) return "DontDestroyOnLoad";
                string p = path;
                return string.IsNullOrEmpty(p) ? "" : System.IO.Path.GetFileNameWithoutExtension(p);
            }
        }
        public int buildIndex => m_Handle == SceneManager.DontDestroyOnLoadHandle ? -1 : SceneUtility.GetBuildIndexByScenePath(path);
        public unsafe bool isLoaded
        {
            get
            {
                if (m_Handle == 0) return IsValid();
                int loaded, roots;
                Native.Api.Scene_HandleInfo(m_Handle, &loaded, &roots);
                return loaded != 0;
            }
        }
        public bool isDirty => false;
        public bool isSubScene { get => false; set { } }
        public unsafe int rootCount
        {
            get
            {
                if (m_Handle == 0) return 0;
                int loaded, roots;
                Native.Api.Scene_HandleInfo(m_Handle, &loaded, &roots);
                return roots;
            }
        }
        public bool IsValid() => m_Handle != 0 || !string.IsNullOrEmpty(m_Path);

        public GameObject[] GetRootGameObjects()
        {
            var list = new List<GameObject>();
            GetRootGameObjects(list);
            return list.ToArray();
        }

        public unsafe void GetRootGameObjects(List<GameObject> rootGameObjects)
        {
            if (rootGameObjects == null) throw new ArgumentNullException(nameof(rootGameObjects));
            rootGameObjects.Clear();
            if (m_Handle == 0) return;
            int n = Native.Api.Scene_Roots(m_Handle, null, 0);
            if (n <= 0) return;
            var ids = new ulong[n];
            fixed (ulong* p = ids) n = Math.Min(n, Native.Api.Scene_Roots(m_Handle, p, n));
            for (int i = 0; i < n; i++) rootGameObjects.Add(SceneManager.Wrap(ids[i]));
        }

        public bool Equals(Scene other) => m_Handle != 0 || other.m_Handle != 0 ? m_Handle == other.m_Handle : m_Path == other.m_Path;
        public override bool Equals(object obj) => obj is Scene s && Equals(s);
        public override int GetHashCode() => m_Handle != 0 ? m_Handle : (m_Path ?? "").GetHashCode();
        public static bool operator ==(Scene a, Scene b) => a.Equals(b);
        public static bool operator !=(Scene a, Scene b) => !a.Equals(b);
        public override string ToString() => name;
    }

    public enum LoadSceneMode { Single = 0, Additive = 1 }
    public enum UnloadSceneOptions { None = 0, UnloadAllEmbeddedSceneObjects = 1 }

    public struct LoadSceneParameters
    {
        public LoadSceneMode loadSceneMode;
        public LoadSceneParameters(LoadSceneMode mode) { loadSceneMode = mode; }
    }

    // Unity 의 SceneManager: Build Settings 에 넣은 씬을 이름 · 경로 · 빌드 번호로 읽는다.
    //  LoadScene = 이번 프레임이 끝날 때, LoadSceneAsync = 작업 스레드에서 읽고 프레임 끝에 바꾼다 (allowSceneActivation),
    //  Additive = 지금 씬에 더한다 (UnloadSceneAsync 로 내린다), DontDestroyOnLoad 오브젝트는 Single 로 바꿔도 남는다
    public static class SceneManager
    {
        internal const int DontDestroyOnLoadHandle = -1;
        static readonly Dictionary<int, AsyncOperation> s_Ops = new Dictionary<int, AsyncOperation>();

        public static event UnityAction<Scene, LoadSceneMode> sceneLoaded;
        public static event UnityAction<Scene> sceneUnloaded;
        public static event UnityAction<Scene, Scene> activeSceneChanged;

        internal static GameObject Wrap(ulong id) => id == 0 ? null : new GameObject(id);

        public static unsafe int sceneCountInBuildSettings => Native.Api.Scene_Count();
        public static unsafe int sceneCount { get { int n = Native.Api.Scene_LoadedCount(); return n > 0 ? n : 1; } }
        public static int loadedSceneCount => sceneCount;

        public static unsafe Scene GetActiveScene()
        {
            int h = Native.Api.Scene_ActiveHandle();
            if (h != 0) return Scene.FromHandle(h);
            int index;
            return new Scene { m_Path = Native.Str(Native.Api.Scene_Active(&index)) };   // Play 중이 아닐 때 (에디터)
        }

        public static unsafe bool SetActiveScene(Scene scene)
        {
            if (!scene.isLoaded) throw new ArgumentException("SceneManager.SetActiveScene failed; scene '" + scene.name + "' is not loaded and therefore cannot be set active");
            return scene.m_Handle != 0 && Native.Api.Scene_SetActive(scene.m_Handle) != 0;
        }

        public static unsafe Scene GetSceneAt(int index)
        {
            int h = Native.Api.Scene_HandleAt(index);
            if (h == 0)
            {
                if (index == 0) return GetActiveScene();
                throw new ArgumentException("Index out of range");
            }
            return Scene.FromHandle(h);
        }

        public static Scene GetSceneByName(string name)
        {
            for (int i = 0; i < sceneCount; i++)
            {
                Scene s = GetSceneAt(i);
                if (string.Equals(s.name, name, StringComparison.OrdinalIgnoreCase) || SamePath(s.path, name)) return s;
            }
            return default;
        }

        public static Scene GetSceneByPath(string scenePath)
        {
            for (int i = 0; i < sceneCount; i++)
            {
                Scene s = GetSceneAt(i);
                if (SamePath(s.path, scenePath)) return s;
            }
            return default;
        }

        public static Scene GetSceneByBuildIndex(int buildIndex)
        {
            for (int i = 0; i < sceneCount; i++)
            {
                Scene s = GetSceneAt(i);
                if (s.buildIndex == buildIndex) return s;
            }
            return default;
        }

        static bool SamePath(string a, string b) =>
            string.Equals((a ?? "").Replace('/', '\\'), (b ?? "").Replace('/', '\\'), StringComparison.OrdinalIgnoreCase);

        // ---- 읽기
        public static void LoadScene(string sceneName, LoadSceneMode mode = LoadSceneMode.Single) => Load(sceneName, -1, mode, false);
        public static void LoadScene(int sceneBuildIndex, LoadSceneMode mode = LoadSceneMode.Single) => Load(null, sceneBuildIndex, mode, false);
        public static Scene LoadScene(string sceneName, LoadSceneParameters parameters) { Load(sceneName, -1, parameters.loadSceneMode, false); return default; }
        public static Scene LoadScene(int sceneBuildIndex, LoadSceneParameters parameters) { Load(null, sceneBuildIndex, parameters.loadSceneMode, false); return default; }

        public static AsyncOperation LoadSceneAsync(string sceneName, LoadSceneMode mode = LoadSceneMode.Single) => Load(sceneName, -1, mode, true);
        public static AsyncOperation LoadSceneAsync(int sceneBuildIndex, LoadSceneMode mode = LoadSceneMode.Single) => Load(null, sceneBuildIndex, mode, true);
        public static AsyncOperation LoadSceneAsync(string sceneName, LoadSceneParameters parameters) => Load(sceneName, -1, parameters.loadSceneMode, true);
        public static AsyncOperation LoadSceneAsync(int sceneBuildIndex, LoadSceneParameters parameters) => Load(null, sceneBuildIndex, parameters.loadSceneMode, true);

        static unsafe AsyncOperation Load(string name, int index, LoadSceneMode mode, bool async)
        {
            int op;
            if (name != null) { fixed (byte* p = Native.Utf8(name)) op = Native.Api.Scene_LoadOp(p, -1, (int)mode, async ? 1 : 0); }
            else op = Native.Api.Scene_LoadOp(null, index, (int)mode, async ? 1 : 0);
            if (op == 0)
            {
                if (name != null)
                    Debug.LogError($"Scene '{name}' couldn't be loaded because it has not been added to the build settings or the AssetBundle has not been loaded.\nTo add a scene to the build settings use the menu File->Build Settings...");
                else
                    Debug.LogError($"Cannot load scene: Invalid scene build index ({index}). Make sure the scene has been added to the build settings (File > Build Settings).");
                return null;
            }
            if (op < 0) return null;   // Play 중이 아님 (에디터)
            var o = new AsyncOperation(op);
            s_Ops[op] = o;
            return o;
        }

        // ---- 내리기 (Additive 로 더한 씬 · 마지막으로 남은 씬은 내릴 수 없다)
        public static AsyncOperation UnloadSceneAsync(Scene scene) => Unload(scene);
        public static AsyncOperation UnloadSceneAsync(Scene scene, UnloadSceneOptions options) => Unload(scene);
        public static AsyncOperation UnloadSceneAsync(string sceneName) => Unload(GetSceneByName(sceneName));
        public static AsyncOperation UnloadSceneAsync(int sceneBuildIndex) => Unload(GetSceneByBuildIndex(sceneBuildIndex));
        [Obsolete("Use SceneManager.UnloadSceneAsync. This function is not safe to use during triggers and under other circumstances. See Scripting reference for more details.")]
        public static bool UnloadScene(Scene scene) => Unload(scene) != null;
        [Obsolete("Use SceneManager.UnloadSceneAsync.")]
        public static bool UnloadScene(string sceneName) => Unload(GetSceneByName(sceneName)) != null;

        static unsafe AsyncOperation Unload(Scene scene)
        {
            if (!scene.IsValid() || scene.m_Handle == 0)
                throw new ArgumentException("Scene to unload is invalid");
            int op = Native.Api.Scene_Unload(scene.m_Handle);
            if (op == 0)
            {
                Debug.LogWarning("Unloading the last loaded scene " + scene.path + "(build index: " + scene.buildIndex + "), is not supported. Please use SceneManager.LoadScene()/EditorSceneManager.OpenScene() to switch to another scene.");
                return null;
            }
            var o = new AsyncOperation(op);
            s_Ops[op] = o;
            return o;
        }

        public static unsafe void MoveGameObjectToScene(GameObject go, Scene scene)
        {
            if (go == null) throw new ArgumentException("Object to move is null");
            if (!scene.IsValid() || scene.m_Handle == 0) throw new ArgumentException("Scene is invalid");
            if (Native.Api.GO_MoveToScene(go.m_Id, scene.m_Handle) == 0)
                throw new ArgumentException("Gameobject is not a root in a scene");
        }

        public static Scene CreateScene(string sceneName) => throw new NotSupportedException("SceneManager.CreateScene is not supported yet");
        public static void MergeScenes(Scene sourceScene, Scene destinationScene)
        {
            foreach (GameObject go in sourceScene.GetRootGameObjects())
                MoveGameObjectToScene(go, destinationScene);
            UnloadSceneAsync(sourceScene);
        }

        // 네이티브 알림 (AppEvents.SceneEvent)
        internal static void OnNativeEvent(int kind, int a, int b)
        {
            switch (kind)
            {
                case 0: sceneLoaded?.Invoke(Scene.FromHandle(a), (LoadSceneMode)b); break;
                case 1: sceneUnloaded?.Invoke(Scene.FromHandle(a)); break;
                case 2: activeSceneChanged?.Invoke(Scene.FromHandle(a), Scene.FromHandle(b)); break;
                case 3:
                    if (s_Ops.TryGetValue(a, out AsyncOperation op))
                    {
                        s_Ops.Remove(a);
                        op.FireCompleted();
                    }
                    break;
                case 4:   // Play 시작: 지난 Play 의 구독 · 작업을 버린다 (Unity 는 도메인을 다시 읽어 정적 상태가 비어 있다)
                    sceneLoaded = null;
                    sceneUnloaded = null;
                    activeSceneChanged = null;
                    s_Ops.Clear();
                    break;
            }
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
