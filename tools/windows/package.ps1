# Packages CMake's program/runtime list and gameplay settings, without extracted assets.
param([Parameter(Mandatory)][string]$BuildDirectory,
      [Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common.ps1"
$build = Resolve-CheckoutPath $BuildDirectory
$archive = Resolve-CheckoutPath $Destination
& "$PSScriptRoot/check-runtime.ps1" -BuildDirectory $build
$manifest = Join-Path $build 'windows-runtime-files.txt'
$names = @(Get-Content -LiteralPath $manifest | Where-Object { $_.Trim() })
if ('darkchronicle.exe' -notin $names -or 'dc2data.exe' -notin $names -or 'vulkan-1.dll' -notin $names) {
    throw 'The runtime manifest is incomplete. Reconfigure and build first.'
}
$paths = foreach ($name in $names) {
    if ($name -ne [IO.Path]::GetFileName($name) -or $name -notmatch '\.(exe|dll)$') {
        throw "Unexpected runtime manifest entry: $name"
    }
    $path = Join-Path $build $name
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing runtime file: $path" }
    $path
}
$gameplay = Join-Path $build 'gameplay'
if (!(Test-Path -LiteralPath $gameplay -PathType Container)) {
    throw 'Gameplay settings are missing. Rebuild first.'
}
Compress-Archive -LiteralPath @($paths; $gameplay) -DestinationPath $archive -Force
Write-Output "Packaged $($paths.Count) program/runtime files and gameplay settings in $archive."
