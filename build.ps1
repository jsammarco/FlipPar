$ErrorActionPreference = "Stop"

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$firmwareDir = "C:\Users\Joe\Projects\flipperzero-firmware"
$targetDir = Join-Path $firmwareDir "applications_user\flippar"

Write-Host "Syncing app files to $targetDir..."

if(!(Test-Path $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir | Out-Null
}

$stalePaths = @(
    (Join-Path $targetDir "build.ps1")
    (Join-Path $targetDir "assets\flippar_icon.png")
    (Join-Path $targetDir "assets\flippar_splash_128x64.png")
)

foreach($path in $stalePaths) {
    if(Test-Path $path) {
        Remove-Item -Force $path
    }
}

$robocopyArgs = @(
    $sourceDir
    $targetDir
    "/MIR"
    "/XD"
    ".git"
    "/XF"
    "README.md"
    "build.ps1"
)

& robocopy @robocopyArgs | Out-Host

if($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

Push-Location $firmwareDir
try {
    Write-Host "Building FlipPar..."
    & .\fbt.cmd build APPSRC=applications_user/flippar
    if($LASTEXITCODE -ne 0) {
        throw "fbt build failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host "Build complete."
