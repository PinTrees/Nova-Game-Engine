using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using NovaHub;

var root = Path.GetFullPath(args.FirstOrDefault(a => !a.StartsWith("--")) ?? Path.Combine(Path.GetTempPath(), "NovaHubTest-" + Guid.NewGuid().ToString("N")));
if (Directory.Exists(root)) throw new IOException("Use a fresh result directory.");
Directory.CreateDirectory(root);
var results = new List<object>(); int failures = 0;
async Task Check(string name, Func<Task> test)
{
    try { await test(); results.Add(new { name, result = "PASS" }); Console.WriteLine("PASS " + name); }
    catch (Exception e) { failures++; results.Add(new { name, result = "FAIL", detail = e.ToString() }); Console.WriteLine("FAIL " + name + ": " + e.Message); }
}
void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
async Task MustFail(Func<Task> test)
{ try { await test(); } catch (Exception e) when (e is IOException or InvalidDataException or OperationCanceledException or InvalidOperationException or ArgumentException) { return; } throw new Exception("Expected rejection."); }
byte[] Zip(params (string path, string text)[] files)
{
    using var stream = new MemoryStream();
    using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, true))
        foreach (var (path, text) in files) { using var writer = new StreamWriter(archive.CreateEntry(path).Open()); writer.Write(text); }
    return stream.ToArray();
}
var valid = Zip(("NOVA-Engine-0.1.0-win64/Binaries/NovaEngine.exe", "exe-fixture"), ("NOVA-Engine-0.1.0-win64/Binaries/NovaCore.dll", "dll-fixture"), ("NOVA-Engine-0.1.0-win64/Shaders/test.fx", "shader"));
EngineRelease Release(byte[] data, string version = "0.1.0") => new(version, "v" + version, new Uri($"https://github.com/PinTrees/Nova-Game-Engine/releases/download/v{version}/NOVA-Engine-{version}-win64.zip"), data.Length, Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant());
HubStore Store(string name) => new(Path.Combine(root, name, "State"), Path.Combine(root, name, "Editors"));
string Catalog(bool draft = false, bool prerelease = false, string digest = "", string url = "", string tag = "v0.1.0") => JsonSerializer.Serialize(new[] { new { draft, prerelease, tag_name = tag, assets = new[] { new { name = "NOVA-Engine-0.1.0-win64.zip", size = valid.Length, digest = digest.Length == 0 ? "sha256:" + Release(valid).Sha256 : digest, browser_download_url = url.Length == 0 ? Release(valid).Download.ToString() : url } } } });

await Check("Catalog discovers a checksummed Windows engine asset", () => { Assert(EngineInstaller.ParseCatalog(Catalog()).Single().Version == "0.1.0", "Missing version"); return Task.CompletedTask; });
await Check("Catalog excludes draft and preview releases", () => { Assert(EngineInstaller.ParseCatalog(Catalog(draft: true)).Count == 0 && EngineInstaller.ParseCatalog(Catalog(prerelease: true)).Count == 0, "Preview was included"); return Task.CompletedTask; });
await Check("Catalog rejects checksum, source URL and tag mismatches", () => { Assert(EngineInstaller.ParseCatalog(Catalog(digest: "bad")).Count == 0 && EngineInstaller.ParseCatalog(Catalog(url: "https://example.com/engine.zip")).Count == 0 && EngineInstaller.ParseCatalog(Catalog(tag: "v2.0.0")).Count == 0, "Unsafe release was included"); return Task.CompletedTask; });
await Check("Versions cannot supply filesystem paths", () => { Assert(!EngineInstaller.ValidVersion("../0.1.0") && !EngineInstaller.ValidVersion("0.1.0/other") && EngineInstaller.ValidVersion("0.1.0"), "Unsafe version accepted"); return Task.CompletedTask; });
await Check("Corrected release assets take priority without deleting previous releases", () =>
{
    var catalog = JsonNode.Parse(Catalog())!.AsArray(); var corrected = JsonNode.Parse(Catalog())![0]!.DeepClone();
    corrected["assets"]![0]!["name"] = "NOVA-Engine-0.1.0-win64-r1.zip";
    corrected["assets"]![0]!["browser_download_url"] = Release(valid).Download.ToString().Replace("-win64.zip", "-win64-r1.zip");
    catalog.Add(corrected); var release = EngineInstaller.ParseCatalog(catalog.ToJsonString()).Single();
    Assert(release.Revision == 1 && release.Download.ToString().EndsWith("-r1.zip"), "Old asset took priority"); return Task.CompletedTask;
});

