#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
<#
.SYNOPSIS
  Records the Direct3D 11 programs the game compiles into Data\dx11shaders.shipped.

.DESCRIPTION
  The Direct3D 11 backend compiles each generated program with d3dcompiler_47 the first time a pipeline
  needs it, and keeps the bytecode in the player's dx11shaders.cache. Under Wine, d3dcompiler_47 is
  vkd3d-shader, and one of those compiles took minutes (X2, on a Steam Deck): the game looked hung on its
  first frame. The backend reads dx11shaders.shipped, next to the exe, before it compiles anything, so the
  programs recorded here are never compiled on a player's machine.

  This plays AI-against-AI skirmishes with the picture drawn by Direct3D 11, on a Windows machine whose
  d3dcompiler_47 is Microsoft's. Each run names its setup in a user data folder of this script's own:
  reforged-look.ini as Options.ini for the Reforged picture (High), none for a run marked Classic, which is
  what a first start draws, and reforged-look.ini with the slider's Ultra rows for a run marked Ultra, the
  nine looks of 2026-10-31 on. It does so at the default post chain and with it off, on a few seeds,
  plus the two scenarios that stage the per-object looks, plus the shell map. It starts each run from an empty cache: the player's cache is moved aside and put
  back afterwards, and the farm's shipped file is unlinked. What the runs compiled is then the shipped
  file. The bytecode is exactly what run-time D3DCompile gives on this machine, because it is that output.
  A program the recording never met is compiled on the player's machine, as before.

  Every run is unattended (ZH_UNATTENDED), in a farm made by windows-ci.ps1's own New-Farm, and killed at
  -TimeoutMinutes.

  -ExtraArgs goes to every run, for -noDynamicLOD: in a
  build that has that switch (when this was written, only Debug and Internal builds did), it keeps the effects a slow
  machine would drop, and so their programs, in the recording.

.EXAMPLE
  .\record-dx11-shaders.ps1 -DataDir D:\ZeroHourData
  .\record-dx11-shaders.ps1 -DataDir D:\ZeroHourData -Maps "Bitter Winter","Lights Out"
  .\record-dx11-shaders.ps1 -DataDir D:\ZeroHourData -ExtraArgs -noDynamicLOD
#>
param(
	# a folder holding zerohour\ (a Zero Hour install, with ZH_Generals\ in it or the base game findable)
	[Parameter(Mandatory = $true)] [string] $DataDir,
	# the built game: generals.exe, its DLLs and the fork's data
	[string] $RunDir = (Join-Path $PSScriptRoot "..\..\Run"),
	# where the recording goes (default: Data\dx11shaders.shipped)
	[string] $Out = "",
	[string] $WorkDir = (Join-Path $env:TEMP "zh-record-dx11-shaders"),
	# the game's user data folder, where the backend keeps the cache this reads back
	[string] $UserData = (Join-Path $env:TEMP "zh-record-dx11-shaders-userdata"),
	[int[]] $Seeds = @(0, 1, 2),
	# named skirmish maps played the same way, for the lighting the random maps do not have (night, snow)
	[string[]] $Maps = @(),
	# logic frames, 60 a second
	[int] $Frames = 4800,
	[int] $TimeoutMinutes = 30,
	# leave the shipped file in the farm: a check that a run with it compiles nothing it holds
	[switch] $KeepShipped,
	# only the runs whose names match one of these wildcards (seed0-classic, seed0, seed0-ultra): the short check
	[string[]] $Only = @(),
	# given to every run, after its own switches (for example -noDynamicLOD)
	[string[]] $ExtraArgs = @()
)

$ErrorActionPreference = "Stop"
$env:ZH_UNATTENDED = "1"	# every game this starts is unattended: no box may wait on a person (EarlyCommandLine.h)

$RunDir = (Resolve-Path $RunDir).Path
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
# windows-ci.ps1's own farm functions, taken from its syntax tree: a farm is never hand-rolled.
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repoRoot "windows-ci.ps1"), [ref]$null, [ref]$null)
$ast.FindAll({ $args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
	$args[0].Name -in 'New-FarmLink', 'New-Farm' }, $true) | ForEach-Object { Invoke-Expression $_.Extent.Text }

