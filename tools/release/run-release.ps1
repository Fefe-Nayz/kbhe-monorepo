[CmdletBinding()]
param(
    [ValidateSet("app", "firmware", "both")]
    [string]$Target,

    [ValidateSet("patch", "minor", "major")]
    [string]$BumpPart = "patch",

    [ValidateSet("prepare", "publish", "all")]
    [string]$Phase = "prepare",

    [string]$ReleaseNotes,

    [switch]$UseWsl,

    [string]$WslDistribution = "Ubuntu",

    [switch]$Yes
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:WslBuildCommands = @("bun", "cargo", "cmake", "ctest", "python")

function Resolve-WslBuildCommand {
    param([string]$CommandName)

    switch ($CommandName) {
        "bun" {
            if (-not [string]::IsNullOrWhiteSpace($env:KBHE_WSL_BUN)) {
                return $env:KBHE_WSL_BUN
            }
        }
        "cargo" {
            if (-not [string]::IsNullOrWhiteSpace($env:KBHE_WSL_CARGO)) {
                return $env:KBHE_WSL_CARGO
            }
        }
        "python" { return "/usr/bin/python3" }
    }
    return $CommandName
}

function Get-WslEnvironmentArguments {
    $arguments = [Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:KBHE_WSL_RUSTUP_HOME)) {
        $arguments.Add("RUSTUP_HOME=$($env:KBHE_WSL_RUSTUP_HOME)")
    }
    if (-not [string]::IsNullOrWhiteSpace($env:KBHE_WSL_CARGO_HOME)) {
        $arguments.Add("CARGO_HOME=$($env:KBHE_WSL_CARGO_HOME)")
    }
    if (-not [string]::IsNullOrWhiteSpace($env:KBHE_WSL_CARGO_TARGET_DIR)) {
        $arguments.Add("CARGO_TARGET_DIR=$($env:KBHE_WSL_CARGO_TARGET_DIR)")
    }
    return @($arguments)
}

