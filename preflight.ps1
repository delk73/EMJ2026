[CmdletBinding()]
param(
    [switch]$SkipSmokeTest,
    [ValidateRange(30,3600)][int]$BuildTimeoutSeconds = 600,
    [ValidateRange(10,600)][int]$SmokeTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$projectPath = Join-Path $PSScriptRoot 'EMJ2026.uproject'
$logDirectory = Join-Path $PSScriptRoot 'Saved/Logs'

function Invoke-BoundedProcess {
    param([string]$FilePath, [string]$Arguments, [int]$TimeoutSeconds, [string]$LogStem)
    $stdout = Join-Path $logDirectory "$LogStem.stdout.log"
    $stderr = Join-Path $logDirectory "$LogStem.stderr.log"
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -WorkingDirectory $PSScriptRoot `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    # Retain a process handle so Windows PowerShell can read ExitCode after redirected execution.
    $processHandle = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        # Only terminate this invocation and its children, never unrelated editor/build sessions.
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw "$LogStem timed out after $TimeoutSeconds seconds. Logs: $stdout and $stderr"
    }
    $process.WaitForExit()
    Get-Content -LiteralPath $stdout | Write-Host
    if ((Get-Item -LiteralPath $stderr).Length -gt 0) { Get-Content -LiteralPath $stderr | Write-Host }
    if ($process.ExitCode -ne 0) { throw "$LogStem exited $($process.ExitCode). Logs: $stdout and $stderr" }
}

try {
    & (Join-Path $PSScriptRoot "Scripts/Test-SourceArchitecture.ps1")
    if (Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
        Write-Warning 'Unreal Editor is open. Save your work and close it before preflight. No processes changed; build and smoke test skipped.'
        exit 0
    }
    $project = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
    $association = [string]$project.EngineAssociation
    $candidates = @()
    $builds = Get-ItemProperty 'HKCU:/Software/Epic Games/Unreal Engine/Builds' -ErrorAction SilentlyContinue
    if ($builds -and $builds.PSObject.Properties[$association]) { $candidates += $builds.$association }
    $installed = Get-ItemProperty "HKLM:/SOFTWARE/EpicGames/Unreal Engine/$association" -ErrorAction SilentlyContinue
    if ($installed) { $candidates += $installed.InstalledDirectory }
    $candidates += Join-Path ${env:ProgramFiles} "Epic Games/UE_$association"
    $candidates += Join-Path 'D:/Epic Games' "UE_$association"
    $engineRoot = $candidates | Where-Object {
        $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Engine/Build/BatchFiles/Build.bat')) -and
        (Test-Path -LiteralPath (Join-Path $_ 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'))
    } | Select-Object -First 1
    if (-not $engineRoot) { throw "Cannot locate Unreal Engine association '$association' in registry or standard install directories." }
    # Build.bat requires cmd.exe. Reject shell metacharacters in resolved paths before composing its command.
    if (($engineRoot + $projectPath + $logDirectory) -match '["%&|<>^!\r\n]') { throw 'Unsupported shell metacharacter in build paths.' }
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

    $processes = @(Get-CimInstance Win32_Process)
    foreach ($buildProcess in $processes | Where-Object {
        $_.Name -eq 'UnrealBuildTool.exe' -or ($_.Name -eq 'dotnet.exe' -and $_.CommandLine -match 'UnrealBuildTool\.dll')
    }) {
        $parent = $processes | Where-Object { $_.ProcessId -eq $buildProcess.ParentProcessId -and $_.CreationDate -le $buildProcess.CreationDate }
        if ($buildProcess.Name -eq 'UnrealBuildTool.exe' -and -not $parent) {
            Write-Warning "Clearing orphaned UnrealBuildTool.exe PID $($buildProcess.ProcessId)."
            Stop-Process -Id $buildProcess.ProcessId -Force
        } else {
            throw "UnrealBuildTool PID $($buildProcess.ProcessId) may still be active. Wait for that build to finish; no active build was terminated."
        }
    }
    Write-Host "Engine: $engineRoot"
    $buildBatch = Join-Path $engineRoot 'Engine/Build/BatchFiles/Build.bat'
    $buildLog = Join-Path $logDirectory 'PreflightBuild.log'
    $buildArguments = '/d /s /c ""{0}" EMJ2026Editor Win64 Development -Project="{1}" -WaitMutex -FromMsBuild -log="{2}""' -f $buildBatch,$projectPath,$buildLog
    Invoke-BoundedProcess $env:ComSpec $buildArguments $BuildTimeoutSeconds 'PreflightBuild'
    if (-not $SkipSmokeTest) {
        $editorCmd = Join-Path $engineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
        $smokeLog = Join-Path $logDirectory 'PreflightSmoke.log'
        # Quit alone does not close the Slate editor in UE 5.9; request its normal shutdown too.
        $smokeArguments = '"{0}" -NullRHI -unattended -nop4 -nosplash -ExecCmds="Quit,CLOSE_SLATE_MAINFRAME" -abslog="{1}"' -f $projectPath,$smokeLog
        Invoke-BoundedProcess $editorCmd $smokeArguments $SmokeTimeoutSeconds 'PreflightSmoke'
        if (-not (Test-Path -LiteralPath $smokeLog)) { throw 'Smoke process exited without producing an Unreal log.' }
        $smokeText = Get-Content -LiteralPath $smokeLog -Raw
        if ($smokeText -match 'Fatal error:|Assertion failed:|Unhandled Exception:|LogWindows: Error:') { throw "Smoke log contains a fatal/error signature: $smokeLog" }
        if ($smokeText -notmatch 'Engine is initialized' -or $smokeText -notmatch 'LogExit: Exiting') { throw "Smoke log lacks initialization or clean-exit evidence: $smokeLog" }
        Write-Host "Headless editor boot passed. Log: $smokeLog"
    }
    Write-Host "Preflight passed. Smoke skipped: $($SkipSmokeTest.IsPresent)"
    exit 0
} catch {
    Write-Error -Message $_ -ErrorAction Continue
    exit 1
}
