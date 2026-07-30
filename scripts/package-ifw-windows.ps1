param(
    [string]$IfwBin = $env:VAPORVIEW_IFW_BIN,
    [string]$RepositoryUrl,
    [string]$OutputDirectory,
    [string]$QtPrefix = $env:VAPORVIEW_QT_MSVC_PREFIX,
    [string]$VisualStudioInstall = $env:VAPORVIEW_VS2022_INSTALL,
    [string]$CMakeExe = $env:VAPORVIEW_CMAKE_EXE,
    [string]$NinjaExe = $env:VAPORVIEW_NINJA_EXE,
    [string]$EditBin = $env:VAPORVIEW_EDITBIN,
    [switch]$SkipBuild,
    [switch]$NoRepository
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "build\Release"
$workDir = Join-Path $buildDir "ifw-work"
$stageDir = Join-Path $workDir "stage"
$packagesDir = Join-Path $workDir "packages"
$outputDir = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $buildDir "ifw"
} else {
    [IO.Path]::GetFullPath($OutputDirectory)
}
function Resolve-RequiredFile {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Description)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found at '$Path'."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-RequiredDirectory {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Description)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description was not found at '$Path'."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Remove-VerifiedDirectory {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$AllowedRoot)
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    $resolvedPath = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
    $resolvedRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $AllowedRoot).Path)
    $rootPrefix = $resolvedRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolvedPath -eq $resolvedRoot -or
        -not $resolvedPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove directory outside the packaging workspace: $resolvedPath"
    }
    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