$zh = Join-Path $DataDir "zerohour"
if ($Out -eq "") { $Out = Join-Path $PSScriptRoot "..\Data\dx11shaders.shipped" }
$cache = Join-Path $UserData "dx11shaders.cache"
$aside = "$cache.recording-aside"
New-Item -ItemType Directory -Force $WorkDir | Out-Null

# The same things windows-ci and replay-check pass for an E1 match, drawn this time.
$runs = @()
foreach ($seed in $Seeds) {
	$match = @("-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-randommap", $seed, 2,
		"-autoskirmish", 2, "-aidiff", "hard", "-seed", $seed, "-observer")
	$runs += , @{ Name = "seed$seed"; Args = $match; Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "seed$seed-nopost"; Args = $match + @("-dx11post", "off"); Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "seed$seed-ultra"; Args = $match; Minutes = $TimeoutMinutes; Ultra = $true }
	$runs += , @{ Name = "seed$seed-ultra-nopost"; Args = $match + @("-dx11post", "off"); Minutes = $TimeoutMinutes; Ultra = $true }
}
foreach ($map in $Maps) {
	# quoted by hand: Start-Process joins its arguments with spaces and the names have them
	$named = @("-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-map", "`"Maps\$map\$map.map`"",
		"-autoskirmish", 2, "-aidiff", "hard", "-seed", 0, "-observer")
	$tag = $map -replace '[^A-Za-z0-9]', ''
	$runs += , @{ Name = "map-$tag"; Args = $named; Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "map-$tag-nopost"; Args = $named + @("-dx11post", "off"); Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "map-$tag-ultra"; Args = $named; Minutes = $TimeoutMinutes; Ultra = $true }
}
# The per-object looks (power outage, EMP, cloak and its shadow, revealed), each a program of its own on every
# mesh it dresses, which an observer's match never draws: an observer sees every stealthed unit as a friend's
# and none revealed. Scenarios\drawlooks.txt and looks4.txt stage them for a seat 0 that plays, and the
# cinema of the same name keeps the camera on them and ends the match.
foreach ($look in @(
		@{ Name = "drawlooks"; Sides = @("-side", 0, "FactionChina", "-side", 1, "FactionAmerica") },
		@{ Name = "looks4"; Sides = @("-side", 0, "FactionAmericaSuperWeaponGeneral", "-side", 1, "FactionGLA") })) {
	$staged = @("-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-map", "`"Maps\Twilight Flame\Twilight Flame.map`"",
		"-autoskirmish", 2, "-takeover") + $look.Sides + @("-seed", 7, "-scenario", $look.Name, "-cinema", $look.Name)
	$runs += , @{ Name = $look.Name; Args = $staged; Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "$($look.Name)-nopost"; Args = $staged + @("-dx11post", "off"); Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "$($look.Name)-ultra"; Args = $staged; Minutes = $TimeoutMinutes; Ultra = $true }
}
# The main menu's shell map, the first thing a player sees: the intro movies, then the menu, which runs until
# it is killed.  The cache is written while the game runs, so the kill loses nothing.
$runs += , @{ Name = "shellmap"; Args = @("-multiInstance", "-noFPSLimit"); Minutes = 5 }
$runs += , @{ Name = "shellmap-ultra"; Args = @("-multiInstance", "-noFPSLimit"); Minutes = 5; Ultra = $true }
# What a first start draws: the Classic game, no Options.ini, on the menu and in a match.
$runs += , @{ Name = "shellmap-classic"; Args = @("-multiInstance", "-noFPSLimit"); Minutes = 5; Classic = $true }
$runs += , @{ Name = "seed0-classic"; Args = @("-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-randommap", 0, 2,
	"-autoskirmish", 2, "-aidiff", "hard", "-seed", 0, "-observer"); Minutes = $TimeoutMinutes; Classic = $true }

