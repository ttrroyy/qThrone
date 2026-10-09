param([Parameter(Mandatory=$true)][string]$Archive, [Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
$workspace = Join-Path $env:RUNNER_TEMP 'qthrone-upgrade-test'
New-Item -ItemType Directory -Path $workspace -Force | Out-Null
$releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$env:GITHUB_REPOSITORY/releases?per_page=100" -Headers @{Accept='application/vnd.github+json'}
$release = $releases | Where-Object { !$_.draft -and $_.tag_name -ne $Version -and ($_.assets.name -contains "qThrone-$($_.tag_name)-windows64.zip") } | Sort-Object published_at -Descending | Select-Object -First 1
if (!$release) { throw 'A previous published portable release is required' }
$baseline = $release.tag_name
$asset = $release.assets | Where-Object { $_.name -eq "qThrone-$baseline-windows64.zip" }
if (!$asset) { throw 'The baseline portable release is missing' }
$oldArchive = Join-Path $workspace 'baseline.zip'
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $oldArchive
if ($asset.digest -and $asset.digest.StartsWith('sha256:')) {
    if ((Get-FileHash -LiteralPath $oldArchive -Algorithm SHA256).Hash.ToLower() -ne $asset.digest.Substring(7)) { throw 'Baseline archive digest mismatch' }
}
Expand-Archive -LiteralPath $oldArchive -DestinationPath $workspace
$app = Join-Path $workspace 'qThrone'
$executable = Join-Path $app 'qThrone.exe'
$log = Join-Path $app 'config/logs/throne.log'
$fixture = Join-Path $PSScriptRoot 'upgrade_fixture.py'
$newArchive = (Resolve-Path -LiteralPath $Archive).Path
Expand-Archive -LiteralPath $newArchive -DestinationPath (Join-Path $workspace 'expected')
$expectedExecutable = Join-Path $workspace 'expected/qThrone/qThrone.exe'
$gui = $null
$updater = $null
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class UpgradeTestWindow {
    private delegate bool WindowCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(WindowCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    public static bool Close(uint processId) {
        bool sent = false;
        EnumWindows((window, parameter) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != processId) return true;
            var title = new StringBuilder(1024);
            GetWindowText(window, title, title.Capacity);
            if (!title.ToString().Contains("qThrone")) return true;
            sent = PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
            return !sent;
        }, IntPtr.Zero);
        return sent;
    }
}
'@
function Wait-ForGuiAndCore([Diagnostics.Process]$process) {
    # Keep a Windows process handle while it is alive, including detached GUI instances.
    $null = $process.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) { throw "GUI exited: $($process.ExitCode)" }
        $core = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" | Where-Object { $_.Name -eq 'qThroneCore.exe' }
    } until ($core -or [DateTime]::UtcNow -gt $deadline)
    if (!$core) { throw 'GUI/core process startup timed out' }
    Write-Output "Started GUI PID $($process.Id), core PID $($core.ProcessId)"
    Start-Sleep -Seconds 3
}
function Close-TestGui([Diagnostics.Process]$process) {
    $process.Refresh()
    # MainWindowHandle excludes hidden windows; enumerate only this test process.
    if (![UpgradeTestWindow]::Close($process.Id) -or !$process.WaitForExit(30000)) { throw 'GUI did not shut down normally' }
    if ($process.ExitCode -ne 0) { throw "GUI shutdown failed: $($process.ExitCode)" }
}
try {
    python $fixture prepare $app
    if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare isolated portable settings' }
    $gui = Start-Process -FilePath $executable -WorkingDirectory $app -PassThru -WindowStyle Hidden
    Wait-ForGuiAndCore $gui
    $oldProcessId = $gui.Id
    # A normal shutdown flushes the buffered startup log before assertions.
    Close-TestGui $gui
    $text = Get-Content -LiteralPath $log -Raw
    if (!$text.Contains('Core Has Successfully Connected to qThrone!')) { throw 'Baseline GUI/core IPC connection failed' }
    python $fixture seed $app
    if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare the upgrade fixture' }
    foreach ($sourceVersion in @($baseline, $Version)) {
        Copy-Item -LiteralPath $newArchive -Destination (Join-Path $app 'qThrone.zip')
        $oldUpdater = Join-Path $app 'qThroneUpdater.old'
        Copy-Item -LiteralPath (Join-Path $app 'qThroneUpdater.exe') -Destination $oldUpdater
        $start = [Diagnostics.ProcessStartInfo]::new($oldUpdater)
        $start.WorkingDirectory = $app
        $start.Arguments = "$oldProcessId"
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $updater = [Diagnostics.Process]::Start($start)
        if (!$updater.WaitForExit(60000)) { throw "Updater from $sourceVersion timed out" }
        if ($updater.ExitCode -ne 0) {
            if (Test-Path (Join-Path $app 'qThrone-update-error.log')) { Get-Content (Join-Path $app 'qThrone-update-error.log') }
            throw "Updater from $sourceVersion failed: $($updater.ExitCode)"
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            Start-Sleep -Milliseconds 500
            $restarted = Get-CimInstance Win32_Process -Filter "Name='qThrone.exe'" | Where-Object { $_.ExecutablePath -eq $executable }
        } until ($restarted -or [DateTime]::UtcNow -gt $deadline)
        if (!$restarted) { throw 'Updated GUI restart timed out' }
        $gui = [Diagnostics.Process]::GetProcessById($restarted.ProcessId)
        Wait-ForGuiAndCore $gui
        Close-TestGui $gui
        $text = Get-Content -LiteralPath $log -Raw
        if (!$text.Contains("version   : $Version") -or !$text.Contains('Core Has Successfully Connected to qThrone!')) { throw 'Updated GUI/core IPC connection failed' }
        if ((Get-FileHash -LiteralPath $executable).Hash -ne (Get-FileHash -LiteralPath $expectedExecutable).Hash) { throw 'Restarted executable differs from the new release' }
        python $fixture verify $app
        if ($LASTEXITCODE -ne 0) { throw 'User data was not preserved' }
        Write-Output "qThrone $sourceVersion -> qThrone ${Version}: replaced, restarted and preserved user data"
        $oldProcessId = $gui.Id
    }
} catch {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 80 }
    Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($app, [StringComparison]::OrdinalIgnoreCase) } | Select-Object Name,ProcessId,ParentProcessId
    throw
} finally {
    Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($app, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
