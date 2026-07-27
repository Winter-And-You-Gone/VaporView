param(
    [string]$RepositoryDirectory,
    [string]$PagesPath = "ifw/windows/x64/repository",
    [string]$Branch = "gh-pages",
    [string]$Remote = "origin",
    [string]$CommitMessage,
    [switch]$KeepWorktree
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path (Join-Path $repoRoot "build") "Release"
$sourceRepository = if ([string]::IsNullOrWhiteSpace($RepositoryDirectory)) {
    Join-Path (Join-Path $buildDir "ifw") "repository"
} else {
    [IO.Path]::GetFullPath($RepositoryDirectory)
}
$worktreeDir = Join-Path $buildDir "ifw-gh-pages-worktree"
$targetPathParts = $PagesPath -split '[\\/]+' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
if ($targetPathParts.Count -eq 0) {
    throw "PagesPath must not be empty."
}
if ($targetPathParts | Where-Object { $_ -eq "." -or $_ -eq ".." -or $_.Contains(":") }) {
    throw "PagesPath must be a safe relative path."
}
$normalizedPagesPath = ($targetPathParts -join [IO.Path]::DirectorySeparatorChar)

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

function Remove-VerifiedDirectory {
    param([Parameter(Mandatory = $true)][string]$Path,
          [Parameter(Mandatory = $true)][string]$AllowedRoot)
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    $resolvedPath = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
    $resolvedRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $AllowedRoot).Path)
    $rootPrefix = $resolvedRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolvedPath -eq $resolvedRoot -or
        -not $resolvedPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove directory outside allowed root: $resolvedPath"
    }
    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

function Copy-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Source,
          [Parameter(Mandatory = $true)][string]$Destination)
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $sourceRepository -PathType Container)) {
    throw "IFW repository directory was not found: $sourceRepository"
}
if (-not (Test-Path -LiteralPath (Join-Path $sourceRepository "Updates.xml") -PathType Leaf)) {
    throw "IFW repository is missing Updates.xml: $sourceRepository"
}
if (Test-Path -LiteralPath $worktreeDir) {
    Invoke-Git @("worktree", "remove", "--force", $worktreeDir)
}

$remoteBranchExists = $false
& git ls-remote --exit-code --heads $Remote $Branch *> $null
if ($LASTEXITCODE -eq 0) {
    $remoteBranchExists = $true
} elseif ($LASTEXITCODE -ne 2) {
    throw "Failed to query $Remote/$Branch."
}

if ($remoteBranchExists) {
    Invoke-Git @("fetch", $Remote, ("{0}:refs/remotes/{1}/{0}" -f $Branch, $Remote))
    Invoke-Git @("worktree", "add", "-B", $Branch, $worktreeDir, "$Remote/$Branch")
} else {
    Invoke-Git @("worktree", "add", "--orphan", "-B", $Branch, $worktreeDir)
}

try {
    $targetRepository = Join-Path $worktreeDir $normalizedPagesPath
    Remove-VerifiedDirectory $targetRepository $worktreeDir
    Copy-DirectoryContents $sourceRepository $targetRepository
    Set-Content -LiteralPath (Join-Path $worktreeDir ".nojekyll") -Value "" -Encoding ASCII

    Push-Location $worktreeDir
    try {
        Invoke-Git @("add", ".nojekyll", $normalizedPagesPath)
        $hasChanges = $false
        & git diff --cached --quiet
        if ($LASTEXITCODE -eq 1) {
            $hasChanges = $true
        } elseif ($LASTEXITCODE -ne 0) {
            throw "git diff --cached --quiet failed with exit code $LASTEXITCODE."
        }

        if ($hasChanges) {
            if ([string]::IsNullOrWhiteSpace($CommitMessage)) {
                $CommitMessage = "Publish VaporView IFW repository"
            }
            Invoke-Git @("commit", "-m", $CommitMessage)
            Invoke-Git @("push", $Remote, "${Branch}:$Branch")
            Write-Host "Published IFW repository to $Remote/$Branch at $PagesPath"
        } else {
            Write-Host "IFW repository is already up to date at ${Remote}/${Branch}:$PagesPath"
        }
    } finally {
        Pop-Location
    }
} finally {
    if (-not $KeepWorktree) {
        Invoke-Git @("worktree", "remove", "--force", $worktreeDir)
    }
}
