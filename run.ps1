$Config = "Release"
foreach ($arg in $args) {
    if ($arg -eq "--debug") {
        $Config = "Debug"
    }
}

$BuildDir = "build"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

# Configure CMake if needed
if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "Configuring CMake..."
    cmake -B $BuildDir -S .
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Building in $Config mode..."
cmake --build $BuildDir --config $Config

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# Find executable (multi-config vs single-config)
$Exe = $null
$candidates = @(
    "$BuildDir\$Config\TinyVulkanRenderer.exe",
    "$BuildDir\TinyVulkanRenderer.exe"
)
foreach ($c in $candidates) {
    if (Test-Path $c) {
        $Exe = $c
        break
    }
}

if (-not $Exe) {
    Write-Host "Could not find TinyVulkanRenderer.exe!" -ForegroundColor Red
    exit 1
}

Write-Host "Running: $Exe"
& $Exe
