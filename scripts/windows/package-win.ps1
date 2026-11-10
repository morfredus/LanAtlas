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
# Installer (Inno Setup 6), built from the same staged folder. Skipped with a warning when
# ISCC.exe is not installed: the ZIP is still produced.
if ($Version -match '^(\d+)\.(\d+)\.(\d+)') { $versionNum = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0" }
else { throw "Version '$Version' is not X.Y.Z." }
$iscc = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($iscc) {
    & $iscc "/DAppVersion=$Version" "/DAppVersionNum=$versionNum" "/DStageDir=$dist" `
        "/O$(Join-Path $root 'dist')" (Join-Path $root "installer\lanatlas.iss")
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed." }
} else {
    Write-Warning "Inno Setup 6 not found: installer not built (the ZIP still is)."
}

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zip
Write-Output "OK - $zip"
