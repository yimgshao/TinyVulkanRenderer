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

        $MSBuildCommonArgs = @(
            "/p:Configuration=Development",
            "/p:Platform=x64",
            "/p:PlatformToolset=v143",
            "/p:SolutionDir=$RenderDocSourceDir\",
            "/m"
        )
        $RenderDocProjects = @(
            "renderdoc\3rdparty\breakpad\client\windows\common.vcxproj",
            "renderdoc\3rdparty\breakpad\client\windows\crash_generation\crash_generation_client.vcxproj",
            "renderdoc\3rdparty\breakpad\client\windows\handler\exception_handler.vcxproj",
            "renderdoc\renderdoc.vcxproj"
        )

        foreach ($RelativeProject in $RenderDocProjects) {
            $Project = Join-Path $RenderDocSourceDir $RelativeProject
            Write-Host "Building $RelativeProject..." -ForegroundColor Cyan
            & $MSBuild $Project @MSBuildCommonArgs
            Assert-LastExitCode "Building $RelativeProject"
        }

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
        foreach ($RequiredFile in @($BootstrapExe, $BootstrapDll, $BootstrapJson)) {
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

Install-CodexMcpConfig
Write-Host "renderdoc-mcp is ready. Restart Codex to load the project MCP configuration." -ForegroundColor Green
