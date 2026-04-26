[CmdletBinding()]
param(
    [ValidateSet("app", "firmware", "both")]
    [string]$Target,

    [ValidateSet("patch", "minor", "major")]
    [string]$BumpPart = "patch",

    [switch]$Yes
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-CommandAvailable {
    param([string]$CommandName)
    if (-not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
        throw "Required command '$CommandName' is not available in PATH."
    }
}

function Invoke-External {
    param(
        [string]$Name,
        [string]$WorkingDirectory,
        [string]$Executable,
        [string[]]$Arguments = @()
    )

    Write-Step $Name
    Push-Location $WorkingDirectory
    try {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $($LASTEXITCODE): $Executable $($Arguments -join ' ')"
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-GitCapture {
    param(
        [string]$RepoRoot,
        [string[]]$Arguments
    )

    $output = & git -C $RepoRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
    return @($output)
}

function Confirm-Action {
    param(
        [string]$Prompt,
        [bool]$DefaultYes = $false
    )

    if ($Yes) {
        return $true
    }

    $suffix = if ($DefaultYes) { "[Y/n]" } else { "[y/N]" }
    $raw = Read-Host "$Prompt $suffix"
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $DefaultYes
    }

    switch ($raw.Trim().ToLowerInvariant()) {
        "y" { return $true }
        "yes" { return $true }
        "n" { return $false }
        "no" { return $false }
        default { return $DefaultYes }
    }
}

function Parse-SemVer {
    param([string]$Value)

    if ($Value -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$') {
        throw "Invalid semver string: '$Value'"
    }

    return [pscustomobject]@{
        Major = [int]$matches.major
        Minor = [int]$matches.minor
        Patch = [int]$matches.patch
    }
}

function Format-SemVer {
    param([pscustomobject]$Version)
    return "{0}.{1}.{2}" -f $Version.Major, $Version.Minor, $Version.Patch
}

function Increment-SemVer {
    param(
        [string]$BaseVersion,
        [ValidateSet("patch", "minor", "major")]
        [string]$Part
    )

    $version = Parse-SemVer -Value $BaseVersion

    switch ($Part) {
        "patch" {
            $version.Patch += 1
        }
        "minor" {
            $version.Minor += 1
            $version.Patch = 0
        }
        "major" {
            $version.Major += 1
            $version.Minor = 0
            $version.Patch = 0
        }
    }

    return (Format-SemVer -Version $version)
}

function Get-LatestTag {
    param(
        [string]$RepoRoot,
        [string]$Prefix
    )

    $lines = Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @("tag", "--list", "$Prefix*", "--sort=-v:refname")
    foreach ($line in $lines) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            return $line.Trim()
        }
    }

    return $null
}

function Get-NextVersionFromTags {
    param(
        [string]$RepoRoot,
        [string]$Prefix,
        [string]$Part
    )

    $latestTag = Get-LatestTag -RepoRoot $RepoRoot -Prefix $Prefix
    if ([string]::IsNullOrWhiteSpace($latestTag)) {
        return [pscustomobject]@{
            LatestTag = "$Prefix" + "0.0.0"
            NextVersion = "0.0.1"
        }
    }

    if (-not $latestTag.StartsWith($Prefix, [System.StringComparison]::Ordinal)) {
        throw "Latest tag '$latestTag' does not start with '$Prefix'"
    }

    $rawVersion = $latestTag.Substring($Prefix.Length).TrimStart('v')
    $nextVersion = Increment-SemVer -BaseVersion $rawVersion -Part $Part

    return [pscustomobject]@{
        LatestTag = $latestTag
        NextVersion = $nextVersion
    }
}

function Replace-FirstMatchInFile {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Replacement,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "File not found: $Path"
    }

    $content = [System.IO.File]::ReadAllText($Path)
    $regex = [System.Text.RegularExpressions.Regex]::new($Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    $match = $regex.Match($content)
    if (-not $match.Success) {
        throw "Unable to update $Label in '$Path'"
    }

    $updated = $regex.Replace($content, $Replacement, 1)
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $updated, $utf8NoBom)
}

function Update-AppVersionFiles {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Write-Step "Updating app version files to $Version"

    $packageJsonPath = Join-Path $RepoRoot "apps/configurator/package.json"
    $cargoTomlPath = Join-Path $RepoRoot "apps/configurator/src-tauri/Cargo.toml"
    $tauriConfPath = Join-Path $RepoRoot "apps/configurator/src-tauri/tauri.conf.json"

    Replace-FirstMatchInFile -Path $packageJsonPath -Pattern '^(\s*"version"\s*:\s*")(\d+\.\d+\.\d+)("\s*,\s*)$' -Replacement ('${1}' + $Version + '${3}') -Label "package.json version"
    Replace-FirstMatchInFile -Path $cargoTomlPath -Pattern '^(version\s*=\s*")(\d+\.\d+\.\d+)("\s*)$' -Replacement ('${1}' + $Version + '${3}') -Label "Cargo.toml version"
    Replace-FirstMatchInFile -Path $tauriConfPath -Pattern '^(\s*"version"\s*:\s*")(\d+\.\d+\.\d+)("\s*,\s*)$' -Replacement ('${1}' + $Version + '${3}') -Label "tauri.conf.json version"
}

