# Prepare a firmware release: pristine west build + copy the hex into release/.
# Run from anywhere inside the repo:  .\scripts\make-release.ps1 [-Version 1.0.0] [-NoBuild]
param(
    [string]$Version = "",
    [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $NoBuild) {
    . "$root\scripts\ncs-env.ps1"
    west build -b nrf52dk/nrf52810 -p always $root
    if ($LASTEXITCODE -ne 0) { throw "west build failed ($LASTEXITCODE)" }
}

# Hex location differs between sysbuild and classic builds — take the first that exists.
$hex = @("build\merged.hex", "build\zephyr\zephyr.hex", "build\cadence-for-ebike\zephyr\zephyr.hex") |
    ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $hex) { throw "no built hex found under build\ — run without -NoBuild" }

if (-not $Version) {
    $Version = (git -C $root describe --tags --always --dirty).Trim()
}

$rel = Join-Path $root "release"
New-Item -ItemType Directory -Force -Path $rel | Out-Null
$name = "bk6ls-cadence-$Version.hex"
Copy-Item $hex (Join-Path $rel $name) -Force

$hash = (Get-FileHash (Join-Path $rel $name) -Algorithm SHA256).Hash.ToLower()
@"
BK6LS-Cadence firmware release
version : $Version
date    : $(Get-Date -Format "yyyy-MM-dd HH:mm")
commit  : $(git -C $root rev-parse HEAD)
source  : $hex
sha256  : $hash
flash   : nrfjprog --family NRF52 --program $name --chiperase --verify --reset
"@ | Set-Content (Join-Path $rel "bk6ls-cadence-$Version.txt")

Write-Host "Release ready: release\$name  (sha256 $hash)"
