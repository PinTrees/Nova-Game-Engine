using System;
using System.IO;
using UnityEditor;
using UnityEditor.Formats.Fbx.Exporter;
using UnityEngine;

// NOVA: 모든 부위가 든 프리팹을 Unity FBX Exporter (com.unity.formats.fbx) 로 다시 내보낸다.
//  일부 FBX (예: TheTalesFactory Medieval Civil & Soldier) 는 부위마다 스킨 바인드 자세 (TransformLink) 가 노드와 어긋나
//  Assimp 가 그 부위를 수백 m 크기로 읽는다. Unity 가 읽은 결과 (Unity 화면과 같은 자리) 를 표준 FBX 로 다시 쓰면 NOVA 가 바로 읽는다.
//  임시 Unity 프로젝트의 Assets/Editor 에 NovaExport.cs 와 함께 넣고, Packages/manifest.json 에 "com.unity.formats.fbx" 를 더한 뒤:
//    Unity.exe -batchmode -nographics -projectPath <임시> -executeMethod NovaFbxReexport.Run -quit
//        -novaPrefab "Assets/<패키지>/.../_AllParts.prefab" -novaOut Export/<이름>_Reexport.fbx
public static class NovaFbxReexport
{
    static string Arg(string name, string fallback)
    {
        string[] a = Environment.GetCommandLineArgs();
        for (int i = 0; i + 1 < a.Length; i++)
            if (a[i] == name)
                return a[i + 1];
        return fallback;
    }

    public static void Run()
    {
        string src = Arg("-novaPrefab", "Assets/TheTalesFactory/Medieval Civil & Soldier/Model/Prefabs/_MedievalCivil&Soldier_Prefab.prefab");
        string outPath = Path.GetFullPath(Arg("-novaOut", "Export/CivilSoldier_Reexport.fbx"));
        var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(src);
        if (prefab == null)
        {
            Debug.LogError("NovaFbxReexport: prefab not found " + src);
            return;
        }
        var go = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
        PrefabUtility.UnpackPrefabInstance(go, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);
        // 템플릿 = 모든 부위 (꺼진 부위도 내보낸다 — 변형 프리팹은 NOVA 쪽에서 켜고 끈다)
        int n = 0;
        foreach (var t in go.GetComponentsInChildren<Transform>(true)) { t.gameObject.SetActive(true); n++; }
        Directory.CreateDirectory(Path.GetDirectoryName(outPath));
        string r = ModelExporter.ExportObject(outPath, go);
        Debug.Log("NovaFbxReexport: " + n + " transforms -> " + r);
    }
}