function Copy-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Source, [Parameter(Mandatory = $true)][string]$Destination)
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        return
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Invoke-Checked {
    param([Parameter(Mandatory = $true)][string]$FilePath, [Parameter(Mandatory = $true)][string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Resolve-EditBin {
    param([string]$RequestedPath, [string]$VisualStudioRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return Resolve-RequiredFile $RequestedPath "editbin.exe"
    }

    $command = Get-Command editbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }

    $candidateRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($VisualStudioRoot)) {
        $candidateRoots += (Join-Path $VisualStudioRoot "VC\Tools\MSVC")
    }
    $candidateRoots += @(
        "F:\VisualStudio\2022\BuildTools\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
    )

    foreach ($root in ($candidateRoots | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        $candidate = Get-ChildItem -LiteralPath $root -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\editbin.exe" } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
        if ($candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "editbin.exe was not found. Pass -EditBin with the Visual Studio editbin.exe path."
}

if (-not $SkipBuild) {
    $buildScript = Join-Path $repoRoot "scripts\build-windows-msvc2022.ps1"
    $buildArgs = @("-ExecutionPolicy", "Bypass", "-File", $buildScript, "-Action", "Build", "-EnableOsgEarth")
    if (-not [string]::IsNullOrWhiteSpace($QtPrefix)) { $buildArgs += @("-QtPrefix", $QtPrefix) }
    if (-not [string]::IsNullOrWhiteSpace($VisualStudioInstall)) { $buildArgs += @("-VisualStudioInstall", $VisualStudioInstall) }
    if (-not [string]::IsNullOrWhiteSpace($CMakeExe)) { $buildArgs += @("-CMakeExe", $CMakeExe) }
    if (-not [string]::IsNullOrWhiteSpace($NinjaExe)) { $buildArgs += @("-NinjaExe", $NinjaExe) }
    & powershell.exe @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "VaporView osgEarth Release build failed with exit code $LASTEXITCODE."
    }
}

$buildDir = Resolve-RequiredDirectory $buildDir "build/Release"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
Remove-VerifiedDirectory $stageDir $buildDir
Remove-VerifiedDirectory $packagesDir $buildDir
New-Item -ItemType Directory -Force -Path $stageDir, $packagesDir, $outputDir | Out-Null

if ([string]::IsNullOrWhiteSpace($IfwBin)) {
    $binaryCreatorCommand = Get-Command binarycreator.exe -ErrorAction SilentlyContinue
    if ($binaryCreatorCommand) {
        $IfwBin = Split-Path -Parent $binaryCreatorCommand.Source
    }
}
$IfwBin = Resolve-RequiredDirectory $IfwBin "Qt Installer Framework bin directory"
$binaryCreator = Resolve-RequiredFile (Join-Path $IfwBin "binarycreator.exe") "binarycreator.exe"
$repoGenerator = Resolve-RequiredFile (Join-Path $IfwBin "repogen.exe") "repogen.exe"
$editBin = Resolve-EditBin $EditBin $VisualStudioInstall

$cmakeText = Get-Content -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($cmakeText -notmatch 'project\(VaporView VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "Could not determine VaporView version from CMakeLists.txt."
}
$version = $Matches[1]
$releaseDate = (Get-Date).ToString("yyyy-MM-dd")

$executableNames = @("VaporView.exe", "VaporViewSky.exe", "VaporViewSkyCore.exe", "VaporViewSkyTui.exe")
$executableSources = @{}
foreach ($name in $executableNames) {
    $matches = Get-ChildItem -LiteralPath $buildDir -Recurse -Filter $name -File |
        Where-Object { $_.FullName -notmatch '\\(tests|CMakeFiles|ifw-work)\\' } |
        Sort-Object LastWriteTime -Descending
    if (-not $matches) {
        throw "Built executable was not found: $name"
    }
    $executableSources[$name] = $matches[0].FullName
    Copy-Item -LiteralPath $matches[0].FullName -Destination (Join-Path $stageDir $name) -Force
}

$runtimeRoots = @($buildDir) + ($executableSources.Values | ForEach-Object { Split-Path -Parent $_ }) | Select-Object -Unique
$excludedRuntimeDllRegex = '^(Qt6Test|squish|aws-c-|aws-checksums|aws-cpp-|aws-crt|libprotoc)'
foreach ($root in $runtimeRoots) {
    Get-ChildItem -LiteralPath $root -Filter *.dll -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch $excludedRuntimeDllRegex } |
        Copy-Item -Destination $stageDir -Force
    foreach ($pattern in @("generic", "iconengines", "imageformats", "networkinformation", "platforms", "styles", "tls", "osgPlugins-*")) {
        Get-ChildItem -LiteralPath $root -Directory -Filter $pattern -ErrorAction SilentlyContinue |
            ForEach-Object { Copy-DirectoryContents $_.FullName (Join-Path $stageDir $_.Name) }
    }
}

$shareRoot = Join-Path $buildDir "share"
foreach ($shareName in @("gdal", "proj", "proj4")) {
    $shareSource = Join-Path $shareRoot $shareName
    if (Test-Path -LiteralPath $shareSource -PathType Container) {
        Copy-DirectoryContents $shareSource (Join-Path $stageDir "share\$shareName")
    }
}

$resourceSource = $runtimeRoots |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ "resources\modern_style.qss") } |
    Select-Object -First 1
if (-not $resourceSource) {
    throw "Runtime resources were not staged by the Release build."
}
$resourceDestination = Join-Path $stageDir "resources"
New-Item -ItemType Directory -Force -Path $resourceDestination | Out-Null
foreach ($fileName in @("modern_style.qss", "combo_arrow_down.xpm", "combo_arrow_up.xpm", "sky_startup_logo_ansi.txt")) {
    foreach ($root in $runtimeRoots) {
        $source = Join-Path $root "resources\$fileName"
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $resourceDestination -Force
            break
        }
    }
}
foreach ($directoryName in @("lucide", "VaproViewLOGO")) {
    $source = Join-Path $resourceSource "resources\$directoryName"
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-DirectoryContents $source (Join-Path $resourceDestination $directoryName)
    }
}
if (Test-Path -LiteralPath (Join-Path $stageDir "resources\maps")) {
    Remove-VerifiedDirectory (Join-Path $stageDir "resources\maps") $stageDir
}

