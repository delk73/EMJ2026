[CmdletBinding()]
param(
    [switch]$Capture,
    [switch]$Bake,
    [switch]$PIE,
    [ValidateRange(640,3840)][int]$Width = 1600,
    [ValidateRange(360,2160)][int]$Height = 1000,
    [ValidateRange(1,8)][int]$Feed = 1,
    [ValidateRange(30,1200)][int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
if ($PIE) {
    & (Join-Path $PSScriptRoot 'Test-DirectOrScut.ps1') -TimeoutSeconds $TimeoutSeconds
    return
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$projectPath = Join-Path $projectRoot 'EMJ2026.uproject'
$association = (Get-Content $projectPath -Raw | ConvertFrom-Json).EngineAssociation
$editor = Join-Path ${env:ProgramFiles} "Epic Games/UE_$association/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) { throw 'Close Unreal Editor before standalone verification.' }
$operation = if ($Bake) { 'Bake' } elseif ($Capture) { 'Capture' } else { 'Runtime' }
$logPath = Join-Path $projectRoot "Saved/Logs/DirectOrScut$operation.log"
$arguments = '"{0}" -unattended -nop4 -nosplash -abslog="{1}"' -f $projectPath,$logPath
if ($Bake) {
    $arguments += ' -run=DirectOrScutBake -NullRHI'
} elseif ($Capture) {
    $arguments += ' /Game/Maps/DirectOrScut_Soundstage -game -RenderOffscreen -ResX={0} -ResY={1} -ScutCapture -ScutFeed={2}' -f $Width,$Height,$Feed
} else {
    $arguments += ' /Game/DirectOrScut/Maps/DirectOrScutBlockout -game -NullRHI -ScutValidate'
}
$startedAt = [DateTime]::UtcNow
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
$processHandle = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    & taskkill.exe /PID $process.Id /T /F | Out-Null
    throw "Verification timed out: $logPath"
}
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Verification exited $($process.ExitCode): $logPath" }
if ($Capture) {
    $preview = Get-Item (Join-Path $projectRoot 'Saved/DirectOrScut/Workstation.png') -ErrorAction SilentlyContinue
    if (!$preview -or $preview.LastWriteTimeUtc -lt $startedAt) { throw "Fresh screenshot missing: $logPath" }
} elseif (!$Bake -and !(Select-String -LiteralPath $logPath -SimpleMatch 'SCUT VALIDATION PASS' -Quiet)) {
    throw "Runtime checks did not pass: $logPath"
}
Write-Host "Completed $operation. Log: $logPath"
