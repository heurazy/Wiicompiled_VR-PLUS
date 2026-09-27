[CmdletBinding()]
param([string]$Checkout='integrations/wheelwizard-work', [string]$OutputDirectory='Launcher/artifacts/wheelwizard-publish')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Full([string]$path) { if([IO.Path]::IsPathRooted($path)){return [IO.Path]::GetFullPath($path)}; return [IO.Path]::GetFullPath((Join-Path $repo $path)) }
$source=Full $Checkout
$output=Full $OutputDirectory
$pin='86618e7367df935d78401583136e492c6f00fa27'
if (!(Test-Path -LiteralPath $source)) {
    & git clone https://github.com/TeamWheelWizard/WheelWizard.git $source
    if($LASTEXITCODE -ne 0){throw 'Wheel Wizard clone failed.'}
    & git -C $source checkout --detach $pin
    if($LASTEXITCODE -ne 0){throw 'Wheel Wizard checkout failed.'}
}
$head=& git -C $source rev-parse HEAD
if($head -ne $pin){throw 'Wheel Wizard checkout does not match the pinned integration.'}
if(!(Test-Path -LiteralPath (Join-Path $source 'WheelWizard/Services/Launcher/LocalVrLauncher.cs'))) {
    & git -C $source apply (Join-Path $repo 'integrations/wheelwizard-vr.patch')
    if($LASTEXITCODE -ne 0){throw 'Wheel Wizard VR integration patch failed.'}
}
& dotnet publish (Join-Path $source 'WheelWizard/WheelWizard.csproj') -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -o $output
if($LASTEXITCODE -ne 0){throw 'Wheel Wizard publish failed.'}
'Local WiiCompiled VR integration' | Set-Content (Join-Path $output 'vr-local.txt') -Encoding UTF8
Copy-Item (Join-Path $source 'LICENSE') (Join-Path $output 'WheelWizard-LICENSE.txt')
