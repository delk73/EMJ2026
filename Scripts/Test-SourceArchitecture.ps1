[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$source = Join-Path (Split-Path $PSScriptRoot -Parent) 'Source/EMJ2026'
$obsolete = @(Get-ChildItem -LiteralPath $source -Filter 'DirectOrScutSlice*.cpp')
if ($obsolete.Count -gt 0) { throw "Slice-numbered source is forbidden: $($obsolete.Name -join ', ')" }
$owners = @{}
foreach ($header in Get-ChildItem -LiteralPath $source -Filter '*.h') {
    $text = Get-Content -LiteralPath $header.FullName -Raw
    foreach ($match in [regex]::Matches($text, '(?m)^class\s+(?:EMJ2026_API\s+)?([AUFS]\w+)\s*(?:final\s*)?:\s*public')) {
        $owners[$match.Groups[1].Value] = $header.BaseName
    }
}
foreach ($implementation in Get-ChildItem -LiteralPath $source -Filter '*.cpp') {
    if ($implementation.BaseName -like '*Test') { continue }
    $text = Get-Content -LiteralPath $implementation.FullName -Raw
    foreach ($match in [regex]::Matches($text, '(?m)^(?:[\w:<>,*&]+[ \t]+)*([AUFS]\w+)::~?\w+\s*\([^;{}]*\)\s*(?:const\s*)?\{')) {
        $class = $match.Groups[1].Value
        if ($owners.ContainsKey($class) -and $owners[$class] -ne $implementation.BaseName) {
            throw "$class method in $($implementation.Name); expected $($owners[$class]).cpp"
        }
    }
}
Write-Host 'Source architecture passed: no slice files; runtime methods match their owning headers.'
