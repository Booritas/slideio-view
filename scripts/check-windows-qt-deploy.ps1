# Asserts that a Windows install tree carries the Qt runtime the application
# needs to reach its event loop. This is the regression test for the OPTIONAL
# installs that let an empty installer build cleanly.
param([Parameter(Mandatory=$true)][string]$Prefix,
      [ValidateSet('Release','Debug')][string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
$suffix = if ($Config -eq 'Debug') { 'd' } else { '' }
$required = @(
    "bin\Qt6Core$suffix.dll",
    "bin\Qt6Gui$suffix.dll",
    "bin\Qt6Widgets$suffix.dll",
    "bin\Qt6OpenGLWidgets$suffix.dll",
    "bin\qt.conf",
    "plugins\platforms\qwindows$suffix.dll"
)
$missing = $required | Where-Object { -not (Test-Path (Join-Path $Prefix $_)) }
if ($missing) {
    Write-Host "FAIL: missing from $Prefix :"
    $missing | ForEach-Object { Write-Host "  $_" }
    exit 1
}
# A Release tree carrying debug Qt (or the reverse) loads nothing at runtime.
$wrong = if ($Config -eq 'Release') { 'bin\Qt6Cored.dll' } else { 'bin\Qt6Core.dll' }
if (Test-Path (Join-Path $Prefix $wrong)) {
    Write-Host "FAIL: $Config tree also contains $wrong"
    exit 1
}
Write-Host "OK: Qt runtime present in $Prefix ($Config)"