var installedStore = Store("success"); InstalledEngine? installed = null;
await Check("Download, SHA256, extraction and registration complete atomically", async () =>
{
    using var installer = new EngineInstaller(installedStore, new FakeHandler(valid)); installed = await installer.InstallAsync(Release(valid), null, CancellationToken.None);
    Assert(installed.Available && installedStore.Engines.Count == 1 && File.Exists(Path.Combine(installed.Root, ".nova-install.json")), "Incomplete install");
    Assert(!Directory.EnumerateDirectories(installedStore.EngineRoot).Any(p => Path.GetFileName(p).StartsWith('.')), "Left partial folder");
});
await Check("Existing engine installation cannot be overwritten", async () => { using var installer = new EngineInstaller(installedStore, new FakeHandler(valid)); await MustFail(() => installer.InstallAsync(Release(valid), null, CancellationToken.None)); Assert(File.ReadAllText(installed!.Executable) == "exe-fixture", "Existing engine changed"); });
await Check("Installed engines survive Hub reload", () => { var reread = new HubStore(installedStore.StateRoot, installedStore.EngineRoot); Assert(reread.Engines.Single().Available, "Reload lost engine"); return Task.CompletedTask; });
await Check("Completed install can recover an interrupted registry write", () => { var recovery = new HubStore(Path.Combine(root, "recovery-state"), installedStore.EngineRoot); Assert(recovery.Engines.Single().Available, "Recovery failed"); return Task.CompletedTask; });

await Check("Checksum failure preserves old engines and clears temporary files", async () =>
{
    var existing = File.ReadAllBytes(installed!.Executable); using var installer = new EngineInstaller(installedStore, new FakeHandler(valid));
    await MustFail(() => installer.InstallAsync(Release(valid, "0.2.0") with { Sha256 = new string('a', 64) }, null, CancellationToken.None));
    Assert(existing.SequenceEqual(File.ReadAllBytes(installed.Executable)) && installedStore.Engines.Count == 1 && !Directory.Exists(Path.Combine(installedStore.EngineRoot, "0.2.0")), "Previous install changed");
});
await Check("HTTP failure leaves no installation registered", async () => { var store = Store("http-error"); using var installer = new EngineInstaller(store, new FakeHandler(valid, HttpStatusCode.NotFound)); bool failed = false; try { await installer.InstallAsync(Release(valid), null, CancellationToken.None); } catch (HttpRequestException) { failed = true; } Assert(failed && store.Engines.Count == 0 && !Directory.EnumerateDirectories(store.EngineRoot).Any(), "HTTP failure installed files"); });
await Check("Cancellation during download leaves no partial installation", async () => { var store = Store("cancel"); using var cts = new CancellationTokenSource(); using var installer = new EngineInstaller(store, new FakeHandler(valid, cancel: cts)); await MustFail(() => installer.InstallAsync(Release(valid), null, cts.Token)); Assert(store.Engines.Count == 0 && !Directory.EnumerateDirectories(store.EngineRoot).Any(), "Cancellation left files"); });
await Check("Short downloads cannot be installed", async () => { var store = Store("short"); using var installer = new EngineInstaller(store, new FakeHandler(valid[..^1], omitLength: true)); await MustFail(() => installer.InstallAsync(Release(valid), null, CancellationToken.None)); Assert(store.Engines.Count == 0, "Truncated archive registered"); });

