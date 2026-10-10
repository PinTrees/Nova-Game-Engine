// NOVA 가져오기용: Unity 가 프리팹 (변형 · 중첩 포함) 을 풀어 부위 · 재질 · 붙인 물건을 JSON 으로 내보낸다.
// Unity.exe -batchmode -nographics -projectPath <이 프로젝트> -executeMethod NovaExport.Run -quit
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

public static class NovaExport
{
    static string Esc(string s) => s == null ? "" : s.Replace("\\", "\\\\").Replace("\"", "\\\"");

    static string PathOf(Transform t, Transform root)
    {
        var parts = new List<string>();
        for (var c = t; c != null && c != root; c = c.parent) parts.Insert(0, c.name);
        return string.Join("/", parts);
    }

    // 루트를 뺀 부모 사슬의 activeSelf (프리팹 루트가 꺼져 있어도 부위 켜짐을 본다)
    static bool Active(Transform t, Transform root)
    {
        for (var c = t; c != null && c != root; c = c.parent)
            if (!c.gameObject.activeSelf) return false;
        return true;
    }

    static void Vec(StringBuilder sb, string key, Vector3 v) =>
        sb.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, "\"{0}\": [{1}, {2}, {3}]", key, v.x, v.y, v.z);

    static void Mats(StringBuilder sb, Material[] mats)
    {
        sb.Append("\"materials\": [");
        for (int i = 0; i < mats.Length; ++i)
        {
            var m = mats[i];
            string path = m ? AssetDatabase.GetAssetPath(m) : "";
            string guid = string.IsNullOrEmpty(path) ? "" : AssetDatabase.AssetPathToGUID(path);
            sb.AppendFormat("{0}{{\"name\": \"{1}\", \"path\": \"{2}\", \"guid\": \"{3}\"}}", i > 0 ? ", " : "", Esc(m ? m.name : ""), Esc(path), guid);
        }
        sb.Append("]");
    }

    public static void Run()
    {
        string outDir = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "Export"));
        Directory.CreateDirectory(outDir);
        int count = 0;
        foreach (string guid in AssetDatabase.FindAssets("t:Prefab"))
        {
            string path = AssetDatabase.GUIDToAssetPath(guid);
            var root = AssetDatabase.LoadAssetAtPath<GameObject>(path);
            if (root == null) continue;
            var sb = new StringBuilder();
            sb.Append("{\n");
            sb.AppendFormat("\"prefab\": \"{0}\",\n\"name\": \"{1}\",\n", Esc(path), Esc(root.name));
            var src = PrefabUtility.GetCorrespondingObjectFromOriginalSource(root);
            sb.AppendFormat("\"source\": \"{0}\",\n", Esc(src ? AssetDatabase.GetAssetPath(src) : ""));
            // 스킨 메시: 부위 경로 · 켜짐 · 메시 · 메시가 들어 있는 모델 파일 · 재질
            //  + 이 프리팹 자세 (바인드) 에서 구운 정점의 범위 (프리팹 루트 공간) — 다른 엔진이 맞게 스키닝했는지 대조하는 정답
            var inst = (GameObject)PrefabUtility.InstantiatePrefab(root);
            inst.transform.position = Vector3.zero;
            inst.transform.rotation = Quaternion.identity;
            var baked = new Dictionary<string, Bounds>();
            foreach (var ir in inst.GetComponentsInChildren<SkinnedMeshRenderer>(true))
            {
                if (ir.sharedMesh == null) continue;
                var bm = new Mesh();
                ir.BakeMesh(bm, true);
                var vs = bm.vertices;
                if (vs.Length == 0) continue;
                var m = ir.transform.localToWorldMatrix;
                var b = new Bounds(m.MultiplyPoint3x4(vs[0]), Vector3.zero);
                for (int i = 1; i < vs.Length; i += 7) b.Encapsulate(m.MultiplyPoint3x4(vs[i]));
                baked[PathOf(ir.transform, inst.transform)] = b;
                Object.DestroyImmediate(bm);
            }
            Object.DestroyImmediate(inst);
            sb.Append("\"skinned\": [\n");
            bool first = true;
            foreach (var r in root.GetComponentsInChildren<SkinnedMeshRenderer>(true))
            {
                if (!first) sb.Append(",\n");
                first = false;
                var mesh = r.sharedMesh;
                sb.AppendFormat("  {{\"path\": \"{0}\", \"name\": \"{1}\", \"active\": {2}, \"enabled\": {3}, \"mesh\": \"{4}\", \"model\": \"{5}\", ",
                    Esc(PathOf(r.transform, root.transform)), Esc(r.name), Active(r.transform, root.transform) ? "true" : "false",
                    r.enabled ? "true" : "false", Esc(mesh ? mesh.name : ""), Esc(mesh ? AssetDatabase.GetAssetPath(mesh) : ""));
                if (baked.TryGetValue(PathOf(r.transform, root.transform), out var bb))
                {
                    Vec(sb, "bakedMin", bb.min); sb.Append(", ");
                    Vec(sb, "bakedMax", bb.max); sb.Append(", ");
                }
                Mats(sb, r.sharedMaterials);
                sb.Append("}");
            }
            sb.Append("\n],\n");
            // 붙인 물건 (무기 · 소품): 메시 필터 — 부모 본 이름 · 로컬 변환
            sb.Append("\"props\": [\n");
            first = true;
            foreach (var f in root.GetComponentsInChildren<MeshFilter>(true))
            {
                var mr = f.GetComponent<MeshRenderer>();
                if (mr == null || f.sharedMesh == null) continue;
                if (!first) sb.Append(",\n");
                first = false;
                var t = f.transform;
                sb.AppendFormat("  {{\"path\": \"{0}\", \"name\": \"{1}\", \"active\": {2}, \"parent\": \"{3}\", \"mesh\": \"{4}\", \"model\": \"{5}\", ",
                    Esc(PathOf(t, root.transform)), Esc(t.name), Active(f.transform, root.transform) ? "true" : "false",
                    Esc(t.parent ? t.parent.name : ""), Esc(f.sharedMesh.name), Esc(AssetDatabase.GetAssetPath(f.sharedMesh)));
                Vec(sb, "position", t.localPosition); sb.Append(", ");
                Vec(sb, "euler", t.localEulerAngles); sb.Append(", ");
                Vec(sb, "scale", t.localScale); sb.Append(", ");
                Mats(sb, mr.sharedMaterials);
                sb.Append("}");
            }
            sb.Append("\n]\n}\n");
            File.WriteAllText(Path.Combine(outDir, root.name + ".json"), sb.ToString(), new UTF8Encoding(false));
            ++count;
        }
        // 재질: 셰이더 · 텍스처 · 값 (HDRP Lit · URP Lit · Standard)
        var mb = new StringBuilder();
        mb.Append("[\n");
        bool mfirst = true;
        foreach (string guid in AssetDatabase.FindAssets("t:Material"))
        {
            string path = AssetDatabase.GUIDToAssetPath(guid);
            var m = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (m == null) continue;
            if (!mfirst) mb.Append(",\n");
            mfirst = false;
            mb.AppendFormat("{{\"path\": \"{0}\", \"guid\": \"{1}\", \"name\": \"{2}\", \"shader\": \"{3}\", \"textures\": {{", Esc(path), guid, Esc(m.name), Esc(m.shader ? m.shader.name : ""));
            bool tf = true;
            foreach (string p in m.GetTexturePropertyNames())
            {
                var tex = m.GetTexture(p);
                if (tex == null) continue;
                mb.AppendFormat("{0}\"{1}\": \"{2}\"", tf ? "" : ", ", p, Esc(AssetDatabase.GetAssetPath(tex)));
                tf = false;
            }
            mb.Append("}, \"floats\": {");
            var so = new SerializedObject(m);
            bool ff = true;
            var floats = so.FindProperty("m_SavedProperties.m_Floats");
            for (int i = 0; floats != null && i < floats.arraySize; ++i)
            {
                var e = floats.GetArrayElementAtIndex(i);
                mb.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, "{0}\"{1}\": {2}", ff ? "" : ", ", e.FindPropertyRelative("first").stringValue, e.FindPropertyRelative("second").floatValue);
                ff = false;
            }
            mb.Append("}, \"colors\": {");
            bool cf = true;
            var colors = so.FindProperty("m_SavedProperties.m_Colors");
            for (int i = 0; colors != null && i < colors.arraySize; ++i)
            {
                var e = colors.GetArrayElementAtIndex(i);
                var c = e.FindPropertyRelative("second").colorValue;
                mb.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, "{0}\"{1}\": [{2}, {3}, {4}, {5}]", cf ? "" : ", ", e.FindPropertyRelative("first").stringValue, c.r, c.g, c.b, c.a);
                cf = false;
            }
            mb.Append("}, \"keywords\": [");
            var kws = m.shaderKeywords;
            for (int i = 0; i < kws.Length; ++i) mb.AppendFormat("{0}\"{1}\"", i > 0 ? ", " : "", kws[i]);
            mb.Append("]}");
        }
        mb.Append("\n]\n");
        File.WriteAllText(Path.Combine(outDir, "_materials.json"), mb.ToString(), new UTF8Encoding(false));
        Debug.Log("NovaExport: " + count + " prefabs -> " + outDir);
    }
}
