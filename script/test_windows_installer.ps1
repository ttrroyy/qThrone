param([Parameter(Mandatory=$true)][string]$Installer)
$ErrorActionPreference = 'Stop'
$setup = (Resolve-Path -LiteralPath $Installer).Path
$workspace = [IO.Path]::GetFullPath((Join-Path $env:RUNNER_TEMP 'qthrone-installer-test'))
$app = Join-Path $workspace 'qThrone'
$localData = Join-Path $env:LOCALAPPDATA 'qThrone'
$roamingData = Join-Path $env:APPDATA 'qThrone'
if ((Test-Path -LiteralPath $app) -or (Test-Path -LiteralPath $localData) -or (Test-Path -LiteralPath $roamingData)) {
    throw 'Installer test requires an isolated runner without qThrone data'
}
New-Item -ItemType Directory -Path $workspace -Force | Out-Null
$sentinel = Join-Path $workspace 'unrelated.txt'
Set-Content -LiteralPath $sentinel -Value 'keep'
foreach ($language in @('en', 'ru', 'zh_CN', 'fa')) {
    $installed = Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/LANG=$language", "/DIR=`"$app`"", "/LOG=`"$workspace\install-$language.log`"") -Wait -PassThru -WindowStyle Hidden
    if ($installed.ExitCode -ne 0 -or !(Test-Path -LiteralPath (Join-Path $app 'qThrone.exe'))) { throw "Install failed: $language ($($installed.ExitCode))" }
    foreach ($directory in @((Join-Path $app 'config\logs'), (Join-Path $app 'runtime-cache'), (Join-Path $localData 'qwdtt'), (Join-Path $roamingData 'config'))) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $directory 'fixture.txt') -Value 'runtime data'
    }
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    New-Item -Path $runKey -Force | Out-Null
    New-ItemProperty -Path $runKey -Name qThrone -Value "`"$app\qThrone.exe`" -tray" -PropertyType String -Force | Out-Null
    $taskName = "qThrone AutoRun installer-test-$language"
    $action = New-ScheduledTaskAction -Execute (Join-Path $app 'qThrone.exe')
    Register-ScheduledTask -TaskName $taskName -Action $action -Force | Out-Null
    $removed = Start-Process -FilePath (Join-Path $app 'unins000.exe') -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$workspace\uninstall-$language.log`"") -Wait -PassThru -WindowStyle Hidden
    if ($removed.ExitCode -ne 0) { throw "Uninstall failed: $language ($($removed.ExitCode))" }
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ((Test-Path -LiteralPath $app) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200 }
    foreach ($directory in @($app, $localData, $roamingData)) {
        if (Test-Path -LiteralPath $directory) { throw "Uninstall left data: $directory" }
    }
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) { throw 'Autorun task was not removed' }
    if (Get-ItemProperty -Path $runKey -Name qThrone -ErrorAction SilentlyContinue) { throw 'Autorun registry entry was not removed' }
    if (!(Test-Path -LiteralPath $sentinel)) { throw 'Uninstall removed an unrelated file' }
    Write-Output "PASS: $language installation and complete owned-data removal"
}
