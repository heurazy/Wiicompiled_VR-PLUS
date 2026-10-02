[CmdletBinding()]
param([string]$SetupPath='Launcher/dist/WiiCompiled-Setup.exe',
      [string]$WheelWizardPublish='Launcher/artifacts/wheelwizard-publish',
      [string]$OutputDirectory='Launcher/dist', [string]$Name='WiiCompiled-VR-Portable-Migration')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Full([string]$path) { if([IO.Path]::IsPathRooted($path)){return [IO.Path]::GetFullPath($path)}; return [IO.Path]::GetFullPath((Join-Path $repo $path)) }
if($Name -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid portable package name.'}
$setup=Full $SetupPath; $launcher=Full $WheelWizardPublish; $output=Full $OutputDirectory
if(!(Test-Path -LiteralPath $setup -PathType Leaf)){throw 'Build the full installer first.'}
if(!(Test-Path -LiteralPath (Join-Path $launcher 'WheelWizard.exe'))){throw 'Build Wheel Wizard first.'}
$stage=Full ('Launcher/artifacts/portable-bundle/'+$Name)
$allowed=Full 'Launcher/artifacts/portable-bundle'
if(!$stage.StartsWith($allowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe portable staging directory.'}
if(Test-Path -LiteralPath $stage){Remove-Item -LiteralPath $stage -Recurse -Force}
[IO.Directory]::CreateDirectory($stage)|Out-Null
[IO.Directory]::CreateDirectory($output)|Out-Null
Copy-Item -LiteralPath $setup -Destination (Join-Path $stage 'WiiCompiled-Setup.exe')
Copy-Item -LiteralPath $launcher -Destination (Join-Path $stage 'WheelWizard') -Recurse
'Portable installation marker'|Set-Content (Join-Path $stage 'portable.txt') -Encoding UTF8
'Local WiiCompiled VR integration'|Set-Content (Join-Path $stage 'WheelWizard/vr-local.txt') -Encoding UTF8
@'
@echo off
start "WiiCompiled VR Launcher" "%~dp0WheelWizard\WheelWizard.exe"
'@|Set-Content (Join-Path $stage 'Launch-WiiCompiled-VR.cmd') -Encoding ASCII
@'
WiiCompiled VR - complete portable package

1. Extract the entire ZIP to a writable folder.
2. Run WiiCompiled-Setup.exe. Choose your own clean PAL RMCP01 Mario Kart Wii
   ISO/WBFS/RVZ. The English installer compiles the games locally. Retro Rewind
   downloading is enabled by default. No ROM or compiled Nintendo game is included.
3. Run Launch-WiiCompiled-VR.cmd or WheelWizard/WheelWizard.exe. Choose Mario
   Kart Wii VR or Retro Rewind VR. Use the launcher for updates, licenses and
   multiplayer features. Retro WFC availability depends on its online service.
4. Connect the headset using your active OpenXR runtime (SteamVR, VDXR, etc.).
   F10 -> Use SteamVR (next launch) can explicitly select SteamVR instead.

The Install and UserData folders remain beside the launcher. Move the whole
package together. Your save and ROM remain on your PC.

Touch/PICO: RT accelerate, LT brake/reverse, Y item, X trick; hold X 0.65 s to
pause on SteamVR. X+Y opens VR options. Right stick click cycles cameras.
First person: A drift; hold either grip to grab the wheel. Other cameras:
right grip drifts, A also accelerates. F10 includes calibration, button swaps,
adaptive resolution and optional Wii Remote scanning (disabled by default).
'@|Set-Content (Join-Path $stage 'README.txt') -Encoding UTF8
& (Join-Path $PSScriptRoot 'Test-PayloadBoundary.ps1') -PayloadRoot $stage
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $output ($Name+'.zip')
if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Host "Portable package ready: $zip"
