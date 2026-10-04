using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace NovaHub;

// "Android 빌드 지원" 모듈 (Unity Hub 의 Android Build Support 와 같은 역할): OpenJDK 17 + Android SDK · NDK 도구.
//  모든 엔진 버전이 같이 쓰도록 한 곳에 설치한다: <NOVA>\AndroidTools\{jdk, sdk} (기본 %LOCALAPPDATA%\NOVA\AndroidTools)
//  - SDK 패키지: Google 공식 목록 (dl.google.com/android/repository/repository2-3.xml) 의 크기 · SHA-1 로 검증
//  - JDK: Eclipse Temurin 17 (api.adoptium.net) 의 크기 · SHA-256 로 검증, 내려받기는 github.com/adoptium 만
//  - Android SDK 라이선스는 사용자가 Hub 에서 직접 동의해야 설치한다 (acceptLicense). 동의하면 sdkmanager 와 같은 licenses/ 기록을 남긴다
//  - 패키지마다 임시 폴더에서 검증 · 압축 해제 뒤 최종 폴더로 옮긴다. 이미 설치된 패키지는 건너뛴다 (.nova-package 표시)
public sealed record AndroidPackage(string Id, string Folder, Uri Download, long Size, string Hash, string HashType);
public sealed record AndroidCatalog(IReadOnlyList<AndroidPackage> Packages, string LicenseId, string License);
public sealed record AndroidToolsStatus(string Root, bool Installed, IReadOnlyList<string> Missing, long DownloadBytes);

public sealed class AndroidToolsInstaller : IDisposable
{
    // build.py · 엔진이 찾는 버전 (Android/build.py 와 같게)
    public static readonly (string Id, string Folder)[] SdkPackages =
    {
        ("platform-tools", "sdk/platform-tools"),
        ("build-tools;36.0.0", "sdk/build-tools/36.0.0"),
        ("platforms;android-34", "sdk/platforms/android-34"),
        ("ndk;28.2.13676358", "sdk/ndk/28.2.13676358"),
        ("cmake;3.22.1", "sdk/cmake/3.22.1"),
    };
    public const string JdkId = "jdk;17";
    public const string JdkFolder = "jdk";
    // C# 스크립트 런타임 (Microsoft 의 Mono — .NET 8 모바일 런타임, MIT): 엔진 C# (NovaScriptCore, net8.0) 과 같은 8.0.x. 에디터의 AndroidTools::MonoRuntime 이 찾는다
    public const string MonoId = "mono;android-x64";
    public const string MonoFolder = "mono/x86_64";
    public const string MonoVersion = "8.0.31";
    const string NuGetPackage = "microsoft.netcore.app.runtime.mono.android-x64";
    const string GoogleRepository = "https://dl.google.com/android/repository/";
    const string AdoptiumApi = "https://api.adoptium.net/v3/assets/latest/17/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse";
    const long MaxArchiveBytes = 4L * 1024 * 1024 * 1024;
    const long MaxExpandedBytes = 12L * 1024 * 1024 * 1024;

    readonly HttpClient http;
    public string Root { get; }

    public AndroidToolsInstaller(string root, HttpMessageHandler? handler = null)
    {
        Root = Path.GetFullPath(root);
        http = handler is null ? new HttpClient() : new HttpClient(handler);
        http.Timeout = Timeout.InfiniteTimeSpan;
        http.DefaultRequestHeaders.UserAgent.ParseAdd("NOVA-Hub/0.1.0");
    }
    public void Dispose() => http.Dispose();

