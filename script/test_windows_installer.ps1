param([Parameter(Mandatory=$true)][string]$Installer)
$ErrorActionPreference = 'Stop'
$setup = (Resolve-Path -LiteralPath $Installer).Path
$workspace = [IO.Path]::GetFullPath((Join-Path $env:RUNNER_TEMP 'rawline-installer-test'))
$app = Join-Path $workspace 'Rawline'
$localData = Join-Path $env:LOCALAPPDATA 'Rawline'
$roamingData = Join-Path $env:APPDATA 'Rawline'
if ((Test-Path -LiteralPath $app) -or (Test-Path -LiteralPath $localData) -or (Test-Path -LiteralPath $roamingData)) {
    throw 'Installer test requires an isolated runner without Rawline data'
}
New-Item -ItemType Directory -Path $workspace -Force | Out-Null
$sentinel = Join-Path $workspace 'unrelated.txt'
Set-Content -LiteralPath $sentinel -Value 'keep'
foreach ($language in @('en', 'ru', 'zh_CN', 'fa')) {
    $installed = Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/LANG=$language", "/DIR=`"$app`"", "/LOG=`"$workspace\install-$language.log`"") -Wait -PassThru -WindowStyle Hidden
    if ($installed.ExitCode -ne 0 -or !(Test-Path -LiteralPath (Join-Path $app 'Rawline.exe'))) { throw "Install failed: $language ($($installed.ExitCode))" }
    foreach ($directory in @((Join-Path $app 'config\logs'), (Join-Path $app 'runtime-cache'), (Join-Path $localData 'qwdtt'), (Join-Path $roamingData 'config'))) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $directory 'fixture.txt') -Value 'runtime data'
    }
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    New-Item -Path $runKey -Force | Out-Null
    New-ItemProperty -Path $runKey -Name Rawline -Value "`"$app\Rawline.exe`" -tray" -PropertyType String -Force | Out-Null
    $taskName = "Rawline AutoRun installer-test-$language"
    $action = New-ScheduledTaskAction -Execute (Join-Path $app 'Rawline.exe')
    Register-ScheduledTask -TaskName $taskName -Action $action -Force | Out-Null
    $removed = Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$workspace\uninstall-$language.log`"") -Wait -PassThru -WindowStyle Hidden
    if ($removed.ExitCode -ne 0) { throw "Uninstall failed: $language ($($removed.ExitCode))" }
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ((Test-Path -LiteralPath $app) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200 }
    foreach ($directory in @($app, $localData, $roamingData)) {
        if (Test-Path -LiteralPath $directory) { throw "Uninstall left data: $directory" }
    }
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) { throw 'Autorun task was not removed' }
    if (Get-ItemProperty -Path $runKey -Name Rawline -ErrorAction SilentlyContinue) { throw 'Autorun registry entry was not removed' }
    if (!(Test-Path -LiteralPath $sentinel)) { throw 'Uninstall removed an unrelated file' }
    Write-Output "PASS: $language installation and complete owned-data removal"
}