foreach (var (name, file) in new[] { ("traversal", "NOVA-Engine-0.1.0-win64/../../outside.txt"), ("Windows trailing-space traversal", "NOVA-Engine-0.1.0-win64/.. /outside.txt"), ("absolute", "/outside.txt"), ("ADS", "NOVA-Engine-0.1.0-win64/Binaries/file:stream"), ("duplicate", "NOVA-Engine-0.1.0-win64/Binaries/NOVACORE.DLL") })
    await Check("Archive rejects " + name + " paths", async () => { var store = Store(name); byte[] zip = Zip(("NOVA-Engine-0.1.0-win64/Binaries/NovaEngine.exe", "exe"), ("NOVA-Engine-0.1.0-win64/Binaries/NovaCore.dll", "dll"), (file, "bad")); using var installer = new EngineInstaller(store, new FakeHandler(zip)); await MustFail(() => installer.InstallAsync(Release(zip), null, CancellationToken.None)); Assert(store.Engines.Count == 0 && !File.Exists(Path.Combine(root, "outside.txt")), "Unsafe archive installed"); });
await Check("Incomplete engine archives are rejected", async () => { var store = Store("missing-dll"); var zip = Zip(("Binaries/NovaEngine.exe", "exe")); using var installer = new EngineInstaller(store, new FakeHandler(zip)); await MustFail(() => installer.InstallAsync(Release(zip), null, CancellationToken.None)); Assert(store.Engines.Count == 0, "Missing DLL registered"); });
await Check("Flat engine archives are supported", async () => { var store = Store("flat"); var zip = Zip(("Binaries/NovaEngine.exe", "exe"), ("Binaries/NovaCore.dll", "dll")); using var installer = new EngineInstaller(store, new FakeHandler(zip)); var e = await installer.InstallAsync(Release(zip), null, CancellationToken.None); Assert(e.Available, "Flat archive failed"); });

HubProject? project = null;
await Check("Project creation records its selected engine and supports Korean paths", () => { project = installedStore.CreateProject("한글 게임", Path.Combine(root, "프로젝트 위치"), "0.1.0", "3D"); Assert(Directory.Exists(Path.Combine(project.Path, "Assets")) && project.EngineVersion == "0.1.0", "Project failed"); return Task.CompletedTask; });
await Check("Project launch quotes paths and uses the selected engine executable", () => { var info = installedStore.EditorStartInfo(project!, installed!); Assert(info.FileName == installed!.Executable && info.ArgumentList[0] == "--project" && info.ArgumentList[1] == project!.Path && info.WorkingDirectory == Path.GetDirectoryName(installed.Executable), "Launch targeted incorrect engine"); return Task.CompletedTask; });
await Check("Existing projects keep their recorded engine version", () => { var reopened = installedStore.AddProject(project!.Path); Assert(reopened.EngineVersion == "0.1.0", "Version lost"); return Task.CompletedTask; });
await Check("Opening with an incompatible engine is rejected", async () => { await MustFail(() => { installedStore.EditorStartInfo(project! with { EngineVersion = "2.0.0" }, installed!); return Task.CompletedTask; }); });
await Check("Project removal only removes the listing", () => { installedStore.RemoveProject(project!); Assert(Directory.Exists(project!.Path), "Project files deleted"); return Task.CompletedTask; });
await Check("Native Hub projects.json remains readable", () => { var native = Store("native-projects"); File.WriteAllText(Path.Combine(native.StateRoot, "projects.json"), JsonSerializer.Serialize(new { projects = new[] { new { name = "기존 게임", path = project!.Path, engineVersion = "0.1.0", template = "3D", lastOpened = 42L, favorite = true } } })); native.Reload(); Assert(native.Projects.Single().Favorite && native.Projects.Single().LastOpened == 42, "Native history lost"); return Task.CompletedTask; });
await Check("Malformed registry is preserved instead of replaced", () => { var folder = Path.Combine(root, "corrupt-state"); Directory.CreateDirectory(folder); var file = Path.Combine(folder, "projects.json"); File.WriteAllText(file, "{broken"); bool rejected = false; try { _ = new HubStore(folder); } catch (InvalidDataException) { rejected = true; } Assert(rejected && File.ReadAllText(file) == "{broken", "Corrupt history overwritten"); return Task.CompletedTask; });
await Check("Imported engine removal preserves external files", () => { var store = Store("external"); var external = new InstalledEngine("0.1.0", installed!.Root, false); store.Register(external); store.RemoveEngine(external); Assert(File.Exists(installed.Executable) && store.Engines.Count == 0, "Imported files deleted"); return Task.CompletedTask; });
await Check("Managed engine removal preserves project files", () => { installedStore.RemoveEngine(installed!); Assert(!Directory.Exists(installed!.Root) && Directory.Exists(project!.Path), "Removal touched project"); return Task.CompletedTask; });

