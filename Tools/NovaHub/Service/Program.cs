using System.Diagnostics;
using System.Text.Json;
using NovaHub;

if (args.Length != 2 || args[0] != "--request") return 2;
var requestPath = Path.GetFullPath(args[1]);
var job = Path.GetDirectoryName(requestPath)!;
var options = new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, PropertyNameCaseInsensitive = true };
void Write(string name, object value)
{
    var path = Path.Combine(job, name);
    var temp = path + ".tmp";
    File.WriteAllText(temp, JsonSerializer.Serialize(value, options));
    File.Move(temp, path, true);
}
try
{
    var request = JsonSerializer.Deserialize<Request>(File.ReadAllText(requestPath), options) ?? throw new InvalidDataException("설치 요청이 없습니다.");
    var store = new HubStore(request.StateRoot, request.EngineRoot);
    using var cts = new CancellationTokenSource();
    using var installer = new EngineInstaller(store);
    using var android = new AndroidToolsInstaller(request.AndroidRoot ?? AndroidToolsInstaller.DefaultRoot(store.EngineRoot));
    AndroidCatalog? androidCatalog = null;
    var monitor = Task.Run(async () =>
    {
        while (!cts.IsCancellationRequested)
        {
            if (File.Exists(Path.Combine(job, "cancel"))) { cts.Cancel(); break; }
            if (request.ParentPid > 0)
            {
                try { using var parent = Process.GetProcessById(request.ParentPid); if (parent.HasExited) { cts.Cancel(); break; } }
                catch (ArgumentException) { cts.Cancel(); break; }
            }
            try { await Task.Delay(200, cts.Token); } catch (OperationCanceledException) { break; }
        }
    });
    IReadOnlyList<EngineRelease> releases = [];
    try
    {
        switch (request.Command)
        {
            case "catalog": releases = await installer.CatalogAsync(cts.Token); break;
            case "install":
                // Resolve again from the official catalog; the UI cannot provide a different download URL.
                releases = await installer.CatalogAsync(cts.Token);
                var release = releases.FirstOrDefault(r => r.Version == request.Version) ?? throw new InvalidDataException("설치할 엔진 버전이 없습니다.");
                await installer.InstallAsync(release, new FileProgress(p => Write("progress.json", p)), cts.Token);
                break;
            case "remove":
                var engine = store.Engines.SingleOrDefault(e => e.Root.Equals(request.Root, StringComparison.OrdinalIgnoreCase)) ?? throw new InvalidDataException("설치된 엔진을 찾을 수 없습니다.");
                store.RemoveEngine(engine); break;
            case "list": break;
            // Android 빌드 지원 모듈 (OpenJDK · Android SDK · NDK) — 모든 엔진 버전이 같이 쓴다
            case "android-status": break;
            case "android-catalog": androidCatalog = await android.CatalogAsync(cts.Token); break;
            case "android-install":
                // 공식 목록을 다시 받는다 (화면이 다른 주소를 넘길 수 없다). 라이선스 동의는 사용자가 Hub 에서 직접
                androidCatalog = await android.CatalogAsync(cts.Token);
                await android.InstallAsync(androidCatalog, request.AcceptLicense, new FileProgress(p => Write("progress.json", p)), cts.Token);
                break;
            default: throw new InvalidDataException("지원하지 않는 설치 요청입니다.");
        }
        var androidStatus = android.Status(androidCatalog);
        Write("result.json", new
        {
            ok = true, releases, engines = store.Engines,
            android = new
            {
                root = androidStatus.Root, installed = androidStatus.Installed, missing = androidStatus.Missing.Select(AndroidToolsInstaller.Label),
                downloadBytes = androidStatus.DownloadBytes, licenseId = androidCatalog?.LicenseId, license = androidCatalog?.License,
                packages = androidCatalog?.Packages.Select(p => new { id = p.Id, label = AndroidToolsInstaller.Label(p.Id), size = p.Size, installed = android.IsInstalled(p.Folder) })
            }
        });
    }
    finally { cts.Cancel(); await monitor; }
    return 0;
}
catch (OperationCanceledException) { Write("result.json", new { ok = false, error = "설치를 취소했습니다. 기존 엔진과 프로젝트는 보존됩니다." }); return 1; }
catch (Exception e) { Write("result.json", new { ok = false, error = e.Message }); return 1; }

sealed record Request(string Command, string? StateRoot, string? EngineRoot, string Version = "", string Root = "", int ParentPid = 0,
    bool AcceptLicense = false, string? AndroidRoot = null);
sealed class FileProgress(Action<InstallProgress> report) : IProgress<InstallProgress>
{ public void Report(InstallProgress value) => report(value); }
