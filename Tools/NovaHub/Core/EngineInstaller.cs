using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace NovaHub;

public sealed record EngineRelease(string Version, string Tag, Uri Download, long Size, string Sha256, int Revision = 0);
public sealed record InstallProgress(string Message, double? Fraction = null);

public sealed class EngineInstaller : IDisposable
{
    public const string Repository = "PinTrees/Nova-Game-Engine";
    private const long MaxArchiveBytes = 8L * 1024 * 1024 * 1024;
    private const long MaxExpandedBytes = 30L * 1024 * 1024 * 1024;
    private readonly HttpClient http;
    private readonly HubStore store;
    private readonly SemaphoreSlim operation = new(1, 1);
    public EngineInstaller(HubStore store, HttpMessageHandler? handler = null)
    {
        this.store = store;
        http = handler is null ? new HttpClient() : new HttpClient(handler);
        http.Timeout = Timeout.InfiniteTimeSpan;
        http.DefaultRequestHeaders.UserAgent.ParseAdd("NOVA-Hub/0.1.0");
        http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
    }
    public void Dispose() { http.Dispose(); operation.Dispose(); }
    public static bool ValidVersion(string version) => Regex.IsMatch(version, @"^\d+\.\d+\.\d+(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?$");
    private static bool ValidDownload(Uri uri) => uri.Scheme == Uri.UriSchemeHttps && uri.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase)
        && uri.AbsolutePath.StartsWith("/" + Repository + "/releases/download/", StringComparison.Ordinal) && uri.UserInfo.Length == 0;
    public static IReadOnlyList<EngineRelease> ParseCatalog(string json)
    {
        using var doc = JsonDocument.Parse(json);
        if (doc.RootElement.ValueKind != JsonValueKind.Array) throw new InvalidDataException("엔진 목록의 형식이 올바르지 않습니다.");
        var releases = new List<EngineRelease>();
        foreach (var release in doc.RootElement.EnumerateArray())
        {
            if (release.GetProperty("draft").GetBoolean() || release.GetProperty("prerelease").GetBoolean()) continue;
            string tag = release.GetProperty("tag_name").GetString() ?? "";
            foreach (var asset in release.GetProperty("assets").EnumerateArray())
            {
                var match = Regex.Match(asset.GetProperty("name").GetString() ?? "", @"^NOVA-Engine-(.+)-win64(?:-r([1-9][0-9]*))?\.zip$");
                if (!match.Success || !ValidVersion(match.Groups[1].Value)) continue;
                var version = match.Groups[1].Value;
                if (tag != "v" + version && tag != version) continue;
                if (!Uri.TryCreate(asset.GetProperty("browser_download_url").GetString(), UriKind.Absolute, out var uri) || !ValidDownload(uri)) continue;
                long size = asset.GetProperty("size").GetInt64();
                string digest = asset.TryGetProperty("digest", out var d) ? d.GetString() ?? "" : "";
                if (size < 1 || size > MaxArchiveBytes || !Regex.IsMatch(digest, @"^sha256:[0-9a-fA-F]{64}$")) continue;
                int revision = 0;
                if (match.Groups[2].Success && !int.TryParse(match.Groups[2].Value, out revision)) continue;
                releases.Add(new EngineRelease(version, tag, uri, size, digest[7..].ToLowerInvariant(), revision));
            }
        }
        return releases.GroupBy(r => r.Version).Select(g => g.OrderByDescending(r => r.Revision).First()).ToArray();
    }
    public async Task<IReadOnlyList<EngineRelease>> CatalogAsync(CancellationToken token)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        using var response = await http.GetAsync($"https://api.github.com/repos/{Repository}/releases?per_page=100", HttpCompletionOption.ResponseHeadersRead, timeout.Token);
        if (response.StatusCode == HttpStatusCode.Forbidden || (int)response.StatusCode == 429)
            throw new HttpRequestException("엔진 목록 요청 한도에 도달했습니다. 잠시 뒤 새로고침하세요.");
        response.EnsureSuccessStatusCode();
        var content = await response.Content.ReadAsByteArrayAsync(timeout.Token);
        if (content.Length > 8 * 1024 * 1024) throw new InvalidDataException("엔진 목록 응답이 너무 큽니다.");
        return ParseCatalog(System.Text.Encoding.UTF8.GetString(content));
    }
    public async Task<InstalledEngine> InstallAsync(EngineRelease release, IProgress<InstallProgress>? progress, CancellationToken token)
    {
        if (!ValidVersion(release.Version) || !ValidDownload(release.Download) || release.Size < 1 || release.Size > MaxArchiveBytes ||
            !Regex.IsMatch(release.Sha256, @"^[0-9a-fA-F]{64}$")) throw new InvalidDataException("배포 정보가 올바르지 않습니다.");
        if (!await operation.WaitAsync(0, token)) throw new InvalidOperationException("다른 설치가 진행 중입니다.");
        string final = Path.Combine(store.EngineRoot, release.Version);
        string stage = Path.Combine(store.EngineRoot, ".install-" + Guid.NewGuid().ToString("N"));
        string archive = Path.Combine(stage, "engine.zip"), extracted = Path.Combine(stage, "payload");
        try
        {
            if (Directory.Exists(final) || File.Exists(final)) throw new IOException("이 버전의 설치 폴더가 이미 있습니다. 설치 목록을 새로고침하세요.");
            Directory.CreateDirectory(store.EngineRoot);
            if ((File.GetAttributes(store.EngineRoot) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("설치 경로에 링크가 있어 진행하지 않았습니다.");
            var drive = new DriveInfo(Path.GetPathRoot(store.EngineRoot)!);
            if (drive.AvailableFreeSpace < release.Size * 2) throw new IOException("엔진을 내려받을 디스크 공간이 부족합니다.");
            Directory.CreateDirectory(stage);
            progress?.Report(new("엔진을 다운로드하고 있습니다.", 0));
            using (var timeout = CancellationTokenSource.CreateLinkedTokenSource(token))
            {
                timeout.CancelAfter(TimeSpan.FromMinutes(60));
                using var response = await http.GetAsync(release.Download, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
                response.EnsureSuccessStatusCode();
                if (response.Content.Headers.ContentLength is long expected && expected != release.Size) throw new InvalidDataException("서버의 파일 크기가 배포 정보와 다릅니다.");
                using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
                await using var input = await response.Content.ReadAsStreamAsync(timeout.Token);
                await using var output = new FileStream(archive, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024, true);
                var buffer = new byte[1024 * 1024]; long received = 0; var last = DateTime.MinValue;
                while (true)
                {
                    int count = await input.ReadAsync(buffer, timeout.Token);
                    if (count == 0) break;
                    received += count;
                    if (received > release.Size) throw new InvalidDataException("다운로드 파일이 예상 크기를 초과했습니다.");
                    await output.WriteAsync(buffer.AsMemory(0, count), timeout.Token);
                    hash.AppendData(buffer, 0, count);
                    if ((DateTime.UtcNow - last).TotalMilliseconds >= 100)
                    { progress?.Report(new($"다운로드 중 · {received / 1048576.0:N1} / {release.Size / 1048576.0:N1} MB", received / (double)release.Size * .75)); last = DateTime.UtcNow; }
                }
                if (received != release.Size || !Convert.ToHexString(hash.GetHashAndReset()).Equals(release.Sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("다운로드 파일 검증에 실패했습니다. 다시 설치를 시도하세요.");
            }
            token.ThrowIfCancellationRequested();
            progress?.Report(new("파일을 검증했습니다. 설치를 준비합니다.", .75));
            await Task.Run(() => Extract(archive, extracted, token, progress), token);
            token.ThrowIfCancellationRequested();
            var installed = new InstalledEngine(release.Version, final);
            HubStore.WriteJson(Path.Combine(extracted, ".nova-install.json"), installed);
            // Commit only after the whole archive is verified and extracted. Existing versions are untouched.
            Directory.Move(extracted, final);
            store.Register(installed);
            progress?.Report(new("설치를 완료했습니다.", 1));
            return installed;
        }
        finally
        {
            try
            {
                if (Directory.Exists(stage)) { HubStore.RequireInside(store.EngineRoot, stage); HubStore.RejectReparseTree(stage); Directory.Delete(stage, true); }
            }
            finally { operation.Release(); }
        }
    }
    internal static void Extract(string archive, string target, CancellationToken token, IProgress<InstallProgress>? progress)
    {
        using var zip = ZipFile.OpenRead(archive);
        var executables = zip.Entries.Where(e => e.FullName.Replace('\\', '/').EndsWith("Binaries/NovaEngine.exe", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (executables.Length != 1) throw new InvalidDataException("배포 파일의 엔진 실행 파일이 없거나 중복되었습니다.");
        string exe = executables[0].FullName.Replace('\\', '/');
        string prefix = exe[..^"Binaries/NovaEngine.exe".Length];
        if (prefix.Length > 0 && (prefix.Count(c => c == '/') != 1 || prefix.Contains(".."))) throw new InvalidDataException("배포 파일의 폴더 구조가 올바르지 않습니다.");
        var files = new List<(ZipArchiveEntry entry, string path)>(); var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long size = 0;
        foreach (var entry in zip.Entries)
        {
            token.ThrowIfCancellationRequested();
            var name = entry.FullName.Replace('\\', '/');
            if (name.Length == 0 || name.StartsWith('/') || name.Contains(':') || name.Split('/').Any(s => s is ".." or "." || s != s.TrimEnd(' ', '.')) ||
                ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000 || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("배포 파일에 사용할 수 없는 경로가 있습니다.");
            if (!name.StartsWith(prefix, StringComparison.Ordinal)) throw new InvalidDataException("배포 파일에 엔진 폴더 밖의 파일이 있습니다.");
            name = name[prefix.Length..]; if (name.Length == 0) continue;
            var path = Path.GetFullPath(Path.Combine(target, name)); HubStore.RequireInside(target, path);
            if (!names.Add(path)) throw new InvalidDataException("배포 파일에 중복된 경로가 있습니다.");
            size = checked(size + entry.Length);
            if (size > MaxExpandedBytes || zip.Entries.Count > 100000) throw new InvalidDataException("배포 파일의 압축 해제 크기가 너무 큽니다.");
            files.Add((entry, path));
        }
        if (new DriveInfo(Path.GetPathRoot(target)!).AvailableFreeSpace < size + 64 * 1024 * 1024) throw new IOException("압축을 풀 디스크 공간이 부족합니다.");
        Directory.CreateDirectory(target); int written = 0;
        foreach (var (entry, path) in files)
        {
            token.ThrowIfCancellationRequested();
            if (entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\')) { Directory.CreateDirectory(path); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var input = entry.Open(); using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write);
            var buffer = new byte[1024 * 1024]; int count;
            while ((count = input.Read(buffer)) > 0) { token.ThrowIfCancellationRequested(); output.Write(buffer, 0, count); }
            if (++written % 25 == 0) progress?.Report(new($"설치 중 · {written} / {files.Count} 파일", .75 + .24 * written / files.Count));
        }
        foreach (var required in new[] { "Binaries/NovaEngine.exe", "Binaries/NovaCore.dll" })
            if (!File.Exists(Path.Combine(target, required))) throw new InvalidDataException("필수 엔진 파일이 누락되었습니다.");
    }
}
