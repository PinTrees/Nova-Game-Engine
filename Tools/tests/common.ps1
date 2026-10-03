# NOVA 자동 검사 공용 (run_tests.ps1 이 dot-source 한다).
#  - 테스트 에디터를 백그라운드로 띄우고 VRAM·로그 감시를 건다 (이상하면 그 에디터만 끈다 — 사용자의 다른 에디터는 건드리지 않는다)
#  - nova 명령 실행, 레이아웃 파일(Binaries/nova_layout_v2.ini, 사용자 에디터와 공유) 백업·복원, 이미지 비교(C#, 파이썬 없이)
$ErrorActionPreference = 'Continue'
$script:Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:Nova = Join-Path $Root 'Binaries\nova.exe'
$script:EditorLog = Join-Path $Root 'Binaries\Logs\Editor.log'
$script:LayoutIni = Join-Path $Root 'Binaries\nova_layout_v2.ini'

function Get-VramMB
{
    try { [int]((& nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits 2>$null | Select-Object -First 1).Trim()) } catch { 0 }
}

# 에디터를 띄우고 감시 작업을 건다. 돌려주는 객체로 Invoke-Nova / Stop-TestEditor
function Start-TestEditor([switch]$OpenGL, [switch]$Vulkan, [int]$MaxGrowMB = 1500, [int]$WatchSeconds = 900)
{
    $base = Get-VramMB
    if ($OpenGL) { $out = & $Nova open $script:Project --background --graphics opengl --timeout 300 2>&1 | Out-String }
    elseif ($Vulkan) { $out = & $Nova open $script:Project --background --graphics vulkan --timeout 300 2>&1 | Out-String }
    else { $out = & $Nova open $script:Project --background --timeout 300 2>&1 | Out-String }
    if ($out -notmatch 'pid (\d+)') { throw "editor did not open: $out" }
    $editorPid = [int]$Matches[1]
    $job = Start-Job -ArgumentList $editorPid, $base, $MaxGrowMB, $WatchSeconds, $EditorLog -ScriptBlock {
        param($editorPid, $base, $maxGrow, $seconds, $log)
        $sw = [Diagnostics.Stopwatch]::StartNew(); $peak = $base
        while ($sw.Elapsed.TotalSeconds -lt $seconds)
        {
            Start-Sleep -Milliseconds 250
            if (-not (Get-Process -Id $editorPid -ErrorAction SilentlyContinue)) { return "editor closed, VRAM peak +$($peak - $base) MB" }
            $v = 0; try { $v = [int]((& nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | Select-Object -First 1).Trim()) } catch { }
            if ($v -gt $peak) { $peak = $v }
            if ($base -gt 0 -and $v - $base -gt $maxGrow) { Stop-Process -Id $editorPid -Force; return "STOP: VRAM +$($v - $base) MB - editor killed" }
            $bad = Select-String -Path $log -Pattern 'device removed|DEVICE_REMOVED|device lost|Present failed|VRAM budget: refused|\[CRASH\]' -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($bad) { Stop-Process -Id $editorPid -Force; return "STOP: $($bad.Line) - editor killed" }
        }
        return "watch ended, VRAM peak +$($peak - $base) MB"
    }
    [pscustomobject]@{ Pid = $editorPid; Job = $job; OpenGL = [bool]$OpenGL }
}

function Test-EditorAlive($editor) { [bool](Get-Process -Id $editor.Pid -ErrorAction SilentlyContinue) }

# nova 명령 한 줄 (셸처럼 "…" 로 묶기). 돌려주는 값 = 출력 글자 (오류 포함)
function Invoke-Nova([string]$line)
{
    $parts = @($line -split ' (?=(?:[^"]*"[^"]*")*[^"]*$)' | ForEach-Object { $_.Trim('"') })
    (& $Nova @parts --project $script:Project 2>&1 | Out-String).TrimEnd()
}

