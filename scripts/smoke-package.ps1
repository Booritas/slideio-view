# Unpacks the Windows archive and checks it the way a stranger would meet it:
# no build tree, no conan, no toolchain file.
param([string]$PackageDir = "build/packages")

$ErrorActionPreference = 'Stop'

$zip = Get-ChildItem $PackageDir -Filter 'slideio-viewer-*-windows-x86_64.zip' | Select-Object -First 1
if (-not $zip) { throw "no windows zip in $PackageDir" }

$unpacked = Join-Path $PackageDir 'unpacked'
if (Test-Path $unpacked) { Remove-Item -Recurse -Force $unpacked }
Expand-Archive -Path $zip.FullName -DestinationPath $unpacked -Force

# CPack nests the payload one directory deep under the package name.
$root = Get-ChildItem $unpacked -Directory | Select-Object -First 1
if (-not $root) { $root = Get-Item $unpacked }
Write-Host "unpacked to $($root.FullName)"

$required = @(
    'bin\slideio-viewer.exe', 'bin\Qt6Core.dll', 'bin\Qt6Widgets.dll',
    'bin\Qt6OpenGLWidgets.dll', 'bin\slideio.dll', 'plugins\platforms\qwindows.dll'
)
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $root.FullName $rel))) { throw "missing from package: $rel" }
}
Write-Host "OK: all required files present"

# A GUI process that stays up has resolved every library it needs to reach the
# event loop. One that exits immediately has not -- which is the failure a
# missing DLL or platform plugin actually produces.
$exe = Join-Path $root.FullName 'bin\slideio-viewer.exe'
$p = Start-Process -FilePath $exe -ArgumentList '-platform','offscreen' -PassThru
Start-Sleep -Seconds 10
if ($p.HasExited) { throw "slideio-viewer exited immediately with code $($p.ExitCode)" }
Stop-Process -Id $p.Id -Force
Write-Host "OK: slideio-viewer stayed up for 10s under -platform offscreen"
