param([Parameter(Mandatory=$true)][string]$Archive, [Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
$workspace = Join-Path $env:RUNNER_TEMP 'qthrone-upgrade-test'
New-Item -ItemType Directory -Path $workspace -Force | Out-Null
$baseline = '1.0-beta.1'
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/$env:GITHUB_REPOSITORY/releases/tags/$baseline" -Headers @{Accept='application/vnd.github+json'}
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
try {
    $gui = Start-Process -FilePath $executable -ArgumentList '-tray' -WorkingDirectory $app -PassThru -WindowStyle Hidden
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 500
        $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
        $gui.Refresh()
        if ($gui.HasExited) { throw "Baseline GUI exited: $($gui.ExitCode)" }
    } until ($text.Contains('Core Has Successfully Connected to qThrone!') -or [DateTime]::UtcNow -gt $deadline)
    if (!$text.Contains('Core Has Successfully Connected to qThrone!')) { throw 'Baseline GUI/core startup timed out' }
    $oldProcessId = $gui.Id
    # Close only the isolated test instance and its own core before replacement.
    Get-CimInstance Win32_Process -Filter "ParentProcessId=$oldProcessId" | Where-Object { $_.Name -eq 'qThroneCore.exe' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Stop-Process -Id $oldProcessId -Force
    $gui.WaitForExit()
    Start-Sleep -Seconds 1
    python $fixture seed $app
    if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare the upgrade fixture' }
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
    if (!$updater.WaitForExit(60000)) { throw 'Baseline updater timed out' }
    if ($updater.ExitCode -ne 0) {
        if (Test-Path (Join-Path $app 'qThrone-update-error.log')) { Get-Content (Join-Path $app 'qThrone-update-error.log') }
        throw "Baseline updater failed: $($updater.ExitCode)"
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 500
        $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
    } until (($text.Contains("version   : $Version") -and $text.Contains('Core Has Successfully Connected to qThrone!')) -or [DateTime]::UtcNow -gt $deadline)
    if (!$text.Contains("version   : $Version") -or !$text.Contains('Core Has Successfully Connected to qThrone!')) { throw 'Updated GUI/core restart timed out' }
    if ((Get-FileHash -LiteralPath $executable).Hash -ne (Get-FileHash -LiteralPath $expectedExecutable).Hash) { throw 'Restarted executable differs from the new release' }
    python $fixture verify $app
    if ($LASTEXITCODE -ne 0) { throw 'User data was not preserved' }
    Write-Output "qThrone $baseline -> qThrone ${Version}: downloaded, replaced, restarted and preserved user data"
} finally {
    Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($app, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
