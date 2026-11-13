[CmdletBinding()]
param([switch]$Capture, [switch]$Bake, [switch]$PIE, [switch]$Chunk2, [int]$TimeoutSeconds=240)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$project=Join-Path $projectRoot 'EMJ2026.uproject'
$association=(Get-Content $project -Raw | ConvertFrom-Json).EngineAssociation
$editor=Join-Path ${env:ProgramFiles} "Epic Games/UE_$association/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
if (!(Test-Path $editor)) { throw "Unreal Editor commandlet executable not found: $editor" }
if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) { throw 'Close Unreal Editor before running this standalone verification.' }
$stem=if ($Chunk2 -and $PIE) {'DirectOrScutChunk2PIE'} elseif ($Chunk2 -and $Capture) {'DirectOrScutChunk2Capture'} elseif ($PIE) {'DirectOrScutPIE'} elseif ($Bake) {'DirectOrScutBake'} elseif ($Capture) {'DirectOrScutCapture'} else {'DirectOrScutRuntime'}
$log=Join-Path $projectRoot "Saved/Logs/$stem.log"
$arguments='"{0}" -unattended -nop4 -nosplash -abslog="{1}"' -f $project,$log
if ($PIE) { $arguments+=' -RenderOffscreen -ResX=1600 -ResY=1400 -ExecCmds="Automation RunTests DirectOrScut.Chunk1.PIEDoorMouseTrace" -TestExit="Automation Test Queue Empty" -ReportExportPath="Saved/DirectOrScut/PIEReport"' }
elseif ($Bake) { $arguments+=' -run=DirectOrScutBake -NullRHI' }
elseif ($Capture) { $arguments+=' /Game/DirectOrScut/Maps/DirectOrScutBlockout -game -RenderOffscreen -ResX=1600 -ResY=1400 -ScutCapture' }
else { $arguments+=' /Game/DirectOrScut/Maps/DirectOrScutBlockout -game -NullRHI -ScutValidate' }
if ($Chunk2 -and $PIE) { $arguments=$arguments.Replace('DirectOrScut.Chunk1.PIEDoorMouseTrace','DirectOrScut.Chunk2.SimulationPIE').Replace('Saved/DirectOrScut/PIEReport','Saved/DirectOrScut/Chunk2PIEReport') }
if ($Chunk2 -and $Capture) { $arguments+=' -ScutChunk2Capture' }
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
$successMarker=if ($Chunk2) {'Result={Success} Name={SimulationPIE}'} else {'Result={Success} Name={PIEDoorMouseTrace}'}
if ($PIE -and !(Select-String -Path $log -SimpleMatch $successMarker -Quiet)) { throw "PIE door mouse trace test did not pass. Log: $log" }
if ($Capture) {
    $previewName=if ($Chunk2) {'Saved/DirectOrScut/Chunk2.png'} else {'Saved/DirectOrScut/Blockout.png'}
    $preview=Get-Item (Join-Path $projectRoot $previewName) -ErrorAction SilentlyContinue
    if (!$preview -or $preview.LastWriteTimeUtc -lt $startedAt) { throw "Fresh screenshot missing. Log: $log" }
}
Write-Host "Completed $stem. Log: $log"