if (args.Contains("--live"))
    await Check("Public release downloads, verifies and installs through the production path", async () =>
    {
        var store = Store("live"); using var installer = new EngineInstaller(store); using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(10));
        var releases = await installer.CatalogAsync(timeout.Token); Assert(releases.Count > 0, "No public engine release");
        var engine = await installer.InstallAsync(releases[0], new Progress<InstallProgress>(p => Console.WriteLine(p.Message)), timeout.Token);
        Assert(engine.Available && File.Exists(Path.Combine(engine.Root, "Shaders", "32. InstancedBasic.fx")), "Real engine missing files");
        File.WriteAllText(Path.Combine(root, "live-engine.json"), JsonSerializer.Serialize(engine));
    });
int archiveOption = Array.IndexOf(args, "--archive");
if (archiveOption >= 0 && archiveOption + 1 < args.Length)
    await Check("Packaged Release engine verifies and installs through the production path", async () =>
    {
        var bytes = await File.ReadAllBytesAsync(args[archiveOption + 1]); var store = Store("release-package");
        using var installer = new EngineInstaller(store, new FakeHandler(bytes));
        var engine = await installer.InstallAsync(Release(bytes), null, CancellationToken.None);
        Assert(engine.Available && File.Exists(Path.Combine(engine.Root, "Shaders", "32. InstancedBasic.fx")), "Real engine missing files");
        File.WriteAllText(Path.Combine(root, "installed-release-engine.json"), JsonSerializer.Serialize(engine));
    });

// ---- Android 빌드 지원 모듈 (OpenJDK · Android SDK · NDK)
string License = string.Concat(Enumerable.Repeat("Android Software Development Kit License Agreement. ", 10));
var sdkZips = AndroidToolsInstaller.SdkPackages.ToDictionary(p => p.Id, p => p.Id == "cmake;3.22.1"
    ? Zip(("bin/cmake.exe", "cmake"), ("share/readme.txt", "flat archive")) : Zip(("top-" + p.Id.Replace(';', '-') + "/source.properties", "Pkg.Revision=1")));
var jdkZip = Zip(("jdk-17.0.20.1+1/bin/java.exe", "java"));
string Sha1(byte[] b) => Convert.ToHexString(SHA1.HashData(b)).ToLowerInvariant();
string Sha256(byte[] b) => Convert.ToHexString(SHA256.HashData(b)).ToLowerInvariant();
string RepoXml(string urlOverride = "", string licenseId = "android-sdk-license") =>
    "<?xml version=\"1.0\"?><sdk:sdk-repository xmlns:sdk=\"http://schemas.android.com/sdk/android/repo/repository2/03\">" +
    $"<license id=\"android-sdk-license\" type=\"text\">{License}</license>" +
    string.Concat(AndroidToolsInstaller.SdkPackages.Select(p =>
        $"<remotePackage path=\"{p.Id}\"><uses-license ref=\"{licenseId}\"/><archives>" +
        $"<archive><complete><size>1</size><checksum type=\"sha1\">{new string('0', 40)}</checksum><url>linux.zip</url></complete><host-os>linux</host-os></archive>" +
        $"<archive><complete><size>{sdkZips[p.Id].Length}</size><checksum type=\"sha1\">{Sha1(sdkZips[p.Id])}</checksum><url>{(urlOverride.Length > 0 ? urlOverride : p.Id.Replace(';', '-') + "-win.zip")}</url></complete><host-os>windows</host-os></archive>" +
        "</archives></remotePackage>")) + "</sdk:sdk-repository>";
