# Builds the release archive, so that what is published is put together the
# same way every time and anyone can reproduce it from a clone.
#
#   powershell -ExecutionPolicy Bypass -File pack.ps1 -Version 1.5.0

param([string]$Version = '1.8.1.0')

$ErrorActionPreference = 'Stop'

$build = Join-Path $PSScriptRoot 'build'
if (-not (Test-Path (Join-Path $build 'hlswfix.exe'))) {
    throw "Nothing in build\. Run build.cmd first."
}

$stage = Join-Path $build "hlswfix-$Version"
$zip = Join-Path $build "hlswfix-$Version.zip"

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
$null = New-Item -ItemType Directory -Path $stage

# What a user needs and nothing else. The sources are in the repository, not in
# the archive, so the download stays small enough to look at.
Copy-Item (Join-Path $build 'hlswfix.exe') $stage
Copy-Item (Join-Path $build 'hlswfix.dll') $stage
foreach ($f in 'hlswfix.ini', 'install.cmd', 'uninstall.cmd', 'install.ps1',
               'README.md', 'README.de.md', 'LICENSE') {
    Copy-Item (Join-Path $PSScriptRoot $f) $stage
}

# Built entry by entry rather than with Compress-Archive, for one reason: the
# two files the updater installs are stored uncompressed, everything else is
# compressed as usual.
#
# That is what lets the updater work from this archive alone instead of needing
# the binaries uploaded a second time beside it. Reading a stored entry is
# finding it and copying bytes, so the launcher needs no decompressor, nothing
# shipped alongside it and nothing from the operating system. See
# zip_stored_entry in src/launcher.c.
#
# It costs about a hundred kilobytes in the download and nothing else. The
# result is an ordinary zip: Explorer, 7-Zip and Expand-Archive all open it and
# neither know nor care which entries were compressed.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stored = @('hlswfix.dll', 'hlswfix.exe')
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($f in (Get-ChildItem $stage | Sort-Object Name)) {
        if ($stored -contains $f.Name) {
            $level = [System.IO.Compression.CompressionLevel]::NoCompression
        } else {
            $level = [System.IO.Compression.CompressionLevel]::Optimal
        }
        $null = [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $f.FullName, $f.Name, $level)
    }
} finally {
    $archive.Dispose()
}

# Checked rather than assumed. If a later change to this script quietly
# compresses those two again, the updater in every copy already out there stops
# being able to install anything, and nothing would show it until somebody
# pressed the button. Better to fail here.
$check = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach ($name in $stored) {
        $entry = $check.GetEntry($name)
        if ($null -eq $entry) { throw "$name is missing from the archive." }
        # Not equality: .NET answers NoCompression with a deflate stream made of
        # stored blocks, which comes out a handful of bytes longer than the file
        # rather than shorter. The launcher reads both that and a plainly stored
        # entry. What must never happen is that it actually got smaller, because
        # that means it really was compressed.
        if ($entry.CompressedLength -lt $entry.Length) {
            throw "$name is compressed in the archive. The updater cannot read it."
        }
    }
} finally {
    $check.Dispose()
}
Write-Host "hlswfix.dll and hlswfix.exe are stored uncompressed, as the updater needs."

Write-Host ""
Write-Host ("{0}, {1:N0} bytes" -f (Split-Path -Leaf $zip), (Get-Item $zip).Length)
Write-Host ("SHA-256  {0}" -f (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())
Write-Host ""
Get-ChildItem $stage | ForEach-Object {
    Write-Host ("  {0,-16} {1,10:N0}  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())
}

# One archive and the mirrored HLSW installer, nothing else. The updater reads
# the two files it needs out of the archive, so uploading them separately would
# only put things on the release page that no person should click.
#
# No #label after a file name either. gh turns that into a display label and
# GitHub then shows the label instead of the file name, so 1.7.0.0 first went
# out reading "hlswfix 1.7.0.0" with no extension, next to a README that tells
# people to download the zip.
Write-Host ""
Write-Host "To publish:"
Write-Host ""
Write-Host ("  gh release create v{0} ``" -f $Version)
Write-Host ("      `"{0}`" ``" -f $zip)
Write-Host ("      `"<path to>\hlsw_1_4_0_5_setup.exe`" ``")
Write-Host ("      --title `"hlswfix {0}`" --notes-file <notes>" -f $Version)
