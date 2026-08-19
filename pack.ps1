# Builds the release archive, so that what is published is put together the
# same way every time and anyone can reproduce it from a clone.
#
#   powershell -ExecutionPolicy Bypass -File pack.ps1 -Version 1.5.0

param([string]$Version = '1.7.0.0')

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

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host ""
Write-Host ("{0}, {1:N0} bytes" -f (Split-Path -Leaf $zip), (Get-Item $zip).Length)
Write-Host ("SHA-256  {0}" -f (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())
Write-Host ""
Get-ChildItem $stage | ForEach-Object {
    Write-Host ("  {0,-16} {1,10:N0}  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())
}

# The updater downloads these two on their own, not the archive, so a release
# without them can only be pointed at and not installed. GitHub publishes a
# SHA-256 for every asset it is given, and that is what the updater checks the
# download against before it replaces anything.
Write-Host ""
Write-Host "Upload the archive AND the two files beside it, or the updater has nothing to fetch:"
Write-Host ""
Write-Host ("  gh release create v{0} ``" -f $Version)
# No #label after the file name. gh turns that into a display label and GitHub
# then shows the label instead of the file name, so 1.7.0.0 first went out
# reading "hlswfix 1.7.0.0" with no extension, next to a README that tells
# people to download the zip. The file name already says everything.
Write-Host ("      `"{0}`" ``" -f $zip)
Write-Host ("      `"{0}`" ``" -f (Join-Path $build 'hlswfix.dll'))
Write-Host ("      `"{0}`" ``" -f (Join-Path $build 'hlswfix.exe'))
Write-Host ("      --title `"hlswfix {0}`" --notes-file <notes>" -f $Version)