string JdkJson(string link = "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.20.1%2B1/OpenJDK17U-jdk_x64_windows_hotspot_17.0.20.1_1.zip") =>
    JsonSerializer.Serialize(new[] { new { binary = new { package = new { link, size = jdkZip.Length, checksum = Sha256(jdkZip), name = "jdk.zip" } } } });
MapHandler AndroidServer(Func<string, byte[]?>? overrideFile = null) => new(url =>
{
    if (url.EndsWith("repository2-3.xml")) return Encoding.UTF8.GetBytes(RepoXml());
    if (url.StartsWith("https://api.adoptium.net/")) return Encoding.UTF8.GetBytes(JdkJson());
    if (overrideFile?.Invoke(url) is { } custom) return custom;
    if (url.Contains("/adoptium/")) return jdkZip;
    var hit = AndroidToolsInstaller.SdkPackages.FirstOrDefault(p => url.EndsWith(p.Id.Replace(';', '-') + "-win.zip"));
    return hit.Id is null ? null : sdkZips[hit.Id];
});

await Check("Android catalog reads Windows SDK packages and the license", () =>
{
    var (packages, id, license) = AndroidToolsInstaller.ParseRepository(RepoXml());
    Assert(packages.Count == AndroidToolsInstaller.SdkPackages.Length && id == "android-sdk-license" && license.Contains("License Agreement"), "Catalog incomplete");
    Assert(packages.All(p => p.Download.Host == "dl.google.com" && !p.Download.ToString().Contains("linux")), "Wrong archive picked"); return Task.CompletedTask;
});
await Check("Android catalog rejects unsafe archive names and JDK sources", async () =>
{
    await MustFail(() => { AndroidToolsInstaller.ParseRepository(RepoXml("../evil.zip")); return Task.CompletedTask; });
    await MustFail(() => { AndroidToolsInstaller.ParseRepository(RepoXml("https://example.com/x.zip")); return Task.CompletedTask; });
    await MustFail(() => { AndroidToolsInstaller.ParseJdk(JdkJson("https://example.com/adoptium/jdk.zip")); return Task.CompletedTask; });
    await MustFail(() => { AndroidToolsInstaller.ParseRepository(RepoXml(licenseId: "missing-license")); return Task.CompletedTask; });
});
await Check("Android install requires accepting the SDK license", async () =>
{
    var dir = Path.Combine(root, "android-nolicense"); using var android = new AndroidToolsInstaller(dir, AndroidServer());
    var catalog = await android.CatalogAsync(CancellationToken.None);
    await MustFail(() => android.InstallAsync(catalog, false, null, CancellationToken.None));
    Assert(!Directory.Exists(Path.Combine(dir, "sdk")) && !Directory.Exists(Path.Combine(dir, "jdk")), "Installed without license");
});
var androidDir = Path.Combine(root, "android-ok");
await Check("Android tools download, verify and install into the shared folder", async () =>
{
    var server = AndroidServer(); using var android = new AndroidToolsInstaller(androidDir, server);
    var catalog = await android.CatalogAsync(CancellationToken.None);
    var status = await android.InstallAsync(catalog, true, null, CancellationToken.None);
    Assert(status.Installed && status.Missing.Count == 0, "Status not installed");
    Assert(File.Exists(Path.Combine(androidDir, "jdk", "bin", "java.exe")) && File.Exists(Path.Combine(androidDir, "sdk", "ndk", "28.2.13676358", "source.properties")) &&
           File.Exists(Path.Combine(androidDir, "sdk", "cmake", "3.22.1", "bin", "cmake.exe")), "Folder layout wrong (top folder strip / flat archive)");
    Assert(File.ReadAllText(Path.Combine(androidDir, "sdk", "licenses", "android-sdk-license")).Trim() == Sha1(Encoding.UTF8.GetBytes(License)), "License record missing");
    Assert(!Directory.EnumerateDirectories(androidDir).Any(d => Path.GetFileName(d).StartsWith(".install-")), "Stage folder left");
});
await Check("Installed Android packages are not downloaded again", async () =>
{
    var server = AndroidServer(); using var android = new AndroidToolsInstaller(androidDir, server);
    var catalog = await android.CatalogAsync(CancellationToken.None); int before = server.Requests;
    await android.InstallAsync(catalog, true, null, CancellationToken.None);
    Assert(server.Requests == before, "Re-downloaded installed packages");
});
await Check("Android checksum failure installs nothing for that package", async () =>
{
    var dir = Path.Combine(root, "android-badhash");
    var server = AndroidServer(url => url.EndsWith("ndk-28.2.13676358-win.zip") ? Zip(("x/tampered.txt", "tampered")) : null);
    using var android = new AndroidToolsInstaller(dir, server);
    var catalog = await android.CatalogAsync(CancellationToken.None);
    await MustFail(() => android.InstallAsync(catalog, true, null, CancellationToken.None));
    Assert(!Directory.Exists(Path.Combine(dir, "sdk", "ndk")) && !android.Status().Installed, "Tampered package installed");
    Assert(!Directory.EnumerateDirectories(dir).Any(d => Path.GetFileName(d).StartsWith(".install-")), "Stage folder left");
});
await Check("Android root defaults next to the engine root", () =>
{
    Assert(AndroidToolsInstaller.DefaultRoot(@"C:\Users\x\AppData\Local\NOVA\Editors") == @"C:\Users\x\AppData\Local\NOVA\AndroidTools", "Unexpected root"); return Task.CompletedTask;
});
if (args.Contains("--live"))
    await Check("Public Android catalog (Google · Adoptium) lists every package with a checksum", async () =>
    {
        using var android = new AndroidToolsInstaller(Path.Combine(root, "android-live"));
        var catalog = await android.CatalogAsync(CancellationToken.None);
        Assert(catalog.Packages.Count == AndroidToolsInstaller.SdkPackages.Length + 1 && catalog.License.Length > 1000, "Live catalog incomplete");
        File.WriteAllText(Path.Combine(root, "android-live-catalog.json"), JsonSerializer.Serialize(catalog.Packages.Select(p => new { p.Id, url = p.Download.ToString(), p.Size, p.HashType })));
        Console.WriteLine($"  Android download total {catalog.Packages.Sum(p => p.Size) / 1048576.0:N0} MB");
    });

File.WriteAllText(Path.Combine(root, "results.json"), JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"{results.Count - failures}/{results.Count} passed. Results: {root}");
return failures == 0 ? 0 : 1;

sealed class FakeHandler(byte[] bytes, HttpStatusCode status = HttpStatusCode.OK, CancellationTokenSource? cancel = null, bool omitLength = false) : HttpMessageHandler
{
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
    {
        if (cancel is not null) cancel.Cancel();
        HttpContent content = omitLength ? new StreamContent(new MemoryStream(bytes)) : new ByteArrayContent(bytes);
        return Task.FromResult(new HttpResponseMessage(status) { Content = content });
    }
}

sealed class MapHandler(Func<string, byte[]?> files) : HttpMessageHandler
{
    public int Requests;
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
    {
        Interlocked.Increment(ref Requests);
        var bytes = files(request.RequestUri!.ToString());
        return Task.FromResult(bytes is null ? new HttpResponseMessage(HttpStatusCode.NotFound) : new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(bytes) });
    }
}
