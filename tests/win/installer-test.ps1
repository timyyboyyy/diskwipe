# Installiert diskwipe still, prüft Dateien/Registry/Version/Prüfsummen und deinstalliert wieder.
# Muss mit Adminrechten laufen (GitHub windows-latest Runner).
param([string]$Dist = "dist")
$ErrorActionPreference = "Stop"

function Fail($msg) { Write-Host "FAIL: $msg"; exit 1 }

$version = (Get-Content VERSION -Raw).Trim()
$setup = Join-Path $Dist "diskwipe-$version-setup.exe"
$portable = Join-Path $Dist "diskwipe-$version-portable.exe"
$dir = Join-Path $env:ProgramFiles "diskwipe"
$key = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\diskwipe"
$startMenu = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs\diskwipe\diskwipe.lnk"

if (-not (Test-Path $setup)) { Fail "Installer fehlt: $setup" }

foreach ($line in Get-Content (Join-Path $Dist "SHA256SUMS.txt")) {
    $hash, $name = $line -split '\s+', 2
    $name = $name.TrimStart('*')
    $actual = (Get-FileHash (Join-Path $Dist $name) -Algorithm SHA256).Hash.ToLower()
    if ($actual -ne $hash) { Fail "Prüfsumme falsch: $name" }
}

Start-Process $setup -ArgumentList "/S" -Wait
if (-not (Test-Path "$dir\diskwipe.exe")) { Fail "diskwipe.exe nicht installiert" }
if (-not (Test-Path "$dir\uninstall.exe")) { Fail "uninstall.exe fehlt" }
if (-not (Test-Path $startMenu)) { Fail "Startmenü-Eintrag fehlt" }
$reg = Get-ItemProperty $key
if ($reg.DisplayName -ne "diskwipe") { Fail "DisplayName falsch" }
if ($reg.DisplayVersion -ne $version) { Fail "DisplayVersion $($reg.DisplayVersion) != $version" }
$exeVersion = (Get-Item "$dir\diskwipe.exe").VersionInfo.ProductVersion
if ($exeVersion -ne $version) { Fail "Exe-ProductVersion $exeVersion != $version" }
if ((Get-FileHash "$dir\diskwipe.exe").Hash -ne (Get-FileHash $portable).Hash) { Fail "Installierte Exe != portable Exe" }

Start-Process "$dir\uninstall.exe" -ArgumentList "/S", "_?=$dir" -Wait
Remove-Item "$dir\uninstall.exe" -ErrorAction SilentlyContinue
Remove-Item $dir -ErrorAction SilentlyContinue
if (Test-Path "$dir\diskwipe.exe") { Fail "diskwipe.exe nach Deinstallation vorhanden" }
if (Test-Path $key) { Fail "Registry-Eintrag nach Deinstallation vorhanden" }
if (Test-Path $startMenu) { Fail "Startmenü-Eintrag nach Deinstallation vorhanden" }

Write-Host "PASS installer"
