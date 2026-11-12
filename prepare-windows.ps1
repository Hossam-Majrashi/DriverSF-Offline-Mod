$ErrorActionPreference = 'Stop'
$mod = $PSScriptRoot
$game = Split-Path -Parent $mod
$backupMode = if ($env:DSF_BACKUPS) { $env:DSF_BACKUPS } else { '0' }
if ($backupMode -notin @('0', '1')) { throw 'DSF_BACKUPS must be 0 or 1.' }
New-Item -ItemType Directory -Force -Path (Join-Path $mod 'logs') | Out-Null
if (Get-Process -Name Driver -ErrorAction SilentlyContinue) { throw 'Close Driver.exe first.' }
$expected = 'c79987b0548f7130337c3f295c8fe269e120ceddad55c1159e6f5cbc6aa26616'
if ((Get-FileHash -LiteralPath (Join-Path $game 'Driver.exe') -Algorithm SHA256).Hash.ToLower() -ne $expected) { throw 'Unsupported Driver.exe hash.' }
$source = Join-Path $mod 'build\dinput8.dll'
$target = Join-Path $game 'dinput8.dll'
if (!(Test-Path -LiteralPath $source)) { throw 'Missing mod/build/dinput8.dll' }
if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) { throw 'dinput8.dll conflict: not overwriting.' }
} else { Copy-Item -LiteralPath $source -Destination $target }
$docs = [Environment]::GetFolderPath('MyDocuments')
$save = Join-Path $docs 'Ubisoft\Driver San Francisco'
if (!(Test-Path -LiteralPath $save)) { New-Item -ItemType Directory -Force -Path $save | Out-Null }
$offlineIni = Join-Path $save 'offline.ini'
if (!(Test-Path -LiteralPath $offlineIni)) {
    [IO.File]::WriteAllText($offlineIni, "OnlineUserName Player`r`nOnlineID 1`r`n")
}
if ($backupMode -eq '1') {
$backup = Join-Path $mod ('backups\windows-' + (Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
New-Item -ItemType Directory -Path $backup | Out-Null
if (Test-Path -LiteralPath $save) { Copy-Item -LiteralPath $save -Destination (Join-Path $backup 'Driver San Francisco') -Recurse }
# Preserve Ubisoft's launcher saves as well where present, without changing them.
foreach ($pf in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
    if (!$pf) { continue }
    $ubi = Join-Path $pf 'Ubisoft\Ubisoft Game Launcher\savegames'
    if (Test-Path -LiteralPath $ubi) { Copy-Item -LiteralPath $ubi -Destination (Join-Path $backup ('launcher-' + [IO.Path]::GetFileName($pf))) -Recurse }
}
Write-Host "Save backup: $backup"
}
Get-FileHash -LiteralPath $target -Algorithm SHA256 | Format-List | Out-File (Join-Path $mod 'installed-windows.txt')
