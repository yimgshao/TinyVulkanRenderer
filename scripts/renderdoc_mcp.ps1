[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$RepositoryUrl = "https://github.com/yimgshao/renderdoc-mcp.git"
$RenderDocRepositoryUrl = "https://github.com/baldurk/renderdoc.git"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ThirdPartyDir = Join-Path $ProjectRoot "third_party"
$InstallDir = Join-Path $ThirdPartyDir "renderdoc-mcp"
$BuildDir = Join-Path $InstallDir "build"
$McpExe = Join-Path $BuildDir "Release\renderdoc-mcp.exe"
$InstalledRenderDocSourceDir = Join-Path $InstallDir "renderdoc-src"
$InstalledRenderDocBuildDir = Join-Path $InstalledRenderDocSourceDir "x64\Development"
$RenderDocCmdExe = Join-Path $InstalledRenderDocBuildDir "renderdoccmd.exe"
$QRenderDocExe = Join-Path $InstalledRenderDocBuildDir "qrenderdoc.exe"
$CodexDir = Join-Path $ProjectRoot ".codex"
$CodexConfigPath = Join-Path $CodexDir "config.toml"

function Assert-LastExitCode {
    param([Parameter(Mandatory = $true)][string]$Description)

    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Get-RequiredCommand {
    param([Parameter(Mandatory = $true)][string]$Name)

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $Command) {
        throw "Required command '$Name' was not found in PATH."
    }

    return $Command.Source
}

function Invoke-GitClone {
    param(
        [Parameter(Mandatory = $true)][string[]]$CloneArguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $MaximumAttempts = 3
    for ($Attempt = 1; $Attempt -le $MaximumAttempts; $Attempt++) {
        & $Git clone @CloneArguments
        if ($LASTEXITCODE -eq 0) {
            return
        }

        if ($Attempt -eq $MaximumAttempts) {
            throw "$Description failed after $MaximumAttempts attempts (last exit code: $LASTEXITCODE)."
        }

        $DelaySeconds = 2 * $Attempt
        Write-Warning "$Description failed (attempt $Attempt of $MaximumAttempts). Retrying in $DelaySeconds seconds..."
        Start-Sleep -Seconds $DelaySeconds
    }
}

function Get-VisualStudioInstallation {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf)) {
        throw "vswhere.exe was not found. Install Visual Studio with the Desktop development with C++ workload."
    }

    $InstallationPath = & $VsWhere `
        -latest `
        -products * `
        -requires Microsoft.Component.MSBuild `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    Assert-LastExitCode "Visual Studio discovery"

    if (-not $InstallationPath) {
        throw "A Visual Studio installation with MSBuild and C++ tools was not found."
    }

    return $InstallationPath.Trim()
}

function Get-LatestPlatformToolset {
    param([Parameter(Mandatory = $true)][string]$VisualStudioDir)

    $VcMsBuildDir = Join-Path $VisualStudioDir "MSBuild\Microsoft\VC"
    $Toolsets = @(
        Get-ChildItem -LiteralPath $VcMsBuildDir -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $PlatformToolsetsDir = Join-Path $_.FullName "Platforms\x64\PlatformToolsets"
                if (Test-Path -LiteralPath $PlatformToolsetsDir -PathType Container) {
                    Get-ChildItem -LiteralPath $PlatformToolsetsDir -Directory |
                        Where-Object { $_.Name -match '^v\d+$' } |
                        ForEach-Object { $_.Name }
                }
            } |
            Sort-Object -Unique
    )

    $SelectedToolset = $Toolsets |
        Sort-Object { [int]$_.Substring(1) } -Descending |
        Select-Object -First 1
    if (-not $SelectedToolset) {
        throw "No x64 MSVC platform toolset was found under '$VcMsBuildDir'."
    }

    return $SelectedToolset
}

