param(
    [string]$BuildDir = "build-mingw",
    [string]$Version
)
$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root $BuildDir
if (-not (Test-Path (Join-Path $build "LanAtlas.exe"))) {
    throw "LanAtlas.exe introuvable dans $build."
}
if (-not $Version) {
    $Version = (Get-Content -Path (Join-Path $root "VERSION") -TotalCount 1).Trim()
}
$name = "LanAtlas-$Version-win64"
$dist = Join-Path $root "dist\$name"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item (Join-Path $build "LanAtlas.exe") $dist
Copy-Item (Join-Path $build "*.dll") $dist -ErrorAction SilentlyContinue
Copy-Item (Join-Path $build "oui.json") $dist -ErrorAction SilentlyContinue
foreach ($d in 'platforms','styles','imageformats','tls','networkinformation','generic','iconengines') {
    $src = Join-Path $build $d
    if (Test-Path $src) { Copy-Item $src $dist -Recurse }
}
foreach ($f in 'LICENSE','README.md','README.fr.md','CHANGELOG.md','VERSION') {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $dist }
}
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zip
Write-Output "OK - $zip"
