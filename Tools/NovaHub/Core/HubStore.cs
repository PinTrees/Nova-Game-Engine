using System.Diagnostics;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace NovaHub;

public sealed record InstalledEngine(string Version, string Root, bool Managed = true)
{
    public string Executable => Path.Combine(Root, "Binaries", "NovaEngine.exe");
    public bool Available => File.Exists(Executable) && File.Exists(Path.Combine(Root, "Binaries", "NovaCore.dll"));
}

// These names match the native Hub's existing projects.json format.
public sealed record HubProject(string Name, string Path, string EngineVersion, string Template = "3D",
    long LastOpened = 0, bool Favorite = false);

public sealed class HubStore
{
    internal static readonly JsonSerializerOptions JsonOptions = new()
    { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, PropertyNameCaseInsensitive = true, WriteIndented = true };
    public string StateRoot { get; }
    public string EngineRoot { get; }
    public List<InstalledEngine> Engines { get; private set; } = [];
    public List<HubProject> Projects { get; private set; } = [];
    public HubStore(string? stateRoot = null, string? engineRoot = null)
    {
        StateRoot = Path.GetFullPath(stateRoot ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NOVA", "Hub"));
        EngineRoot = Path.GetFullPath(engineRoot ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NOVA", "Editors"));
        Directory.CreateDirectory(StateRoot);
        Reload();
    }
    public void Reload()
    {
        Engines = ReadList<InstalledEngine>("engines.json", "engines");
        Projects = ReadList<HubProject>("projects.json", "projects");
        // Recover a completed install whose final registry write was interrupted.
        if (Directory.Exists(EngineRoot))
            foreach (var root in Directory.EnumerateDirectories(EngineRoot).Where(p => !Path.GetFileName(p).StartsWith('.')))
            {
                var marker = Path.Combine(root, ".nova-install.json");
                if (!File.Exists(marker)) continue;
                try
                {
                    var engine = JsonSerializer.Deserialize<InstalledEngine>(File.ReadAllText(marker), JsonOptions);
                    if (engine is not null && SamePath(engine.Root, root) && engine.Available && !Engines.Any(e => SamePath(e.Root, root))) Engines.Add(engine);
                }
                catch (JsonException) { }
            }
    }
    private List<T> ReadList<T>(string file, string key)
    {
        var path = Path.Combine(StateRoot, file);
        if (!File.Exists(path)) return [];
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            return doc.RootElement.GetProperty(key).Deserialize<List<T>>(JsonOptions) ?? [];
        }
        catch (Exception e) when (e is JsonException or KeyNotFoundException)
        { throw new InvalidDataException($"목록 파일을 읽을 수 없습니다. 원본을 보존했습니다: {path}", e); }
    }
    internal static void WriteJson(string path, object data)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temp, JsonSerializer.Serialize(data, JsonOptions));
            File.Move(temp, path, true);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    public void Register(InstalledEngine engine)
    {
        if (!engine.Available) throw new InvalidDataException("선택한 엔진 폴더에 NovaEngine.exe와 NovaCore.dll이 필요합니다.");
        var next = Engines.Where(e => !SamePath(e.Root, engine.Root)).Append(engine).ToList();
        WriteJson(Path.Combine(StateRoot, "engines.json"), new { engines = next });
        Engines = next;
    }
    public void Unregister(InstalledEngine engine)
    {
        var next = Engines.Where(e => !SamePath(e.Root, engine.Root)).ToList();
        WriteJson(Path.Combine(StateRoot, "engines.json"), new { engines = next });
        Engines = next;
    }
    public void RemoveEngine(InstalledEngine engine)
    {
        if (IsRunning(engine)) throw new InvalidOperationException("이 엔진의 에디터를 종료한 뒤 다시 시도하세요.");
        if (engine.Managed)
        {
            RequireInside(EngineRoot, engine.Root);
            var marker = Path.Combine(engine.Root, ".nova-install.json");
            if (!File.Exists(marker)) throw new InvalidOperationException("Hub가 설치한 엔진인지 확인할 수 없어 삭제하지 않았습니다.");
            var stored = JsonSerializer.Deserialize<InstalledEngine>(File.ReadAllText(marker), JsonOptions);
            if (stored is null || !SamePath(stored.Root, engine.Root) || stored.Version != engine.Version) throw new InvalidOperationException("설치 기록이 일치하지 않습니다.");
            RejectReparseTree(engine.Root);
            Directory.Delete(engine.Root, true);
        }
        Unregister(engine); // Imported engines are only removed from the list.
    }
    public static bool IsRunning(InstalledEngine engine)
    {
        foreach (var process in Process.GetProcessesByName("NovaEngine"))
            using (process)
            {
                try { if (SamePath(process.MainModule!.FileName, engine.Executable)) return true; }
                catch (Exception e) when (e is System.ComponentModel.Win32Exception or InvalidOperationException)
                { throw new InvalidOperationException("실행 중인 에디터를 확인하지 못했습니다. 에디터를 종료하고 다시 시도하세요.", e); }
            }
        return false;
    }
    public HubProject AddProject(string folder)
    {
        folder = Path.TrimEndingDirectorySeparator(Path.GetFullPath(folder));
        if (!Directory.Exists(Path.Combine(folder, "Assets"))) throw new InvalidDataException("Assets 폴더가 있는 NOVA 프로젝트를 선택하세요.");
        string name = Path.GetFileName(folder), version = "";
        var settings = Path.Combine(folder, "ProjectSettings", "ProjectSettings.json");
        if (File.Exists(settings))
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(settings));
            if (doc.RootElement.TryGetProperty("projectName", out var n)) name = n.GetString() ?? name;
            if (doc.RootElement.TryGetProperty("engineVersion", out var v)) version = v.GetString() ?? "";
        }
        var existing = Projects.FirstOrDefault(p => SamePath(p.Path, folder));
        var project = existing ?? new HubProject(name, folder, version);
        SaveProject(project);
        return project;
    }
    public HubProject CreateProject(string name, string location, string version, string template)
    {
        name = name.Trim();
        if (string.IsNullOrWhiteSpace(name) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.EndsWith('.') ||
            Regex.IsMatch(name, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?$", RegexOptions.IgnoreCase))
            throw new ArgumentException("사용할 수 있는 프로젝트 이름을 입력하세요.");
        if (!Engines.Any(e => e.Version == version && e.Available)) throw new InvalidOperationException("사용할 엔진을 먼저 설치하세요.");
        if (template is not ("3D" or "Empty")) throw new ArgumentException("프로젝트 템플릿이 올바르지 않습니다.");
        var folder = Path.GetFullPath(Path.Combine(location, name));
        if (Directory.Exists(folder) || File.Exists(folder)) throw new IOException("같은 이름의 폴더가 이미 있습니다. 다른 이름을 입력하세요.");
        Directory.CreateDirectory(Path.Combine(folder, "Assets", "Scenes"));
        WriteJson(Path.Combine(folder, "ProjectSettings", "ProjectSettings.json"), new { projectName = name, engineVersion = version, template, created = DateTimeOffset.UtcNow.ToUnixTimeSeconds() });
        WriteJson(Path.Combine(folder, "Assets", "EditorSettings.json"), new { LastOpenedScenePath = "" });
        WriteJson(Path.Combine(folder, "Packages", "manifest.json"), new { dependencies = new Dictionary<string, string>() });
        var project = new HubProject(name, folder, version, template);
        SaveProject(project);
        return project;
    }
    private void SaveProject(HubProject project)
    {
        var next = Projects.Where(p => !SamePath(p.Path, project.Path)).Prepend(project).ToList();
        WriteJson(Path.Combine(StateRoot, "projects.json"), new { projects = next });
        Projects = next;
    }
    public void RemoveProject(HubProject project)
    {
        var next = Projects.Where(p => !SamePath(p.Path, project.Path)).ToList();
        WriteJson(Path.Combine(StateRoot, "projects.json"), new { projects = next });
        Projects = next; // Never delete user project files.
    }
    public ProcessStartInfo EditorStartInfo(HubProject project, InstalledEngine engine)
    {
        if (!engine.Available || !Directory.Exists(Path.Combine(project.Path, "Assets"))) throw new FileNotFoundException("엔진 또는 프로젝트가 없습니다.");
        if (project.EngineVersion.Length > 0 && project.EngineVersion != engine.Version) throw new InvalidOperationException($"이 프로젝트에는 NOVA {project.EngineVersion}이 필요합니다. 설치 탭에서 해당 버전을 설치하세요.");
        var info = new ProcessStartInfo(engine.Executable) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(engine.Executable)! };
        info.ArgumentList.Add("--project"); info.ArgumentList.Add(project.Path);
        return info;
    }
    public void LaunchProject(HubProject project, InstalledEngine engine)
    {
        using var process = Process.Start(EditorStartInfo(project, engine)) ?? throw new IOException("에디터를 시작하지 못했습니다.");
        SaveProject(project with { LastOpened = DateTimeOffset.UtcNow.ToUnixTimeSeconds(), EngineVersion = engine.Version });
    }
    internal static bool SamePath(string a, string b) => string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(a)), Path.TrimEndingDirectorySeparator(Path.GetFullPath(b)), StringComparison.OrdinalIgnoreCase);
    internal static void RequireInside(string parent, string child)
    {
        var prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(parent)) + Path.DirectorySeparatorChar;
        if (!Path.GetFullPath(child).StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("지정한 설치 폴더 밖의 파일은 변경할 수 없습니다.");
    }
    internal static void RejectReparseTree(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("설치 폴더에 링크가 있어 진행하지 않았습니다.");
        foreach (var child in Directory.EnumerateFileSystemEntries(path))
        {
            if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("설치 폴더에 링크가 있어 진행하지 않았습니다.");
            if (Directory.Exists(child)) RejectReparseTree(child);
        }
    }
}