$driversSource = Join-Path $repoRoot "packaging\ifw\drivers"
Copy-DirectoryContents $driversSource (Join-Path $stageDir "drivers")
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir "data") | Out-Null
$licenseDestination = Join-Path $stageDir "licenses"
New-Item -ItemType Directory -Force -Path $licenseDestination | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $licenseDestination -Force
if (Test-Path -LiteralPath (Join-Path $repoRoot "third_party\rtklib\LICENSE.txt")) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "third_party\rtklib\LICENSE.txt") -Destination $licenseDestination -Force
}

$packageRoot = Join-Path $packagesDir "com.vaporview.core"
$packageMeta = Join-Path $packageRoot "meta"
$packageData = Join-Path $packageRoot "data"
New-Item -ItemType Directory -Force -Path $packageMeta, $packageData | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "packaging\ifw\packages\com.vaporview.core\meta\installscript.qs") -Destination $packageMeta -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "packaging\ifw\packages\com.vaporview.core\meta\shortcutselection.ui") -Destination $packageMeta -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "packaging\ifw\license-zh.txt") -Destination (Join-Path $packageMeta "license.txt") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination (Join-Path $packageMeta "license_en.txt") -Force
$packageXmlTemplatePath = Join-Path $repoRoot "packaging\ifw\packages\com.vaporview.core\meta\package.xml.in"
$packageXml = [IO.File]::ReadAllText($packageXmlTemplatePath, [Text.Encoding]::UTF8)
$packageXml = $packageXml.Replace("@VAPORVIEW_VERSION@", $version).Replace("@VAPORVIEW_RELEASE_DATE@", $releaseDate)
$utf8NoBom = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $packageMeta "package.xml"), $packageXml, $utf8NoBom)
Copy-DirectoryContents $stageDir $packageData

$remoteRepositories = ""
if (-not [string]::IsNullOrWhiteSpace($RepositoryUrl)) {
    $escapedRepositoryUrl = [System.Security.SecurityElement]::Escape($RepositoryUrl)
    $remoteRepositories = "<RemoteRepositories>`n        <Repository>`n            <Url>$escapedRepositoryUrl</Url>`n            <Enabled>1</Enabled>`n        </Repository>`n    </RemoteRepositories>"
}
$configXml = (Get-Content -LiteralPath (Join-Path $repoRoot "packaging\ifw\config.xml.in") -Raw)
$configXml = $configXml.Replace("@VAPORVIEW_VERSION@", $version).Replace("@VAPORVIEW_TARGET_DIR@", "C:\VaporView").Replace("@VAPORVIEW_REMOTE_REPOSITORIES@", $remoteRepositories)
Set-Content -LiteralPath (Join-Path $workDir "config.xml") -Value $configXml -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $repoRoot "packaging\ifw\control.qs") -Destination $workDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "resources\VaproViewLOGO\VaporViewLOGO_black.ico") -Destination (Join-Path $workDir "VaporViewInstallerIcon.ico") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "resources\VaproViewLOGO\VaporViewLOGO_black_icon.png") -Destination (Join-Path $workDir "VaporViewInstallerIcon.png") -Force

$installerPath = Join-Path $outputDir ("VaporView-$version-win64-setup.exe")
if (Test-Path -LiteralPath $installerPath) {
    Remove-Item -LiteralPath $installerPath -Force
}
Invoke-Checked $binaryCreator @("--offline-only", "-c", (Join-Path $workDir "config.xml"), "-p", $packagesDir, $installerPath)
Invoke-Checked $editBin @("/SUBSYSTEM:WINDOWS", $installerPath)

if (-not $NoRepository) {
    $repositoryPath = Join-Path $outputDir "repository"
    if (Test-Path -LiteralPath $repositoryPath) {
        Remove-VerifiedDirectory $repositoryPath $outputDir
    }
    New-Item -ItemType Directory -Force -Path $repositoryPath | Out-Null
    Invoke-Checked $repoGenerator @("-p", $packagesDir, $repositoryPath)
}

Write-Host "Created: $installerPath"
Write-Host "Staging: $stageDir"
if (-not [string]::IsNullOrWhiteSpace($RepositoryUrl)) {
    Write-Host "Embedded maintenance repository: $RepositoryUrl"
} else {
    Write-Host "Offline installer only; no remote repository is queried during installation."
}