    // 엔진 설치 루트 (…\NOVA\Editors) 옆
    public static string DefaultRoot(string engineRoot) => Path.Combine(Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(Path.GetFullPath(engineRoot)))!, "AndroidTools");

    public static IEnumerable<(string Id, string Folder)> AllPackages() => SdkPackages.Prepend((JdkId, JdkFolder)).Append((MonoId, MonoFolder));

    public bool IsInstalled(string folder) => File.Exists(Path.Combine(Root, folder, ".nova-package"));

    public AndroidToolsStatus Status(AndroidCatalog? catalog = null)
    {
        var missing = AllPackages().Where(p => !IsInstalled(p.Folder)).Select(p => p.Id).ToArray();
        long bytes = catalog?.Packages.Where(p => missing.Contains(p.Id)).Sum(p => p.Size) ?? 0;
        return new AndroidToolsStatus(Root, missing.Length == 0, missing, bytes);
    }

    static bool ValidGoogle(Uri u) => u.Scheme == Uri.UriSchemeHttps && u.Host.Equals("dl.google.com", StringComparison.OrdinalIgnoreCase) &&
        u.AbsolutePath.StartsWith("/android/repository/", StringComparison.Ordinal) && u.UserInfo.Length == 0 && !u.AbsolutePath.Contains("..");
    static bool ValidNuGet(Uri u) => u.Scheme == Uri.UriSchemeHttps && u.Host.Equals("api.nuget.org", StringComparison.OrdinalIgnoreCase) &&
        u.AbsolutePath.StartsWith("/v3-flatcontainer/" + NuGetPackage + "/", StringComparison.Ordinal) && u.UserInfo.Length == 0 && !u.AbsolutePath.Contains("..");
    static bool ValidJdk(Uri u) => u.Scheme == Uri.UriSchemeHttps && u.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase) &&
        u.AbsolutePath.StartsWith("/adoptium/", StringComparison.Ordinal) && u.UserInfo.Length == 0 && !u.AbsolutePath.Contains("..");

    // Google 목록 XML → 필요한 패키지 (Windows 용 또는 OS 상관없는 압축 파일) + 라이선스 글
    public static (List<AndroidPackage> Packages, string LicenseId, string License) ParseRepository(string xml)
    {
        var doc = XDocument.Parse(xml);
        var packages = new List<AndroidPackage>();
        string licenseId = "", license = "";
        foreach (var (id, folder) in SdkPackages)
        {
            var pkg = doc.Descendants().FirstOrDefault(e => e.Name.LocalName == "remotePackage" && (string?)e.Attribute("path") == id)
                ?? throw new InvalidDataException($"Android SDK 목록에 {id} 가 없습니다.");
            var archive = pkg.Descendants().Where(e => e.Name.LocalName == "archive")
                .FirstOrDefault(a => a.Elements().FirstOrDefault(c => c.Name.LocalName == "host-os") is not { } os || os.Value == "windows")
                ?? throw new InvalidDataException($"{id} 의 Windows 파일이 없습니다.");
            var complete = archive.Elements().First(e => e.Name.LocalName == "complete");
            long size = long.Parse(complete.Elements().First(e => e.Name.LocalName == "size").Value);
            var checksum = complete.Elements().First(e => e.Name.LocalName == "checksum");
            string sha1 = checksum.Value.Trim().ToLowerInvariant();
            string url = complete.Elements().First(e => e.Name.LocalName == "url").Value.Trim();
            if ((string?)checksum.Attribute("type") != "sha1" || !Regex.IsMatch(sha1, "^[0-9a-f]{40}$")) throw new InvalidDataException($"{id} 의 검사 값이 올바르지 않습니다.");
            if (!Regex.IsMatch(url, @"^[A-Za-z0-9._\-]+\.zip$")) throw new InvalidDataException($"{id} 의 파일 주소가 올바르지 않습니다.");
            var uri = new Uri(GoogleRepository + url);
            if (!ValidGoogle(uri) || size < 1 || size > MaxArchiveBytes) throw new InvalidDataException($"{id} 의 배포 정보가 올바르지 않습니다.");
            packages.Add(new AndroidPackage(id, folder, uri, size, sha1, "sha1"));
            var lic = pkg.Elements().FirstOrDefault(e => e.Name.LocalName == "uses-license")?.Attribute("ref")?.Value ?? "";
            if (licenseId.Length == 0) licenseId = lic;
            else if (lic != licenseId) throw new InvalidDataException("Android SDK 패키지의 라이선스가 서로 다릅니다.");
        }
        license = doc.Descendants().FirstOrDefault(e => e.Name.LocalName == "license" && (string?)e.Attribute("id") == licenseId)?.Value ?? "";
        if (licenseId.Length == 0 || license.Length < 100) throw new InvalidDataException("Android SDK 라이선스 글을 찾지 못했습니다.");
        return (packages, licenseId, license);
    }

    public static AndroidPackage ParseJdk(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var first = doc.RootElement.EnumerateArray().FirstOrDefault();
        if (first.ValueKind != JsonValueKind.Object) throw new InvalidDataException("JDK 목록이 비었습니다.");
        var p = first.GetProperty("binary").GetProperty("package");
        var uri = new Uri(p.GetProperty("link").GetString() ?? "");
        long size = p.GetProperty("size").GetInt64();
        string sha256 = (p.GetProperty("checksum").GetString() ?? "").ToLowerInvariant();
        if (!ValidJdk(uri) || size < 1 || size > MaxArchiveBytes || !Regex.IsMatch(sha256, "^[0-9a-f]{64}$") || !uri.AbsolutePath.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("JDK 배포 정보가 올바르지 않습니다.");
        return new AndroidPackage(JdkId, JdkFolder, uri, size, sha256, "sha256");
    }

    // nuget.org 카탈로그 (크기 · SHA512) → Mono 런타임 패키지 (.nupkg = zip)
    public static AndroidPackage ParseMono(string catalogJson)
    {
        using var doc = JsonDocument.Parse(catalogJson);
        var root = doc.RootElement;
        long size = root.GetProperty("packageSize").GetInt64();
        string algorithm = root.TryGetProperty("packageHashAlgorithm", out var a) ? a.GetString() ?? "" : "";
        string sha512 = Convert.ToHexString(Convert.FromBase64String(root.GetProperty("packageHash").GetString() ?? "")).ToLowerInvariant();
        var uri = new Uri($"https://api.nuget.org/v3-flatcontainer/{NuGetPackage}/{MonoVersion}/{NuGetPackage}.{MonoVersion}.nupkg");
        if (!algorithm.Equals("SHA512", StringComparison.OrdinalIgnoreCase) || !Regex.IsMatch(sha512, "^[0-9a-f]{128}$") || size < 1 || size > MaxArchiveBytes || !ValidNuGet(uri))
            throw new InvalidDataException("Mono 런타임 배포 정보가 올바르지 않습니다.");
        return new AndroidPackage(MonoId, MonoFolder, uri, size, sha512, "sha512");
    }

    async Task<string> GetText(string url, CancellationToken token)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        using var response = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
        response.EnsureSuccessStatusCode();
        var bytes = await response.Content.ReadAsByteArrayAsync(timeout.Token);
        if (bytes.Length > 16 * 1024 * 1024) throw new InvalidDataException("목록 응답이 너무 큽니다.");
        return System.Text.Encoding.UTF8.GetString(bytes);
    }

    public async Task<AndroidCatalog> CatalogAsync(CancellationToken token)
    {
        var (packages, licenseId, license) = ParseRepository(await GetText(GoogleRepository + "repository2-3.xml", token));
        packages.Insert(0, ParseJdk(await GetText(AdoptiumApi, token)));
        // nuget.org: 버전 등록 → 카탈로그 항목 (크기 · SHA512)
        using (var reg = JsonDocument.Parse(await GetText($"https://api.nuget.org/v3/registration5-semver1/{NuGetPackage}/{MonoVersion}.json", token)))
        {
            var entry = new Uri(reg.RootElement.GetProperty("catalogEntry").GetString() ?? "");
            if (entry.Scheme != Uri.UriSchemeHttps || !entry.Host.Equals("api.nuget.org", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Mono 런타임 카탈로그 주소가 올바르지 않습니다.");
            packages.Add(ParseMono(await GetText(entry.ToString(), token)));
        }
        return new AndroidCatalog(packages, licenseId, license);
    }

    public async Task<AndroidToolsStatus> InstallAsync(AndroidCatalog catalog, bool acceptLicense, IProgress<InstallProgress>? progress, CancellationToken token)
    {
        if (!acceptLicense) throw new InvalidOperationException("Android SDK 라이선스에 동의해야 설치할 수 있습니다.");
        foreach (var p in catalog.Packages)
        {
            bool known = AllPackages().Any(k => k.Id == p.Id && k.Folder == p.Folder);
            bool valid = p.HashType switch
            {
                "sha1" => ValidGoogle(p.Download) && Regex.IsMatch(p.Hash, "^[0-9a-f]{40}$"),
                "sha512" => ValidNuGet(p.Download) && Regex.IsMatch(p.Hash, "^[0-9a-f]{128}$"),
                _ => ValidJdk(p.Download) && Regex.IsMatch(p.Hash, "^[0-9a-f]{64}$"),
            };
            if (!known || !valid || p.Size < 1 || p.Size > MaxArchiveBytes) throw new InvalidDataException($"{p.Id} 의 배포 정보가 올바르지 않습니다.");
        }
        Directory.CreateDirectory(Root);
        if ((File.GetAttributes(Root) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("설치 경로에 링크가 있어 진행하지 않았습니다.");
        var todo = catalog.Packages.Where(p => !IsInstalled(p.Folder)).ToList();
        long total = Math.Max(1, todo.Sum(p => p.Size)), done = 0;
        var drive = new DriveInfo(Path.GetPathRoot(Root)!);
        if (drive.AvailableFreeSpace < total * 4) throw new IOException($"Android 도구를 설치할 디스크 공간이 부족합니다 (약 {total * 4 / 1073741824.0:N1} GB 필요).");
        // 라이선스 동의 기록 (sdkmanager 와 같은 형식: licenses/<id> = 라이선스 글의 SHA-1)
        var licenses = Path.Combine(Root, "sdk", "licenses");
        Directory.CreateDirectory(licenses);
        File.WriteAllText(Path.Combine(licenses, catalog.LicenseId), "\n" + Convert.ToHexString(SHA1.HashData(System.Text.Encoding.UTF8.GetBytes(catalog.License))).ToLowerInvariant());
        foreach (var p in todo)
        {
            token.ThrowIfCancellationRequested();
            string stage = Path.Combine(Root, ".install-" + Guid.NewGuid().ToString("N"));
            try
            {
                Directory.CreateDirectory(stage);
                string archive = Path.Combine(stage, "package.zip"), extracted = Path.Combine(stage, "payload");
                await Download(p, archive, received => progress?.Report(new($"{Label(p.Id)} 다운로드 중 · {(done + received) / 1048576.0:N0} / {total / 1048576.0:N0} MB",
                    (done + received) / (double)total * .9)), token);
                progress?.Report(new($"{Label(p.Id)} 압축 해제 중", (done + p.Size) / (double)total * .9));
                await Task.Run(() => Extract(archive, extracted, token), token);
                HubStore.WriteJson(Path.Combine(extracted, ".nova-package"), new { id = p.Id, hash = p.Hash, url = p.Download.ToString() });
                string final = Path.GetFullPath(Path.Combine(Root, p.Folder));
                HubStore.RequireInside(Root, final);
                if (Directory.Exists(final))
                {
                    // 표시 없는 (덜 끝난) 예전 폴더만 바꾼다
                    HubStore.RejectReparseTree(final);
                    Directory.Delete(final, true);
                }
                Directory.CreateDirectory(Path.GetDirectoryName(final)!);
                Directory.Move(extracted, final);
                done += p.Size;
            }
            finally
            {
                if (Directory.Exists(stage)) { HubStore.RequireInside(Root, stage); HubStore.RejectReparseTree(stage); Directory.Delete(stage, true); }
            }
        }
        progress?.Report(new("Android 빌드 지원 설치를 완료했습니다.", 1));
        return Status(catalog);
    }

    public static string Label(string id) => id switch
    {
        JdkId => "OpenJDK 17",
        MonoId => "C# 런타임 (Mono 8.0, Android x64)",
        "platform-tools" => "Android SDK Platform-Tools",
        "build-tools;36.0.0" => "Android SDK Build-Tools 36",
        "platforms;android-34" => "Android SDK Platform 34",
        "ndk;28.2.13676358" => "Android NDK 28",
        "cmake;3.22.1" => "CMake 3.22.1",
        _ => id,
    };

    async Task Download(AndroidPackage p, string file, Action<long> report, CancellationToken token)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromMinutes(120));
        using var response = await http.GetAsync(p.Download, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength is long expected && expected != p.Size) throw new InvalidDataException($"{p.Id}: 서버의 파일 크기가 목록과 다릅니다.");
        using var hash = IncrementalHash.CreateHash(p.HashType switch { "sha1" => HashAlgorithmName.SHA1, "sha512" => HashAlgorithmName.SHA512, _ => HashAlgorithmName.SHA256 });
        await using var input = await response.Content.ReadAsStreamAsync(timeout.Token);
        await using var output = new FileStream(file, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024, true);
        var buffer = new byte[1024 * 1024];
        long received = 0;
        var last = DateTime.MinValue;
        while (true)
        {
            int n = await input.ReadAsync(buffer, timeout.Token);
            if (n == 0) break;
            received += n;
            if (received > p.Size) throw new InvalidDataException($"{p.Id}: 다운로드가 예상 크기를 넘었습니다.");
            await output.WriteAsync(buffer.AsMemory(0, n), timeout.Token);
            hash.AppendData(buffer, 0, n);
            if ((DateTime.UtcNow - last).TotalMilliseconds >= 150) { report(received); last = DateTime.UtcNow; }
        }
        if (received != p.Size || !Convert.ToHexString(hash.GetHashAndReset()).Equals(p.Hash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"{Label(p.Id)} 다운로드 검증에 실패했습니다. 다시 시도하세요.");
    }

    // 압축 해제: 경로 탈출 · 링크 · 중복을 거절하고, 맨 위 폴더가 하나뿐이면 벗긴다 (android-ndk-r28c/ → ndk/28.2…)
    internal static void Extract(string archive, string target, CancellationToken token)
    {
        using var zip = ZipFile.OpenRead(archive);
        var names = zip.Entries.Select(e => e.FullName.Replace('\\', '/')).Where(n => n.Length > 0).ToArray();
        var tops = names.Select(n => n.Split('/')[0]).Distinct(StringComparer.Ordinal).ToArray();
        string prefix = tops.Length == 1 && names.All(n => n.Contains('/')) ? tops[0] + "/" : "";
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long size = 0;
        var files = new List<(ZipArchiveEntry Entry, string Path)>();
        foreach (var entry in zip.Entries)
        {
            token.ThrowIfCancellationRequested();
            var name = entry.FullName.Replace('\\', '/');
            if (name.Length == 0) continue;
            if (name.StartsWith('/') || name.Contains(':') || name.Split('/').Any(s => s is ".." or "." || s != s.TrimEnd(' ', '.')) ||
                ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000 || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("압축 파일에 사용할 수 없는 경로가 있습니다.");
            name = name[prefix.Length..];
            if (name.Length == 0) continue;
            var path = Path.GetFullPath(Path.Combine(target, name));
            HubStore.RequireInside(target, path);
            if (!seen.Add(path)) throw new InvalidDataException("압축 파일에 중복된 경로가 있습니다.");
            size = checked(size + entry.Length);
            if (size > MaxExpandedBytes || zip.Entries.Count > 500000) throw new InvalidDataException("압축 해제 크기가 너무 큽니다.");
            files.Add((entry, path));
        }
        if (new DriveInfo(Path.GetPathRoot(target)!).AvailableFreeSpace < size + 64 * 1024 * 1024) throw new IOException("압축을 풀 디스크 공간이 부족합니다.");
        Directory.CreateDirectory(target);
        foreach (var (entry, path) in files)
        {
            token.ThrowIfCancellationRequested();
            if (entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\')) { Directory.CreateDirectory(path); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var input = entry.Open();
            using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write);
            input.CopyTo(output);
        }
    }
}
