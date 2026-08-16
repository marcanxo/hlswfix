# Sets up the takeover arrangement, and undoes it again.
#
# Only needed if you want HLSW to be fixed no matter how it is started. Simply
# running hlswfix.exe out of the HLSW folder needs none of this and changes
# nothing, and is the better choice if you are unsure.
#
# What it sets up: hlsw.exe becomes hlsw-real.exe, and the launcher takes the
# name hlsw.exe. Every shortcut, start menu entry and file association that
# already exists then starts HLSW with the fix in it, without knowing anything
# about the fix.
#
# Run it again after an HLSW update. An update writes its own hlsw.exe over the
# launcher and undoes the arrangement, and HLSW then shows every server as
# timed out again. This script looks at what hlsw.exe actually is rather than
# assuming, so a first install, an update and a repeat run all end in the same
# correct state.
#
#   powershell -ExecutionPolicy Bypass -File install.ps1
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
#
# -Dir points at the HLSW folder when this script is not already sitting in it,
# which is the case when running it straight out of a build tree.

[CmdletBinding()]
param(
    [string]$Dir,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

# The files to install, either next to this script (release archive) or in
# build\ (built from source).
$payloadDir = $PSScriptRoot
if (-not (Test-Path (Join-Path $payloadDir 'hlswfix.dll'))) {
    $payloadDir = Join-Path $PSScriptRoot 'build'
}
$payloadExe = Join-Path $payloadDir 'hlswfix.exe'
$payloadDll = Join-Path $payloadDir 'hlswfix.dll'
$payloadIni = Join-Path $PSScriptRoot 'hlswfix.ini'

if (-not $Dir) {
    if ((Test-Path (Join-Path $PSScriptRoot 'hlsw.exe')) -or
        (Test-Path (Join-Path $PSScriptRoot 'hlsw-real.exe'))) {
        $Dir = $PSScriptRoot
    } else {
        throw "No HLSW next to this script. Say where it is: install.ps1 -Dir 'C:\Program Files (x86)\HLSW'"
    }
}
$Dir = (Resolve-Path $Dir).Path

$exe = Join-Path $Dir 'hlsw.exe'
$real = Join-Path $Dir 'hlsw-real.exe'
$dll = Join-Path $Dir 'hlswfix.dll'
$ini = Join-Path $Dir 'hlswfix.ini'
$log = Join-Path $Dir 'hlswfix.log'

if (Get-Process hlsw, hlsw-real, hlswfix -ErrorAction SilentlyContinue) {
    throw "HLSW is running. Close it first."
}

function Test-RealHlsw([string]$path) {
    if (-not (Test-Path $path)) { return $false }
    $info = (Get-Item $path).VersionInfo
    return ($info.FileDescription -match 'HLSW') -or ($info.ProductName -match 'HLSW')
}

# The launcher is recognised by content wherever possible. Falling back on
# "carries no HLSW version resource and is small" covers a launcher built from
# an older revision of the sources, which will not match by hash.
function Test-OurLauncher([string]$path) {
    if (-not (Test-Path $path)) { return $false }
    if (Test-Path $payloadExe) {
        $a = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $b = (Get-FileHash -LiteralPath $payloadExe -Algorithm SHA256).Hash
        if ($a -eq $b) { return $true }
    }
    return (-not (Test-RealHlsw $path)) -and ((Get-Item $path).Length -lt 1MB)
}

if ($Uninstall) {
    if (Test-Path $real) {
        if (Test-Path $exe) {
            if (Test-OurLauncher $exe) {
                Remove-Item $exe -Force
            } elseif (Test-RealHlsw $exe) {
                throw "hlsw.exe is the real HLSW and hlsw-real.exe exists as well. Decide by hand which one to keep."
            } else {
                throw "hlsw.exe is neither the launcher nor HLSW. Not touching it."
            }
        }
        Move-Item $real $exe
        Write-Host "hlsw-real.exe moved back to hlsw.exe"
    } else {
        Write-Host "Nothing was taken over here, only removing files"
    }

    foreach ($f in @($dll, $log, (Join-Path $Dir 'hlswfix.exe'))) {
        if (Test-Path $f) {
            Remove-Item $f -Force
            Write-Host ("removed {0}" -f (Split-Path -Leaf $f))
        }
    }
    if (Test-Path $ini) {
        Write-Host "hlswfix.ini was left in place, in case you edited it. Delete it if you want it gone."
    }
    Write-Host ""
    Write-Host "Done. HLSW is exactly as it was."
    return
}

if (-not (Test-Path $payloadExe)) { throw "hlswfix.exe was not found. Run build.cmd first, or use the release archive." }
if (-not (Test-Path $payloadDll)) { throw "hlswfix.dll was not found. Run build.cmd first, or use the release archive." }

Copy-Item $payloadDll $dll -Force

# Never overwritten: it is the one file the user is meant to edit.
if ((Test-Path $payloadIni) -and -not (Test-Path $ini)) {
    Copy-Item $payloadIni $ini
}

if (Test-RealHlsw $exe) {
    # Either a first install, or an update has just written the real program
    # over the launcher. Either way it belongs under the other name.
    $v = (Get-Item $exe).VersionInfo.FileVersion
    Write-Host "hlsw.exe is the real HLSW ($v), moving it to hlsw-real.exe"
    Move-Item $exe $real -Force
} elseif (Test-Path $exe) {
    Write-Host "hlsw.exe is already the launcher, replacing it"
}

if (-not (Test-Path $real)) {
    throw "hlsw-real.exe is missing and hlsw.exe was not the real program either. There is nothing here to launch."
}

Copy-Item $payloadExe $exe -Force

Write-Host ""
Write-Host "Done."
Write-Host ("  hlsw.exe       launcher, {0:N0} bytes" -f (Get-Item $exe).Length)
Write-Host ("  hlsw-real.exe  HLSW {0}, {1:N0} bytes" -f (Get-Item $real).VersionInfo.FileVersion, (Get-Item $real).Length)
Write-Host ("  hlswfix.dll    {0:N0} bytes" -f (Get-Item $dll).Length)
Write-Host ""
Write-Host "Start HLSW as usual. Undo all of it with: install.ps1 -Uninstall"
