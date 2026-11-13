[CmdletBinding()]
param([switch]$Capture, [switch]$Bake, [switch]$PIE, [switch]$Chunk2, [switch]$Slice9, [switch]$Slice9a, [switch]$Slice9b, [ValidateRange(640,3840)][int]$Width=1600, [ValidateRange(360,2160)][int]$Height=1000, [ValidateRange(0,8)][int]$Feed=0, [switch]$Grid, [int]$TimeoutSeconds=240)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$project=Join-Path $projectRoot 'EMJ2026.uproject'
$association=(Get-Content $project -Raw | ConvertFrom-Json).EngineAssociation
$editor=Join-Path ${env:ProgramFiles} "Epic Games/UE_$association/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
if (!(Test-Path $editor)) { throw "Unreal Editor commandlet executable not found: $editor" }
if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) { throw 'Close Unreal Editor before running this standalone verification.' }
$slice=if ($Slice9b) {'Slice9b'} elseif ($Slice9a) {'Slice9a'} elseif ($Slice9) {'Slice9'} elseif ($Chunk2) {'Chunk2'} else {'Chunk1'}
$prefix=if ($slice -eq 'Chunk1') {'DirectOrScut'} elseif ($slice -eq 'Chunk2') {'DirectOrScutChunk2'} else {$slice}
$operation=if ($PIE) {'PIE'} elseif ($Bake) {'Bake'} elseif ($Capture) {'Capture'} else {'Runtime'}
$stem=$prefix+$operation
$log=Join-Path $projectRoot "Saved/Logs/$stem.log"
$arguments='"{0}" -unattended -nop4 -nosplash -abslog="{1}"' -f $project,$log
$testName=switch ($slice) {
    'Slice9b' {'DirectOrScut.Slice9b.RoutedConsolePIE'}
    'Slice9a' {'DirectOrScut.Slice9a.RoutedInputPIE'}
    'Slice9' {'DirectOrScut.Slice9.WorkstationPIE'}
    'Chunk2' {'DirectOrScut.Chunk2.SimulationPIE'}
    default {'DirectOrScut.Chunk1.PIEDoorMouseTrace'}
}
$reportBase=if ($slice -eq 'Chunk1') {'PIE'} elseif ($slice -eq 'Chunk2') {'Chunk2PIE'} else {$slice}
$previewBase=if ($slice -eq 'Chunk1') {'Blockout'} else {$slice}
if ($PIE) {
    $arguments+=' -RenderOffscreen -ResX={0} -ResY={1} -ExecCmds="Automation RunTests {2}" -TestExit="Automation Test Queue Empty" -ReportExportPath="Saved/DirectOrScut/{3}Report"' -f $Width,$Height,$testName,$reportBase
} elseif ($Bake) {
    $arguments+=' -run=DirectOrScutBake -NullRHI'
} elseif ($Capture) {
    $arguments+=' /Game/DirectOrScut/Maps/DirectOrScutBlockout -game -RenderOffscreen -ResX={0} -ResY={1} -ScutCapture' -f $Width,$Height
    if ($Chunk2) { $arguments+=' -ScutChunk2Capture' }
    if ($Slice9) { $arguments+=" -ScutSlice9Capture -ScutFeed=$Feed"; if ($Grid) { $arguments+=' -ScutGrid' } }
    if ($Slice9a) { $arguments+=' -ScutSlice9aCapture' }
    if ($Slice9b) { $arguments+=' -ScutSlice9bCapture' }
} else {
    $arguments+=' /Game/DirectOrScut/Maps/DirectOrScutBlockout -game -NullRHI -ScutValidate'
}
$startedAt=[DateTime]::UtcNow
$process=Start-Process $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
$processHandle=$process.Handle
$deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
while (!$process.WaitForExit(1000)) {
    if ([DateTime]::UtcNow -gt $deadline) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw "Verification timed out. Log: $log"
    }
}
if ($process.ExitCode -ne 0) { throw "Verification exited $($process.ExitCode). Log: $log" }
if (!$PIE -and !$Bake -and !$Capture -and !(Select-String -Path $log -SimpleMatch 'SCUT VALIDATION PASS' -Quiet)) { throw "Runtime checks did not pass. Log: $log" }
$successMarker='Result={Success} Name={'+($testName.Split('.')[-1])+'}'
if ($PIE -and !(Select-String -Path $log -SimpleMatch $successMarker -Quiet)) { throw "PIE automation test did not pass. Log: $log" }
if ($Capture) {
    $preview=Get-Item (Join-Path $projectRoot "Saved/DirectOrScut/$previewBase.png") -ErrorAction SilentlyContinue
    if (!$preview -or $preview.LastWriteTimeUtc -lt $startedAt) { throw "Fresh screenshot missing. Log: $log" }
}
Write-Host "Completed $stem. Log: $log"