function Invoke-RenderDocProjectBuilds {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$MSBuild,
        [Parameter(Mandatory = $true)][string]$PlatformToolset,
        [Parameter(Mandatory = $true)][string[]]$RelativeProjects
    )

    $MSBuildCommonArgs = @(
        "/p:Configuration=Development",
        "/p:Platform=x64",
        "/p:PlatformToolset=$PlatformToolset",
        "/p:SolutionDir=$SourceDir\",
        "/m"
    )

    foreach ($RelativeProject in $RelativeProjects) {
        $Project = Join-Path $SourceDir $RelativeProject
        if (-not (Test-Path -LiteralPath $Project -PathType Leaf)) {
            throw "RenderDoc project was not found: $Project"
        }

        Write-Host "Building $RelativeProject with $PlatformToolset..." -ForegroundColor Cyan
        & $MSBuild $Project @MSBuildCommonArgs
        Assert-LastExitCode "Building $RelativeProject"
    }
}

function Test-Administrator {
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
    return $Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-RenderDocVulkanLayerRegistration {
    param([Parameter(Mandatory = $true)][string]$RenderDocCmd)

    $StatusOutput = (& $RenderDocCmd vulkanlayer --explain 2>&1 | Out-String)
    Assert-LastExitCode "Checking RenderDoc Vulkan layer registration"
    return -not $StatusOutput.Contains("Warning: Vulkan layer not correctly registered.")
}

function Install-RenderDocVulkanLayer {
    param([Parameter(Mandatory = $true)][string]$RenderDocCmd)

    if (Test-RenderDocVulkanLayerRegistration $RenderDocCmd) {
        Write-Host "RenderDoc Vulkan layer is already registered." -ForegroundColor Green
        return
    }

    Write-Host "Registering the project RenderDoc Vulkan layer (administrator permission required)..." -ForegroundColor Cyan
    if (Test-Administrator) {
        & $RenderDocCmd vulkanlayer --register --system
        Assert-LastExitCode "Registering RenderDoc Vulkan layer"
    }
    else {
        $RegistrationProcess = Start-Process `
            -FilePath $RenderDocCmd `
            -ArgumentList @("vulkanlayer", "--register", "--system") `
            -Verb RunAs `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
        if ($RegistrationProcess.ExitCode -ne 0) {
            throw "Registering RenderDoc Vulkan layer failed with exit code $($RegistrationProcess.ExitCode)."
        }
    }

    if (-not (Test-RenderDocVulkanLayerRegistration $RenderDocCmd)) {
        throw "RenderDoc Vulkan layer is still not correctly registered after installation."
    }

    Write-Host "Project RenderDoc Vulkan layer was registered successfully." -ForegroundColor Green
}

function Remove-BootstrapDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $ResolvedThirdParty = [System.IO.Path]::GetFullPath($ThirdPartyDir).TrimEnd('\')
    $ResolvedPath = [System.IO.Path]::GetFullPath($Path)
    $ParentPath = Split-Path -Parent $ResolvedPath
    $LeafName = Split-Path -Leaf $ResolvedPath

    if ($ParentPath.TrimEnd('\') -ne $ResolvedThirdParty -or
        -not $LeafName.StartsWith(".renderdoc-mcp-bootstrap-")) {
        throw "Refusing to remove unexpected bootstrap directory: $ResolvedPath"
    }

    Remove-Item -LiteralPath $ResolvedPath -Recurse -Force
}

function Install-CodexMcpConfig {
    $SectionPattern = '(?m)^\s*\[mcp_servers\.renderdoc-mcp\]\s*$'

    if (Test-Path -LiteralPath $CodexConfigPath -PathType Leaf) {
        $ExistingConfig = Get-Content -LiteralPath $CodexConfigPath -Raw
        if ($ExistingConfig -match $SectionPattern) {
            Write-Host "Codex MCP configuration already exists in $CodexConfigPath" -ForegroundColor Green
            return
        }
    }
    elseif (-not (Test-Path -LiteralPath $CodexDir -PathType Container)) {
        New-Item -ItemType Directory -Path $CodexDir | Out-Null
    }

    if ($McpExe.Contains("'")) {
        throw "Cannot write the MCP executable path as a TOML literal string because it contains a single quote: $McpExe"
    }

    $ConfigBlock = @(
        "[mcp_servers.renderdoc-mcp]",
        "command = '$McpExe'",
        "args = []"
    ) -join [Environment]::NewLine

    if (Test-Path -LiteralPath $CodexConfigPath -PathType Leaf) {
        $ExistingConfig = Get-Content -LiteralPath $CodexConfigPath -Raw
        $Separator = if ($ExistingConfig.EndsWith("`n")) {
            [Environment]::NewLine
        }
        else {
            [Environment]::NewLine + [Environment]::NewLine
        }
        Add-Content -LiteralPath $CodexConfigPath -Value ($Separator + $ConfigBlock)
    }
    else {
        Set-Content -LiteralPath $CodexConfigPath -Value $ConfigBlock
    }

    Write-Host "Codex MCP configuration was written to $CodexConfigPath" -ForegroundColor Green
}

Set-Location $ProjectRoot

if (-not (Test-Path -LiteralPath $InstallDir -PathType Container)) {
    $Git = Get-RequiredCommand "git"
    $CMake = Get-RequiredCommand "cmake"
    $VisualStudioDir = Get-VisualStudioInstallation
    $MSBuild = Join-Path $VisualStudioDir "MSBuild\Current\Bin\MSBuild.exe"
    $PlatformToolset = Get-LatestPlatformToolset $VisualStudioDir

    if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
        throw "MSBuild.exe was not found at the expected path: $MSBuild"
    }

    if (-not (Test-Path -LiteralPath $ThirdPartyDir -PathType Container)) {
        New-Item -ItemType Directory -Path $ThirdPartyDir | Out-Null
    }

    $BootstrapDir = Join-Path $ThirdPartyDir ".renderdoc-mcp-bootstrap-$PID"
    Remove-BootstrapDirectory $BootstrapDir

    try {
        Write-Host "Cloning renderdoc-mcp..." -ForegroundColor Cyan
        Invoke-GitClone `
            -CloneArguments @("--depth", "1", $RepositoryUrl, $BootstrapDir) `
            -Description "Cloning renderdoc-mcp"

        $RenderDocVersionFile = Join-Path $BootstrapDir "renderdoc-version.txt"
        if (-not (Test-Path -LiteralPath $RenderDocVersionFile -PathType Leaf)) {
            throw "renderdoc-version.txt was not found in the renderdoc-mcp repository."
        }

        $RenderDocVersion = (Get-Content -LiteralPath $RenderDocVersionFile -Raw).Trim()
        if (-not $RenderDocVersion.StartsWith("v")) {
            throw "Invalid RenderDoc version '$RenderDocVersion' in renderdoc-version.txt."
        }

        $RenderDocSourceDir = Join-Path $BootstrapDir "renderdoc-src"
        Write-Host "Cloning RenderDoc $RenderDocVersion..." -ForegroundColor Cyan
        Invoke-GitClone `
            -CloneArguments @("--depth", "1", "--branch", $RenderDocVersion, $RenderDocRepositoryUrl, $RenderDocSourceDir) `
            -Description "Cloning RenderDoc $RenderDocVersion"

        $RenderDocProjects = @(
            "renderdoc\3rdparty\breakpad\client\windows\common.vcxproj",
            "renderdoc\3rdparty\breakpad\client\windows\crash_generation\crash_generation_client.vcxproj",
            "renderdoc\3rdparty\breakpad\client\windows\handler\exception_handler.vcxproj",
            "renderdoc\renderdoc.vcxproj",
            "renderdoccmd\renderdoccmd.vcxproj",
            "qrenderdoc\qrenderdoc_local.vcxproj"
        )
        Invoke-RenderDocProjectBuilds `
            -SourceDir $RenderDocSourceDir `
            -MSBuild $MSBuild `
            -PlatformToolset $PlatformToolset `
            -RelativeProjects $RenderDocProjects

        $RenderDocBuildDir = Join-Path $RenderDocSourceDir "x64\Development"
        $McpBuildDir = Join-Path $BootstrapDir "build"

        Write-Host "Configuring renderdoc-mcp..." -ForegroundColor Cyan
        & $CMake `
            -S $BootstrapDir `
            -B $McpBuildDir `
            "-DRENDERDOC_DIR=$RenderDocSourceDir" `
            "-DRENDERDOC_BUILD_DIR=$RenderDocBuildDir"
        Assert-LastExitCode "Configuring renderdoc-mcp"

        Write-Host "Building renderdoc-mcp..." -ForegroundColor Cyan
        & $CMake --build $McpBuildDir --config Release --target renderdoc-mcp
        Assert-LastExitCode "Building renderdoc-mcp"

        $BootstrapExe = Join-Path $McpBuildDir "Release\renderdoc-mcp.exe"
        $BootstrapDll = Join-Path $McpBuildDir "Release\renderdoc.dll"
        $BootstrapJson = Join-Path $McpBuildDir "Release\renderdoc.json"
        $BootstrapRenderDocCmd = Join-Path $RenderDocBuildDir "renderdoccmd.exe"
        $BootstrapQRenderDoc = Join-Path $RenderDocBuildDir "qrenderdoc.exe"
        foreach ($RequiredFile in @(
            $BootstrapExe,
            $BootstrapDll,
            $BootstrapJson,
            $BootstrapRenderDocCmd,
            $BootstrapQRenderDoc
        )) {
            if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf)) {
                throw "Expected build output was not found: $RequiredFile"
            }
        }

        Move-Item -LiteralPath $BootstrapDir -Destination $InstallDir
        Write-Host "renderdoc-mcp was installed in $InstallDir" -ForegroundColor Green
    }
    finally {
        Remove-BootstrapDirectory $BootstrapDir
    }
}

