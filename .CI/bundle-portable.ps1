if (-not (Test-Path -PathType Container Bajerino)) {
    Write-Error "Couldn't find a folder called 'Bajerino' in the current directory.";
    exit 1
}
if (-not $Env:C2_PORTABLE_INSTALLER_VERSION -or -not $Env:C2_PORTABLE_INSTALLER_SHA256_X64 -or -not $Env:C2_PORTABLE_INSTALLER_SHA256_ARM64) {
    Write-Error "C2_PORTABLE_INSTALLER_VERSION or C2_PORTABLE_INSTALLER_SHA256_{X64,ARM64} not defined.";
    exit 1
}

function Remove-IfExists {
    param (
        [string] $Path
    )
    if (Test-Path -PathType Container $Path) {
        Remove-Item $Path -Force -Recurse -Confirm:$false;
    }
    elseif (Test-Path -PathType Leaf $Path) {
        Remove-Item $Path -Force;
    }
}

# Check if we're on a tag
$OldErrorActionPref = $ErrorActionPreference;
$ErrorActionPreference = 'Continue';
git describe --exact-match --match 'v*' *> $null;
$isTagged = $?;
$ErrorActionPreference = $OldErrorActionPref;

Write-Output portable | Out-File Bajerino/modes -Encoding ASCII;
if ($isTagged) {
    # This is a release.
    # Make sure, any existing `modes` file is overwritten for the user,
    # for example when updating from nightly to stable.
    $bundleBaseName = "Bajerino.Portable";
}
else {
    Write-Output nightly | Out-File Bajerino/modes -Append -Encoding ASCII;
    $bundleBaseName = "Bajerino.Nightly.Portable";
}

$architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLower()
if ($architecture -eq 'arm64') {
    $bundleBaseName = "Experimental-ARM64-$bundleBaseName"
    $updaterArch = "aarch64"
    $expectedUpdaterHash = $Env:C2_PORTABLE_INSTALLER_SHA256_ARM64
}
else {
    $updaterArch = "x86_64"
    $expectedUpdaterHash = $Env:C2_PORTABLE_INSTALLER_SHA256_X64
}

if ($Env:GITHUB_OUTPUT) {
    # This is used in CI when creating the artifact
    "C2_PORTABLE_BASE_NAME=$bundleBaseName" >> "$Env:GITHUB_OUTPUT"
}

Remove-IfExists "Bajerino/updater.1";
New-Item "Bajerino/updater.1" -ItemType Directory;

Invoke-RestMethod "https://github.com/Nerixyz/c2-portable-updater/releases/download/$($Env:C2_PORTABLE_INSTALLER_VERSION)/c2-portable-updater-$updaterArch-pc-windows-msvc.zip" -OutFile _portable-installer.zip;
$updaterHash = (Get-FileHash _portable-installer.zip).Hash.ToLower();
if (-not $updaterHash -eq $expectedUpdaterHash) {
    Write-Error "Hash mismatch: expected $expectedUpdaterHash - got: $updaterHash";
    exit 1
}

7z e -y _portable-installer.zip c2-portable-updater.exe;
Move-Item c2-portable-updater.exe "Bajerino/updater.1/BajerinoUpdater.exe" -Force;
7z e -so _portable-installer.zip LICENSE-MIT > "Bajerino/updater.1/LICENSE";
Remove-IfExists _portable-installer.zip;

Remove-IfExists "$bundleBaseName.zip";
7z a "$bundleBaseName.zip" Bajerino/;