# Ultra, the slider's step 10 (TheGraphicsQualityTable): reforged-look.ini (High) with the rows that rise above
# High. The nine looks of 2026-10-31 are the ones that change a generated program; the rest (the Ultra
# detail level with its particle ring, Warm Light, 8x MSAA, the shadow map size, Smoke) rides along so the
# rung is whole.
$ultraRows = [ordered]@{ StaticGameLOD = "Ultra"; GraphicsQuality = 10; MSAA = 3; ShadowQuality = 2; Smoke = 1; MapLight = "yes";
	Firelight = "yes"; HemiAmbient = "yes"; AmbientOcclusion = "yes"; InternalHdr = "yes"; Weather = "yes"; WaterSplashes = "yes";
	Atmosphere = "yes"; StructureCollapse = "yes"; GroundMarks = "yes" }
$ultraIni = @(Get-Content (Join-Path $repoRoot "reforged-look.ini") | Where-Object { $_ -notmatch "^\s*($($ultraRows.Keys -join '|'))\s*=" }) +
	@($ultraRows.GetEnumerator() | ForEach-Object { "$($_.Key) = $($_.Value)" })

if ($Only.Count -gt 0) { $runs = @($runs | Where-Object { $name = $_.Name; @($Only | Where-Object { $name -like $_ }).Count -gt 0 }) }

New-Item -ItemType Directory -Force $UserData | Out-Null
$env:ZH_USER_DATA_DIR = (Resolve-Path $UserData).Path

if (Test-Path $cache) { Move-Item -Force $cache $aside }
try {
	foreach ($run in $runs) {
		$farm = Join-Path $WorkDir "farm-$($run.Name)"	# one a run, so every run's log is kept
		New-Farm $zh $farm
		$options = Join-Path $UserData "Options.ini"
		if ($run.Classic) { Remove-Item -LiteralPath $options -Force -ErrorAction SilentlyContinue }
		elseif ($run.Ultra) { Set-Content -LiteralPath $options -Value $ultraIni -Encoding ASCII }
		else { Copy-Item (Join-Path $repoRoot "reforged-look.ini") $options -Force }
		$shipped = Join-Path $farm "dx11shaders.shipped"
		if (-not $KeepShipped -and (Test-Path -LiteralPath $shipped)) { Remove-Item -LiteralPath $shipped -Force }	# the link, never its target
		# -win as well: Windows has no hidden window, and a full screen game minimizes when someone clicks
		# away from it and then waits, never reaching -maxframes
		$arguments = $run.Args + $ExtraArgs + @("-maxframes", $Frames, "-win", "-hiddenwindow", "-noaudio", "-logPrefix", "rec_$($run.Name)")
		Write-Host ("{0}: " -f $run.Name) -NoNewline
		$sw = [Diagnostics.Stopwatch]::StartNew()
		$proc = Start-Process -FilePath (Join-Path $farm "generals.exe") -ArgumentList $arguments -WorkingDirectory $farm -PassThru
		$null = $proc.Handle
		if (-not $proc.WaitForExit($run.Minutes * 60 * 1000)) {
			$proc.Kill(); $proc.WaitForExit()
			Write-Host ("killed after {0} min; the cache keeps what it compiled" -f $run.Minutes)
		} else {
			Write-Host ("exit {0} after {1} s" -f $proc.ExitCode, [int]$sw.Elapsed.TotalSeconds)
		}
	}
	if (-not (Test-Path $cache)) {
		if ($KeepShipped) { Write-Host "the runs compiled nothing: every program they drew with was shipped"; return }
		throw "no program was compiled: $cache was never written"
	}
	Copy-Item -Force $cache $Out
	# a count, read the way the backend reads it: an 8-byte marker, then hash, size and bytes per program
	$bytes = [System.IO.File]::ReadAllBytes($Out)
	$at = 8; $count = 0
	while ($at + 12 -le $bytes.Length) {
		$size = [BitConverter]::ToUInt32($bytes, $at + 8)
		if ($size -eq 0 -or $at + 12 + $size -gt $bytes.Length) { break }
		$at += 12 + $size; $count++
	}
	$compiler = (Get-Item (Join-Path $env:WINDIR "System32\d3dcompiler_47.dll")).VersionInfo.FileVersion
	Write-Host ("{0}: {1} programs, {2} bytes, compiled by d3dcompiler_47 {3}" -f $Out, $count, $bytes.Length, $compiler)
} finally {
	if (Test-Path $aside) { Move-Item -Force $aside $cache } else { Remove-Item -Force $cache -ErrorAction SilentlyContinue }
}