function Invoke-WslBuildCommand {
    param(
        [string]$WorkingDirectory,
        [string]$Executable,
        [string[]]$Arguments = @()
    )

    $resolved = Resolve-WslBuildCommand -CommandName $Executable
    $wslArguments = [Collections.Generic.List[string]]::new()
    foreach ($argument in @("-d", $WslDistribution, "--cd", $WorkingDirectory, "--exec")) {
        $wslArguments.Add($argument)
    }
    $environmentArguments = @(Get-WslEnvironmentArguments)
    if ($environmentArguments.Count -gt 0) {
        $wslArguments.Add("/usr/bin/env")
        foreach ($argument in $environmentArguments) {
            $wslArguments.Add($argument)
        }
    }
    $wslArguments.Add($resolved)
    foreach ($argument in $Arguments) {
        $wslArguments.Add($argument)
    }

    & wsl.exe @wslArguments
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-CommandAvailable {
    param([string]$CommandName)

    if ($UseWsl -and $CommandName -in $script:WslBuildCommands) {
        Invoke-WslBuildCommand -WorkingDirectory $repoRoot `
            -Executable $CommandName -Arguments @("--version") *> $null
        if ($LASTEXITCODE -ne 0) {
            throw "Required WSL command '$CommandName' is not available in distribution '$WslDistribution'."
        }
        return
    }
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
        if ($UseWsl -and $Executable -in $script:WslBuildCommands) {
            Invoke-WslBuildCommand -WorkingDirectory $WorkingDirectory `
                -Executable $Executable -Arguments $Arguments
        }
        else {
            & $Executable @Arguments
        }
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

function Read-MultilineInput {
    param(
        [string]$Prompt,
        [string]$Terminator = "."
    )

    Write-Host $Prompt -ForegroundColor Yellow
    Write-Host "  - Enter on an empty first line: skip"
    Write-Host "  - Type '$Terminator' on its own line to finish"
    $lines = [System.Collections.Generic.List[string]]::new()
    while ($true) {
        $line = Read-Host
        if ($lines.Count -eq 0 -and [string]::IsNullOrWhiteSpace($line)) {
            return ""
        }
        if ($line -eq $Terminator) {
            break
        }
        $lines.Add($line)
    }
    return ($lines -join [Environment]::NewLine)
}

function Parse-SemVer {
    param([string]$Value)

    if ($Value -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Invalid canonical semver string: '$Value'"
    }
    return [pscustomobject]@{
        Major = [int]($Value.Split('.')[0])
        Minor = [int]($Value.Split('.')[1])
        Patch = [int]($Value.Split('.')[2])
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
        "patch" { $version.Patch += 1 }
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

    $lines = Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @(
        "tag", "--list", "$Prefix*", "--sort=-v:refname"
    )
    $escapedPrefix = [Regex]::Escape($Prefix)
    foreach ($line in $lines) {
        $candidate = $line.Trim()
        if ($candidate -match "^$escapedPrefix(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$") {
            return $candidate
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
    $rawVersion = $latestTag.Substring($Prefix.Length)
    return [pscustomobject]@{
        LatestTag = $latestTag
        NextVersion = Increment-SemVer -BaseVersion $rawVersion -Part $Part
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
    $content = [IO.File]::ReadAllText($Path)
    $regex = [Regex]::new($Pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $regex.IsMatch($content)) {
        throw "Unable to update $Label in '$Path'"
    }
    $updated = $regex.Replace($content, $Replacement, 1)
    [IO.File]::WriteAllText($Path, $updated, [Text.UTF8Encoding]::new($false))
}

function Update-AppVersionFiles {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Write-Step "Updating app version files to $Version"
    Replace-FirstMatchInFile `
        -Path (Join-Path $RepoRoot "apps/configurator/package.json") `
        -Pattern '^(\s*"version"\s*:\s*")(\d+\.\d+\.\d+)("\s*,\s*)$' `
        -Replacement ('${1}' + $Version + '${3}') -Label "package.json version"
    Replace-FirstMatchInFile `
        -Path (Join-Path $RepoRoot "apps/configurator/src-tauri/Cargo.toml") `
        -Pattern '^(version\s*=\s*")(\d+\.\d+\.\d+)("\s*)$' `
        -Replacement ('${1}' + $Version + '${3}') -Label "Cargo.toml version"
    Replace-FirstMatchInFile `
        -Path (Join-Path $RepoRoot "apps/configurator/src-tauri/tauri.conf.json") `
        -Pattern '^(\s*"version"\s*:\s*")(\d+\.\d+\.\d+)("\s*,\s*)$' `
        -Replacement ('${1}' + $Version + '${3}') -Label "tauri.conf.json version"
}

function Update-FirmwareVersionFile {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Write-Step "Updating firmware version defines to $Version"
    $versionHeaderPath = Join-Path $RepoRoot "firmware/Core/Inc/firmware_version.h"
    $parsed = Parse-SemVer -Value $Version
    if ($parsed.Major -gt 255 -or $parsed.Minor -gt 255 -or $parsed.Patch -gt 255) {
        throw "Firmware version components must fit one byte: $Version"
    }
    Replace-FirstMatchInFile -Path $versionHeaderPath `
        -Pattern '^(#define\s+FIRMWARE_VERSION_MAJOR\s+)\d+u\s*$' `
        -Replacement ('${1}' + $parsed.Major + 'u') -Label "FIRMWARE_VERSION_MAJOR"
    Replace-FirstMatchInFile -Path $versionHeaderPath `
        -Pattern '^(#define\s+FIRMWARE_VERSION_MINOR\s+)\d+u\s*$' `
        -Replacement ('${1}' + $parsed.Minor + 'u') -Label "FIRMWARE_VERSION_MINOR"
    Replace-FirstMatchInFile -Path $versionHeaderPath `
        -Pattern '^(#define\s+FIRMWARE_VERSION_PATCH\s+)\d+u\s*$' `
        -Replacement ('${1}' + $parsed.Patch + 'u') -Label "FIRMWARE_VERSION_PATCH"
}

function Get-AppSourceVersion {
    param([string]$RepoRoot)

    $package = (Get-Content (Join-Path $RepoRoot "apps/configurator/package.json") -Raw |
        ConvertFrom-Json).version
    $tauri = (Get-Content (Join-Path $RepoRoot "apps/configurator/src-tauri/tauri.conf.json") -Raw |
        ConvertFrom-Json).version
    $cargoContent = Get-Content (Join-Path $RepoRoot "apps/configurator/src-tauri/Cargo.toml") -Raw
    $cargoMatch = [Regex]::Match($cargoContent, '(?m)^version\s*=\s*"([^"]+)"')
    if (-not $cargoMatch.Success) {
        throw "Unable to read the Cargo package version"
    }
    $cargo = $cargoMatch.Groups[1].Value
    [void](Parse-SemVer -Value $package)
    if ($package -ne $tauri -or $package -ne $cargo) {
        throw "App versions disagree: package=$package tauri=$tauri cargo=$cargo"
    }
    return $package
}

function Get-FirmwareSourceVersion {
    param([string]$RepoRoot)

    $content = Get-Content (Join-Path $RepoRoot "firmware/Core/Inc/firmware_version.h") -Raw
    $parts = @{}
    foreach ($name in @("MAJOR", "MINOR", "PATCH")) {
        $match = [Regex]::Match($content, "(?m)^#define\s+FIRMWARE_VERSION_$name\s+(\d+)u\s*$")
        if (-not $match.Success) {
            throw "Unable to read FIRMWARE_VERSION_$name"
        }
        $parts[$name] = [int]$match.Groups[1].Value
        if ($parts[$name] -gt 255) {
            throw "FIRMWARE_VERSION_$name does not fit one byte"
        }
    }
    $version = "$($parts.MAJOR).$($parts.MINOR).$($parts.PATCH)"
    [void](Parse-SemVer -Value $version)
    return $version
}

function Run-AppChecks {
    param([string]$RepoRoot)

    $appDir = Join-Path $RepoRoot "apps/configurator"
    $tauriDir = Join-Path $appDir "src-tauri"
    Invoke-External -Name "App: bun install" -WorkingDirectory $appDir `
        -Executable "bun" -Arguments @("install")
    Invoke-External -Name "App: cargo check (refresh Cargo.lock)" -WorkingDirectory $tauriDir `
        -Executable "cargo" -Arguments @("check")
    Invoke-External -Name "App: bun test" -WorkingDirectory $appDir `
        -Executable "bun" -Arguments @("test")
    Invoke-External -Name "App: bun run lint" -WorkingDirectory $appDir `
        -Executable "bun" -Arguments @("run", "lint")
    Invoke-External -Name "App: bun run build" -WorkingDirectory $appDir `
        -Executable "bun" -Arguments @("run", "build")
    Invoke-External -Name "App: cargo check --locked" -WorkingDirectory $tauriDir `
        -Executable "cargo" -Arguments @("check", "--locked")
    Invoke-External -Name "App: cargo test --locked" -WorkingDirectory $tauriDir `
        -Executable "cargo" -Arguments @("test", "--locked")
}

function Run-FirmwareChecks {
    param([string]$RepoRoot)

    Invoke-External -Name "Firmware: configure host tests" -WorkingDirectory $RepoRoot `
        -Executable "cmake" -Arguments @(
            "-S", "firmware/tests", "-B", "firmware/build-host-tests-release",
            "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug"
        )
    Invoke-External -Name "Firmware: build host tests" -WorkingDirectory $RepoRoot `
        -Executable "cmake" -Arguments @("--build", "firmware/build-host-tests-release")
    Invoke-External -Name "Firmware: run host tests" -WorkingDirectory $RepoRoot `
        -Executable "ctest" -Arguments @(
            "--test-dir", "firmware/build-host-tests-release", "--output-on-failure"
        )
    Invoke-External -Name "Firmware: release-signing vector tests" -WorkingDirectory $RepoRoot `
        -Executable "python" -Arguments @("tools/release/test_release_signing.py")
    Invoke-External -Name "Firmware: updater-selection tests" -WorkingDirectory $RepoRoot `
        -Executable "python" -Arguments @("tools/host/test_firmware_updater_selection.py")
    Invoke-External -Name "Firmware: device-protocol tests" -WorkingDirectory $RepoRoot `
        -Executable "python" -Arguments @("tools/host/test_device_protocol.py")
    Invoke-External -Name "Firmware: HIL logger tests" -WorkingDirectory $RepoRoot `
        -Executable "python" -Arguments @("tools/host/test_hil_input_logger.py")
    Invoke-External -Name "Firmware: bounded input diagnostic tests" -WorkingDirectory $RepoRoot `
        -Executable "python" -Arguments @("tools/host/test_kbhe_input_diagnostic.py")
    Invoke-External -Name "Firmware: all-key input trace tests" -WorkingDirectory $RepoRoot `
        -Executable "python" -Arguments @("tools/host/test_input_trace.py")
    Invoke-External -Name "Firmware: cmake --preset Release" -WorkingDirectory $RepoRoot `
        -Executable "cmake" -Arguments @("--preset", "Release")
    Invoke-External -Name "Firmware: cmake --build --preset Release" -WorkingDirectory $RepoRoot `
        -Executable "cmake" -Arguments @("--build", "--preset", "Release")
    Invoke-External -Name "Firmware: cmake --preset Release-apponly" -WorkingDirectory $RepoRoot `
        -Executable "cmake" -Arguments @("--preset", "Release-apponly")
    Invoke-External -Name "Firmware: cmake --build --preset Release-apponly" -WorkingDirectory $RepoRoot `
        -Executable "cmake" -Arguments @("--build", "--preset", "Release-apponly")
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

function Assert-CleanMainAtOrigin {
    param([string]$RepoRoot)

    $status = @(Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @(
        "status", "--porcelain=v1", "--untracked-files=all"
    ))
    if ($status.Count -gt 0) {
        throw "Release automation requires a clean working tree. Commit or stash existing changes first."
    }
    $branch = (Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @(
        "rev-parse", "--abbrev-ref", "HEAD"
    ) | Select-Object -First 1).Trim()
    if ($branch -ne "main") {
        throw "Release automation only runs from main; current branch is '$branch'."
    }
    Invoke-External -Name "Fetch origin/main and release tags" -WorkingDirectory $RepoRoot `
        -Executable "git" -Arguments @("fetch", "origin", "main", "--tags")
    $local = (Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @("rev-parse", "HEAD") |
        Select-Object -First 1).Trim()
    $remote = (Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @("rev-parse", "origin/main") |
        Select-Object -First 1).Trim()
    if ($local -ne $remote) {
        throw "Local main must exactly match origin/main before this phase (local=$local remote=$remote)."
    }
}

function Get-AllowedReleasePaths {
    param([string]$TargetChoice)

    $paths = [Collections.Generic.List[string]]::new()
    if ($TargetChoice -in @("app", "both")) {
        foreach ($path in @(
            "apps/configurator/package.json",
            "apps/configurator/bun.lock",
            "apps/configurator/src-tauri/Cargo.toml",
            "apps/configurator/src-tauri/Cargo.lock",
            "apps/configurator/src-tauri/tauri.conf.json"
        )) {
            $paths.Add($path)
        }
    }
    if ($TargetChoice -in @("firmware", "both")) {
        $paths.Add("firmware/Core/Inc/firmware_version.h")
    }
    return @($paths)
}

function Assert-OnlyAllowedReleaseChanges {
    param(
        [string]$RepoRoot,
        [string[]]$AllowedPaths
    )

    $allowed = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal
    )
    foreach ($path in $AllowedPaths) {
        [void]$allowed.Add($path)
    }
    $unexpected = [Collections.Generic.List[string]]::new()
    $status = @(Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @(
        "status", "--porcelain=v1", "--untracked-files=all"
    ))
    foreach ($line in $status) {
        if ($line.Length -lt 4) {
            $unexpected.Add($line)
            continue
        }
        $path = $line.Substring(3)
        if ($path.StartsWith('"') -or $path.Contains(" -> ") -or -not $allowed.Contains($path)) {
            $unexpected.Add($line)
        }
    }
    if ($unexpected.Count -gt 0) {
        throw "Release checks changed files outside the staging whitelist:`n$($unexpected -join [Environment]::NewLine)"
    }
}

function Wait-GitHubWorkflowForCommit {
    param(
        [string]$RepoRoot,
        [string]$Workflow,
        [string]$Commit,
        [ValidateSet("push", "workflow_dispatch")]
        [string]$Event,
        [DateTimeOffset]$NotBefore = [DateTimeOffset]::MinValue
    )

    Write-Step "Wait for $Workflow on $Commit"
    $run = $null
    for ($attempt = 0; $attempt -lt 120 -and $null -eq $run; $attempt++) {
        Push-Location $RepoRoot
        try {
            $json = & gh run list --workflow $Workflow --branch main --event $Event `
                --limit 30 --json databaseId,headSha,status,conclusion,url,createdAt
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to list GitHub Actions runs for $Workflow"
            }
        }
        finally {
            Pop-Location
        }
        $runs = @($json | ConvertFrom-Json)
        $run = $runs |
            Where-Object {
                $_.headSha -eq $Commit -and
                ([DateTimeOffset]$_.createdAt) -ge $NotBefore
            } |
            Sort-Object { [DateTimeOffset]$_.createdAt } -Descending |
            Select-Object -First 1
        if ($null -eq $run) {
            Start-Sleep -Seconds 5
        }
    }
    if ($null -eq $run) {
        throw "No $Event run of $Workflow appeared for commit $Commit within 10 minutes."
    }
    Write-Host "Run: $($run.url)"
    Invoke-External -Name "Watch $Workflow" -WorkingDirectory $RepoRoot `
        -Executable "gh" -Arguments @("run", "watch", "$($run.databaseId)", "--exit-status")
}

function Invoke-SigningPreflight {
    param(
        [string]$RepoRoot,
        [string]$TargetChoice,
        [string]$Commit
    )

    $notBefore = [DateTimeOffset]::UtcNow.AddSeconds(-5)
    Invoke-External -Name "Dispatch protected release-signing preflight" `
        -WorkingDirectory $RepoRoot -Executable "gh" -Arguments @(
            "workflow", "run", "release-signing-preflight.yml",
            "--ref", "main", "-f", "target=$TargetChoice"
        )
    Write-Host "The preflight jobs may wait for the app-codesign, app-publish, and/or firmware-release environment reviewers."
    Wait-GitHubWorkflowForCommit -RepoRoot $RepoRoot `
        -Workflow "release-signing-preflight.yml" -Commit $Commit `
        -Event "workflow_dispatch" -NotBefore $notBefore
}

function Assert-TagAndReleaseDoNotExist {
    param(
        [string]$RepoRoot,
        [string]$Tag
    )

    $existingTag = @(Invoke-GitCapture -RepoRoot $RepoRoot -Arguments @(
        "tag", "--list", $Tag
    ))
    if ($existingTag.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($existingTag[0])) {
        throw "Tag '$Tag' already exists. Bump the version and prepare another release."
    }
    Push-Location $RepoRoot
    try {
        & gh release view $Tag *> $null
        if ($LASTEXITCODE -eq 0) {
            throw "GitHub Release '$Tag' already exists. Refusing to replace it."
        }
    }
    finally {
        Pop-Location
    }
}

$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptPath "../..")).Path

Write-Step "Preflight checks"
Assert-CommandAvailable -CommandName "git"
Assert-CommandAvailable -CommandName "gh"
if (-not $Target) {
    $Target = Select-TargetInteractive
}
$needsApp = $Target -in @("app", "both")
$needsFirmware = $Target -in @("firmware", "both")
if ($Phase -in @("prepare", "all")) {
    if ($needsApp) {
        Assert-CommandAvailable -CommandName "bun"
        Assert-CommandAvailable -CommandName "cargo"
    }
    if ($needsFirmware) {
        foreach ($command in @("cmake", "ctest", "python")) {
            Assert-CommandAvailable -CommandName $command
        }
    }
}

Assert-CleanMainAtOrigin -RepoRoot $repoRoot

if ($Phase -in @("prepare", "all")) {
    $appVersionInfo = if ($needsApp) {
        Get-NextVersionFromTags -RepoRoot $repoRoot -Prefix "app-v" -Part $BumpPart
    } else { $null }
    $firmwareVersionInfo = if ($needsFirmware) {
        Get-NextVersionFromTags -RepoRoot $repoRoot -Prefix "firmware-v" -Part $BumpPart
    } else { $null }

    Write-Step "Prepare release plan"
    Write-Host "Target: $Target"
    Write-Host "Bump part: $BumpPart"
    if ($needsApp) {
        Write-Host "App: $($appVersionInfo.LatestTag) -> app-v$($appVersionInfo.NextVersion)"
    }
    if ($needsFirmware) {
        Write-Host "Firmware: $($firmwareVersionInfo.LatestTag) -> firmware-v$($firmwareVersionInfo.NextVersion)"
    }
    if (-not (Confirm-Action -Prompt "Apply version updates and run every documented local check?")) {
        throw "Aborted by user."
    }

    if ($needsApp) {
        Update-AppVersionFiles -RepoRoot $repoRoot -Version $appVersionInfo.NextVersion
        Run-AppChecks -RepoRoot $repoRoot
    }
    if ($needsFirmware) {
        Update-FirmwareVersionFile -RepoRoot $repoRoot -Version $firmwareVersionInfo.NextVersion
        Run-FirmwareChecks -RepoRoot $repoRoot
    }

    $allowedPaths = Get-AllowedReleasePaths -TargetChoice $Target
    Assert-OnlyAllowedReleaseChanges -RepoRoot $repoRoot -AllowedPaths $allowedPaths
    Invoke-External -Name "Show whitelisted release changes" -WorkingDirectory $repoRoot `
        -Executable "git" -Arguments @("status", "--short")

    $appVersion = if ($needsApp) { $appVersionInfo.NextVersion } else { $null }
    $firmwareVersion = if ($needsFirmware) { $firmwareVersionInfo.NextVersion } else { $null }
    $defaultSummary = Get-ReleaseCommitSummary -TargetChoice $Target `
        -AppVersion $appVersion -FirmwareVersion $firmwareVersion
    $commitSummary = $defaultSummary
    $commitDescription = ""
    if (-not $Yes) {
        $summaryInput = Read-Host "Commit summary (default: $defaultSummary)"
        if (-not [string]::IsNullOrWhiteSpace($summaryInput)) {
            $commitSummary = $summaryInput.Trim()
        }
        $commitDescription = Read-MultilineInput -Prompt "Commit description (optional)"
    }
    if (-not (Confirm-Action -Prompt "Create the release-preparation commit and push main?")) {
        throw "Aborted before commit/push."
    }

    Invoke-External -Name "Stage only release version and lock files" `
        -WorkingDirectory $repoRoot -Executable "git" `
        -Arguments (@("add", "--") + $allowedPaths)
    $staged = @(Invoke-GitCapture -RepoRoot $repoRoot -Arguments @(
        "diff", "--cached", "--name-only"
    ))
    if ($staged.Count -eq 0) {
        throw "No staged release changes were produced."
    }
    foreach ($path in $staged) {
        if ($path -notin $allowedPaths) {
            throw "Unexpected staged path: $path"
        }
    }
    $commitArgs = @("commit", "-m", $commitSummary)
    if (-not [string]::IsNullOrWhiteSpace($commitDescription)) {
        $commitArgs += @("-m", $commitDescription)
    }
    Invoke-External -Name "Create release-preparation commit" -WorkingDirectory $repoRoot `
        -Executable "git" -Arguments $commitArgs
    Invoke-External -Name "Push release-preparation commit to main" -WorkingDirectory $repoRoot `
        -Executable "git" -Arguments @("push", "origin", "main")

    $releaseCommit = (Invoke-GitCapture -RepoRoot $repoRoot -Arguments @("rev-parse", "HEAD") |
        Select-Object -First 1).Trim()
    if ($needsApp) {
        Wait-GitHubWorkflowForCommit -RepoRoot $repoRoot -Workflow "configurator.yml" `
            -Commit $releaseCommit -Event "push"
    }
    if ($needsFirmware) {
        Wait-GitHubWorkflowForCommit -RepoRoot $repoRoot -Workflow "firmware.yml" `
            -Commit $releaseCommit -Event "push"
    }

    Write-Host ""
    Write-Host "Release preparation is green at $releaseCommit." -ForegroundColor Green
    if ($Phase -eq "prepare") {
        Write-Host "No tag was created. Run this script again with -Phase publish after reviewing main."
        return
    }
}

# Publish uses source versions already committed to the exact origin/main tip.
Assert-CleanMainAtOrigin -RepoRoot $repoRoot
$releaseCommit = (Invoke-GitCapture -RepoRoot $repoRoot -Arguments @("rev-parse", "HEAD") |
    Select-Object -First 1).Trim()
$plannedTags = [Collections.Generic.List[string]]::new()

if ($needsApp) {
    $currentApp = Get-AppSourceVersion -RepoRoot $repoRoot
    $expectedApp = Get-NextVersionFromTags -RepoRoot $repoRoot -Prefix "app-v" -Part $BumpPart
    if ($currentApp -ne $expectedApp.NextVersion) {
        throw "App source version $currentApp is not the expected next $BumpPart version $($expectedApp.NextVersion)."
    }
    $plannedTags.Add("app-v$currentApp")
    Wait-GitHubWorkflowForCommit -RepoRoot $repoRoot -Workflow "configurator.yml" `
        -Commit $releaseCommit -Event "push"
}
if ($needsFirmware) {
    $currentFirmware = Get-FirmwareSourceVersion -RepoRoot $repoRoot
    $expectedFirmware = Get-NextVersionFromTags -RepoRoot $repoRoot -Prefix "firmware-v" -Part $BumpPart
    if ($currentFirmware -ne $expectedFirmware.NextVersion) {
        throw "Firmware source version $currentFirmware is not the expected next $BumpPart version $($expectedFirmware.NextVersion)."
    }
    $plannedTags.Add("firmware-v$currentFirmware")
    Wait-GitHubWorkflowForCommit -RepoRoot $repoRoot -Workflow "firmware.yml" `
        -Commit $releaseCommit -Event "push"
}
foreach ($tag in $plannedTags) {
    Assert-TagAndReleaseDoNotExist -RepoRoot $repoRoot -Tag $tag
}

Write-Step "Protected release tag plan"
Write-Host "Commit: $releaseCommit"
Write-Host "Tags: $($plannedTags -join ', ')"
Write-Host "The signing preflight requires independent GitHub environment approval and exposes no key to local tooling."
if ($needsFirmware) {
    Write-Host "Firmware tag CI will leave an authenticated draft. Publishing it requires the documented destructive v2-to-v3 HIL gate."
}
if (-not (Confirm-Action -Prompt "Dispatch signing preflight and wait for all protected jobs?")) {
    throw "Aborted before signing preflight."
}
Invoke-SigningPreflight -RepoRoot $repoRoot -TargetChoice $Target -Commit $releaseCommit

if ([string]::IsNullOrWhiteSpace($ReleaseNotes) -and -not $Yes) {
    $ReleaseNotes = Read-MultilineInput -Prompt "Annotated-tag notes (optional)"
}
if (-not (Confirm-Action -Prompt "Create and atomically push $($plannedTags -join ', ')?")) {
    throw "Aborted before tag creation."
}
foreach ($tag in $plannedTags) {
    $tagArgs = @("tag", "-a", $tag, "-m", "Release $tag")
    if (-not [string]::IsNullOrWhiteSpace($ReleaseNotes)) {
        $tagArgs += @("-m", $ReleaseNotes.Trim())
    }
    Invoke-External -Name "Create tag $tag" -WorkingDirectory $repoRoot `
        -Executable "git" -Arguments $tagArgs
}
Invoke-External -Name "Atomically push release tag set" -WorkingDirectory $repoRoot `
    -Executable "git" -Arguments (@("push", "--atomic", "origin") + @($plannedTags))

Write-Host ""
Write-Host "Release tags were pushed only after main CI and protected signing preflight passed." `
    -ForegroundColor Green
foreach ($tag in $plannedTags) {
    Write-Host "Created tag: $tag"
}
if ($needsFirmware) {
    Write-Host "The firmware release remains a draft until recovery-equipped migration HIL passes; CI does not publish it automatically." `
        -ForegroundColor Yellow
}
