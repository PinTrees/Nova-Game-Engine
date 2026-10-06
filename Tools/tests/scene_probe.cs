using System.Collections;
using NovaEngine;
using NovaEngine.SceneManagement;

// Scene probe (Tools/tests/run_tests.ps1 -Only scenes): additive · async · unload · active scene · DontDestroyOnLoad · PlayerPrefs
public class SceneProbe : MonoBehaviour
{
    int m_Awakes;
    int m_Starts;

    void Awake() { m_Awakes++; }

    IEnumerator Start()
    {
        m_Starts++;
        SceneManager.sceneLoaded += (s, m) => Debug.Log($"SceneProbe loaded {s.name} {m} roots={s.rootCount}");
        SceneManager.sceneUnloaded += s => Debug.Log($"SceneProbe unloaded {s.name}");
        SceneManager.activeSceneChanged += (a, b) => Debug.Log($"SceneProbe active {a.name} -> {b.name}");
        DontDestroyOnLoad(gameObject);

        int runs = PlayerPrefs.GetInt("probe.runs", 0) + 1;
        PlayerPrefs.SetInt("probe.runs", runs);
        PlayerPrefs.SetFloat("probe.f", 1.5f);
        PlayerPrefs.SetString("probe.s", "가나다");
        PlayerPrefs.Save();
        Debug.Log($"SceneProbe prefs runs={runs} f={PlayerPrefs.GetFloat("probe.f")} s={PlayerPrefs.GetString("probe.s")} wrongType={PlayerPrefs.GetInt("probe.f", -7)} has={PlayerPrefs.HasKey("probe.s")} path={Application.persistentDataPath.Length > 0}");

        var op = SceneManager.LoadSceneAsync("SceneProbeB", LoadSceneMode.Additive);
        op.allowSceneActivation = false;
        bool completed = false;
        op.completed += _ => completed = true;
        int frames = 0;
        while (op.progress < 0.9f && frames++ < 600) yield return null;
        yield return null;
        yield return null;
        Debug.Log($"SceneProbe waiting progress={op.progress:F1} done={op.isDone} count={SceneManager.sceneCount}");
        op.allowSceneActivation = true;
        yield return op;
        Scene b = SceneManager.GetSceneAt(1);
        Debug.Log($"SceneProbe added count={SceneManager.sceneCount} b={b.name} loaded={b.isLoaded} roots={b.rootCount} completed={completed} objScene={GameObject.Find("BObject").scene.name} active={SceneManager.GetActiveScene().name}");

        SceneManager.SetActiveScene(b);
        var made = new GameObject("MadeInB");
        yield return null;
        Debug.Log($"SceneProbe made scene={made.scene.name} active={SceneManager.GetActiveScene().name}");

        var op2 = SceneManager.LoadSceneAsync("SceneProbeB", LoadSceneMode.Additive);
        yield return op2;
        Scene b2 = SceneManager.GetSceneAt(2);
        Debug.Log($"SceneProbe twice count={SceneManager.sceneCount} handlesDiffer={b.handle != b2.handle} roots2={b2.rootCount}");

        var u = SceneManager.UnloadSceneAsync(b);
        yield return u;
        yield return null;
        Debug.Log($"SceneProbe unloadedB count={SceneManager.sceneCount} bLoaded={b.isLoaded} madeAlive={made != null} b2Roots={b2.rootCount} active={SceneManager.GetActiveScene().name}");

        SceneManager.LoadScene("SceneProbeC");
        yield return null;
        yield return null;
        Debug.Log($"SceneProbe single count={SceneManager.sceneCount} active={SceneManager.GetActiveScene().name} mine={gameObject.scene.name} awakes={m_Awakes} starts={m_Starts} cObject={GameObject.Find("CObject") != null} bObject={GameObject.Find("BObject") != null}");
    }
}