# JSON 결과 (--json)
function Invoke-NovaJson([string]$line)
{
    $t = Invoke-Nova "$line --json"
    try { $t | ConvertFrom-Json } catch { $null }
}

# 에디터 닫기 (Scene 탭을 앞으로 되돌린 뒤 — 레이아웃 파일은 사용자 에디터와 공유) + 감시 결과
function Stop-TestEditor($editor)
{
    if (Test-EditorAlive $editor)
    {
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        & $Nova quit --force --project $script:Project 2>&1 | Out-Null
        for ($i = 0; $i -lt 40 -and (Test-EditorAlive $editor); $i++) { Start-Sleep -Milliseconds 250 }
    }
    Stop-Job $editor.Job -ErrorAction SilentlyContinue
    $r = Receive-Job $editor.Job -ErrorAction SilentlyContinue
    Remove-Job $editor.Job -Force -ErrorAction SilentlyContinue
    if ($r) { $r } else { 'watch ok' }
}

function Backup-Layout { if (Test-Path $LayoutIni) { Copy-Item $LayoutIni "$LayoutIni.testbackup" -Force } }
function Restore-Layout { if (Test-Path "$LayoutIni.testbackup") { Copy-Item "$LayoutIni.testbackup" $LayoutIni -Force; Remove-Item "$LayoutIni.testbackup" -Force } }

# ---- 이미지 비교: 최대 차이, 평균, 8 넘게 다른 픽셀 비율(%). 크기가 다르면 $null. diffPng 를 주면 차이 그림(×8)
if (-not ('NovaImageCompare' -as [type]))
{
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class NovaImageCompare
{
    static byte[] Pixels(Bitmap b)
    {
        var r = new Rectangle(0, 0, b.Width, b.Height);
        BitmapData d = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        byte[] px = new byte[d.Stride * b.Height];
        Marshal.Copy(d.Scan0, px, 0, px.Length);
        b.UnlockBits(d);
        return px;
    }
    public static double[] Compare(string pathA, string pathB, string diffPng)
    {
        using (var a = new Bitmap(pathA))
        using (var b = new Bitmap(pathB))
        {
            if (a.Width != b.Width || a.Height != b.Height) return null;
            byte[] pa = Pixels(a), pb = Pixels(b);
            int max = 0, over = 0; double sum = 0; int n = a.Width * a.Height;
            byte[] pd = !string.IsNullOrEmpty(diffPng) ? new byte[pa.Length] : null;   // PowerShell 의 $null 은 "" 로 온다
            for (int i = 0; i < pa.Length; i += 4)
            {
                int m = 0;
                for (int c = 0; c < 3; ++c)
                {
                    int v = Math.Abs(pa[i + c] - pb[i + c]);
                    sum += v;
                    if (v > m) m = v;
                    if (pd != null) pd[i + c] = (byte)Math.Min(255, v * 8);
                }
                if (pd != null) pd[i + 3] = 255;
                if (m > max) max = m;
                if (m > 8) over++;
            }
            if (pd != null)
            {
                using (var o = new Bitmap(a.Width, a.Height, PixelFormat.Format32bppArgb))
                {
                    BitmapData d = o.LockBits(new Rectangle(0, 0, o.Width, o.Height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                    Marshal.Copy(pd, 0, d.Scan0, pd.Length);
                    o.UnlockBits(d);
                    o.Save(diffPng, ImageFormat.Png);
                }
            }
            return new double[] { max, sum / (3.0 * n), 100.0 * over / n };
        }
    }
}
"@
}

# ---- 결과 모으기
$script:Results = New-Object System.Collections.Generic.List[object]
function Add-Result([string]$suite, [string]$name, [bool]$pass, [string]$detail)
{
    $script:Results.Add([pscustomobject]@{ Suite = $suite; Test = $name; Result = $(if ($pass) { 'PASS' } else { 'FAIL' }); Detail = $detail })
    $mark = if ($pass) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1} — {2}" -f $mark, $name, $detail)
}
