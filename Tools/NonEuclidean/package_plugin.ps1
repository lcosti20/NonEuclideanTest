# Builds a shareable, prebuilt copy of the PortalWorlds plugin.
#
#   powershell -ExecutionPolicy Bypass -File Tools\NonEuclidean\package_plugin.ps1 [-Out <folder>]
#
# Runs RunUAT BuildPlugin (editor + game binaries for Win64), then:
#  - restores "EnabledByDefault": false, which BuildPlugin removes. Without it, a copy placed in a
#    project's Plugins/ folder counts as enabled by default, and packaging a Blueprint-only project
#    silently uses the stock game executable without the plugin ("module 'PortalWorlds' could not
#    be found" at runtime). With it, Unreal builds the plugin into the game.
#  - deletes the HostProject scratch folder BuildPlugin leaves behind.
# The result can go in a project's Plugins/ folder or in Engine/Plugins/Marketplace/.

param(
    [string]$Out = (Join-Path $PSScriptRoot "..\..\Saved\PluginPackage\PortalWorlds"),
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Plugin = Join-Path $Root "Plugins\PortalWorlds\PortalWorlds.uplugin"
$Out = [IO.Path]::GetFullPath($Out)

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out -Confirm:$false }
& (Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat") BuildPlugin "-Plugin=$Plugin" "-Package=$Out" -TargetPlatforms=Win64
if ($LASTEXITCODE -ne 0) { throw "BuildPlugin failed ($LASTEXITCODE)" }

$DescriptorPath = Join-Path $Out "PortalWorlds.uplugin"
$Descriptor = Get-Content $DescriptorPath -Raw | ConvertFrom-Json
$Descriptor | Add-Member -NotePropertyName EnabledByDefault -NotePropertyValue $false -Force
[IO.File]::WriteAllText($DescriptorPath, ($Descriptor | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding $false))

$HostProject = Join-Path $Out "HostProject"
if (Test-Path $HostProject) { Remove-Item -Recurse -Force $HostProject -Confirm:$false }

Write-Host "Packaged PortalWorlds to $Out"
Get-ChildItem $Out -Name