function Update-FirmwareVersionFile {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Write-Step "Updating firmware version defines to $Version"

    $settingsPath = Join-Path $RepoRoot "firmware/Core/Src/settings.c"
    $parsed = Parse-SemVer -Value $Version

    Replace-FirstMatchInFile -Path $settingsPath -Pattern '^(#define\s+FIRMWARE_VERSION_MAJOR\s+)\d+u\s*$' -Replacement ('${1}' + $parsed.Major + 'u') -Label "FIRMWARE_VERSION_MAJOR"
    Replace-FirstMatchInFile -Path $settingsPath -Pattern '^(#define\s+FIRMWARE_VERSION_MINOR\s+)\d+u\s*$' -Replacement ('${1}' + $parsed.Minor + 'u') -Label "FIRMWARE_VERSION_MINOR"
    Replace-FirstMatchInFile -Path $settingsPath -Pattern '^(#define\s+FIRMWARE_VERSION_PATCH\s+)\d+u\s*$' -Replacement ('${1}' + $parsed.Patch + 'u') -Label "FIRMWARE_VERSION_PATCH"
}

function Run-AppChecks {
    param([string]$RepoRoot)

    $appDir = Join-Path $RepoRoot "apps/configurator"
    $tauriDir = Join-Path $appDir "src-tauri"

    Invoke-External -Name "App: bun install" -WorkingDirectory $appDir -Executable "bun" -Arguments @("install")
    Invoke-External -Name "App: cargo check (refresh Cargo.lock)" -WorkingDirectory $tauriDir -Executable "cargo" -Arguments @("check")
    Invoke-External -Name "App: bun run lint" -WorkingDirectory $appDir -Executable "bun" -Arguments @("run", "lint")
    Invoke-External -Name "App: bun run build" -WorkingDirectory $appDir -Executable "bun" -Arguments @("run", "build")
    Invoke-External -Name "App: cargo check --locked" -WorkingDirectory $tauriDir -Executable "cargo" -Arguments @("check", "--locked")
}

function Run-FirmwareChecks {
    param([string]$RepoRoot)

    Invoke-External -Name "Firmware: cmake --preset Release" -WorkingDirectory $RepoRoot -Executable "cmake" -Arguments @("--preset", "Release")
    Invoke-External -Name "Firmware: cmake --build --preset Release" -WorkingDirectory $RepoRoot -Executable "cmake" -Arguments @("--build", "--preset", "Release")
    Invoke-External -Name "Firmware: cmake --preset Release-apponly" -WorkingDirectory $RepoRoot -Executable "cmake" -Arguments @("--preset", "Release-apponly")
    Invoke-External -Name "Firmware: cmake --build --preset Release-apponly" -WorkingDirectory $RepoRoot -Executable "cmake" -Arguments @("--build", "--preset", "Release-apponly")
}

function Select-TargetInteractive {
    Write-Host ""
    Write-Host "Select what to release:" -ForegroundColor Yellow
    Write-Host "  1) app"
    Write-Host "  2) firmware"
    Write-Host "  3) both"

    while ($true) {
        $choice = Read-Host "Choice"
        switch ($choice.Trim()) {
            "1" { return "app" }
            "2" { return "firmware" }
            "3" { return "both" }
            default { Write-Host "Invalid choice. Enter 1, 2 or 3." -ForegroundColor Red }
        }
    }
}

function Get-ReleaseCommitSummary {
    param(
        [string]$TargetChoice,
        [string]$AppVersion,
        [string]$FirmwareVersion
    )

    switch ($TargetChoice) {
        "app" { return "release(app): v$AppVersion" }
        "firmware" { return "release(firmware): v$FirmwareVersion" }
        "both" { return "release(app+firmware): app v$AppVersion, firmware v$FirmwareVersion" }
        default { throw "Unhandled target choice: $TargetChoice" }
    }
}

$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptPath "../..")).Path

Write-Step "Preflight checks"
Assert-CommandAvailable -CommandName "git"

if (-not $Target) {
    $Target = Select-TargetInteractive
}

$needsApp = $Target -eq "app" -or $Target -eq "both"
$needsFirmware = $Target -eq "firmware" -or $Target -eq "both"

if ($needsApp) {
    Assert-CommandAvailable -CommandName "bun"
    Assert-CommandAvailable -CommandName "cargo"
}
if ($needsFirmware) {
    Assert-CommandAvailable -CommandName "cmake"
}

Invoke-External -Name "Fetch remote tags" -WorkingDirectory $repoRoot -Executable "git" -Arguments @("fetch", "--tags", "origin")

