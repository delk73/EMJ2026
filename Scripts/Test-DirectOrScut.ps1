[CmdletBinding()]
param(
    [string]$Tests = 'DirectOrScut.PIE',
    [string]$ReportName = 'Consolidated',
    [ValidateRange(30,1200)][int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$projectPath = Join-Path $projectRoot 'EMJ2026.uproject'
$association = (Get-Content $projectPath -Raw | ConvertFrom-Json).EngineAssociation
$editor = Join-Path ${env:ProgramFiles} "Epic Games/UE_$association/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor before standalone automation.'
}
if ($ReportName -notmatch '^[A-Za-z0-9_-]+$' -or $Tests -notmatch '^[A-Za-z0-9_.+]+$') {
    throw 'Use automation prefixes separated by + and an alphanumeric report name.'
}
$logPath = Join-Path $projectRoot "Saved/Logs/$ReportName.log"
$reportPath = Join-Path $projectRoot "Saved/DirectOrScut/${ReportName}Report"
$arguments = '"{0}" -unattended -nop4 -nosplash -RenderOffscreen -ResX=1600 -ResY=1000 -abslog="{1}" -ExecCmds="Automation RunTests {2}" -TestExit="Automation Test Queue Empty" -ReportExportPath="{3}"' -f $projectPath,$logPath,$Tests,$reportPath
$startedAt = [DateTime]::UtcNow
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
$processHandle = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    & taskkill.exe /PID $process.Id /T /F | Out-Null
    throw "Automation timed out after $TimeoutSeconds seconds: $logPath"
}
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Automation exited $($process.ExitCode): $logPath" }
$reportFile = Get-Item (Join-Path $reportPath 'index.json') -ErrorAction SilentlyContinue
if (!$reportFile -or $reportFile.LastWriteTimeUtc -lt $startedAt) {
    throw "Fresh automation report missing: $reportPath"
}
$report = Get-Content -LiteralPath $reportFile.FullName -Raw | ConvertFrom-Json
if ($report.failed -gt 0 -or $report.succeeded -lt 1 -or $report.notRun -gt 0) {
    throw "Automation incomplete: passed=$($report.succeeded), failed=$($report.failed), notRun=$($report.notRun). See $reportPath"
}
Write-Host "Automation passed: $($report.succeeded) tests. Report: $reportPath"