if (-not (Test-Path -LiteralPath $McpExe -PathType Leaf)) {
    throw "renderdoc-mcp is present but its executable was not found at '$McpExe'. Remove '$InstallDir' and run this script again to rebuild it."
}

# Older installations created by this script may contain the replay DLL but not
# the command-line and GUI frontends. Build missing frontends in place so rerunning
# the bootstrap script upgrades such installations without recloning RenderDoc.
$MissingRenderDocProjects = @()
if (-not (Test-Path -LiteralPath $RenderDocCmdExe -PathType Leaf)) {
    $MissingRenderDocProjects += "renderdoccmd\renderdoccmd.vcxproj"
}
if (-not (Test-Path -LiteralPath $QRenderDocExe -PathType Leaf)) {
    $MissingRenderDocProjects += "qrenderdoc\qrenderdoc_local.vcxproj"
}

if ($MissingRenderDocProjects.Count -gt 0) {
    if (-not (Test-Path -LiteralPath $InstalledRenderDocSourceDir -PathType Container)) {
        throw "RenderDoc source directory was not found: $InstalledRenderDocSourceDir"
    }

    $VisualStudioDir = Get-VisualStudioInstallation
    $MSBuild = Join-Path $VisualStudioDir "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
        throw "MSBuild.exe was not found at the expected path: $MSBuild"
    }
    $PlatformToolset = Get-LatestPlatformToolset $VisualStudioDir

    Invoke-RenderDocProjectBuilds `
        -SourceDir $InstalledRenderDocSourceDir `
        -MSBuild $MSBuild `
        -PlatformToolset $PlatformToolset `
        -RelativeProjects $MissingRenderDocProjects
}

foreach ($RequiredFrontend in @($RenderDocCmdExe, $QRenderDocExe)) {
    if (-not (Test-Path -LiteralPath $RequiredFrontend -PathType Leaf)) {
        throw "Expected RenderDoc frontend was not found after building: $RequiredFrontend"
    }
}

Install-RenderDocVulkanLayer $RenderDocCmdExe
Install-CodexMcpConfig
Write-Host "renderdoc-mcp, renderdoccmd, and qrenderdoc are ready. Restart Codex to load the project MCP configuration." -ForegroundColor Green