$branchName = (Invoke-GitCapture -RepoRoot $repoRoot -Arguments @("rev-parse", "--abbrev-ref", "HEAD") | Select-Object -First 1).Trim()
if ($branchName -eq "HEAD") {
    throw "Detached HEAD detected. Checkout a branch before running this script."
}

$appVersionInfo = $null
$firmwareVersionInfo = $null

if ($needsApp) {
    $appVersionInfo = Get-NextVersionFromTags -RepoRoot $repoRoot -Prefix "app-v" -Part $BumpPart
}
if ($needsFirmware) {
    $firmwareVersionInfo = Get-NextVersionFromTags -RepoRoot $repoRoot -Prefix "firmware-v" -Part $BumpPart
}

Write-Step "Release plan"
Write-Host "Branch: $branchName"
Write-Host "Target: $Target"
Write-Host "Bump part: $BumpPart"
if ($needsApp) {
    Write-Host "App tag: $($appVersionInfo.LatestTag) -> app-v$($appVersionInfo.NextVersion)"
}
if ($needsFirmware) {
    Write-Host "Firmware tag: $($firmwareVersionInfo.LatestTag) -> firmware-v$($firmwareVersionInfo.NextVersion)"
}

$plannedTags = @()
if ($needsApp) {
    $plannedTags += "app-v$($appVersionInfo.NextVersion)"
}
if ($needsFirmware) {
    $plannedTags += "firmware-v$($firmwareVersionInfo.NextVersion)"
}

foreach ($plannedTag in $plannedTags) {
    $existingTag = @(Invoke-GitCapture -RepoRoot $repoRoot -Arguments @("tag", "--list", $plannedTag))
    if ($existingTag.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($existingTag[0])) {
        throw "Tag '$plannedTag' already exists. Bump version and retry."
    }
}

if (-not (Confirm-Action -Prompt "Apply version updates and run local checks?" -DefaultYes:$false)) {
    throw "Aborted by user."
}

if ($needsApp) {
    Update-AppVersionFiles -RepoRoot $repoRoot -Version $appVersionInfo.NextVersion
}
if ($needsFirmware) {
    Update-FirmwareVersionFile -RepoRoot $repoRoot -Version $firmwareVersionInfo.NextVersion
}

if ($needsApp) {
    Run-AppChecks -RepoRoot $repoRoot
}
if ($needsFirmware) {
    Run-FirmwareChecks -RepoRoot $repoRoot
}

Write-Step "Git status before commit"
Invoke-External -Name "Show git status" -WorkingDirectory $repoRoot -Executable "git" -Arguments @("status", "--short")

$defaultSummary = Get-ReleaseCommitSummary -TargetChoice $Target -AppVersion (${appVersionInfo}?.NextVersion) -FirmwareVersion (${firmwareVersionInfo}?.NextVersion)
$summaryInput = Read-Host "Commit summary (default: $defaultSummary)"
$commitSummary = if ([string]::IsNullOrWhiteSpace($summaryInput)) { $defaultSummary } else { $summaryInput.Trim() }
$commitDescription = Read-Host "Commit description (optional)"

if (-not (Confirm-Action -Prompt "Commit and push branch '$branchName'?" -DefaultYes:$false)) {
    throw "Aborted by user before commit/push."
}

Invoke-External -Name "Stage all pending changes" -WorkingDirectory $repoRoot -Executable "git" -Arguments @("add", "-A")

& git -C $repoRoot diff --cached --quiet
switch ($LASTEXITCODE) {
    0 { throw "No staged changes to commit after updates." }
    1 { }
    default { throw "git diff --cached --quiet failed with exit code $LASTEXITCODE" }
}

$commitArgs = @("commit", "-m", $commitSummary)
if (-not [string]::IsNullOrWhiteSpace($commitDescription)) {
    $commitArgs += @("-m", $commitDescription)
}
Invoke-External -Name "Create release commit" -WorkingDirectory $repoRoot -Executable "git" -Arguments $commitArgs

Invoke-External -Name "Push commit to origin/$branchName" -WorkingDirectory $repoRoot -Executable "git" -Arguments @("push", "origin", $branchName)

Write-Step "Create and push tags"
foreach ($tag in $plannedTags) {
    $tagTitle = "Release $tag"
    $tagArgs = @("tag", "-a", $tag, "-m", $tagTitle)
    if (-not [string]::IsNullOrWhiteSpace($commitDescription)) {
        $tagArgs += @("-m", $commitDescription)
    }
    Invoke-External -Name "Create tag $tag" -WorkingDirectory $repoRoot -Executable "git" -Arguments $tagArgs
    Invoke-External -Name "Push tag $tag" -WorkingDirectory $repoRoot -Executable "git" -Arguments @("push", "origin", $tag)
}

Write-Host "" 
Write-Host "Release automation completed successfully." -ForegroundColor Green
if ($needsApp) {
    Write-Host "Created tag: app-v$($appVersionInfo.NextVersion)"
}
if ($needsFirmware) {
    Write-Host "Created tag: firmware-v$($firmwareVersionInfo.NextVersion)"
}
